# Sidechain Support — Design Discussion

## The Problem

The PluginProcessor spec included a `setSidechainBuffer(const AudioBuffer<float>*)` method for sidechain support. This API cannot work — JUCE requires sidechain audio as extra channels *inside the same buffer* passed to `processBlock()`, not as a separate pointer. The method was removed from the spec, and sidechain is deferred to a future tier.

This document captures the design work needed to do sidechain properly.

## How JUCE Sidechain Works

JUCE plugins declare their bus layout at instantiation. A typical sidechain-capable plugin has:

```
Bus 0 (Main Input):      stereo (2 channels)
Bus 1 (Sidechain Input): mono (1 channel) or stereo (2 channels)
Bus 0 (Main Output):     stereo (2 channels)
```

When calling `processBlock()`, JUCE maps buses to contiguous channel ranges in a single `AudioBuffer`. For a stereo main + mono sidechain, the buffer must have 3 channels:

```
Channel 0: main left
Channel 1: main right
Channel 2: sidechain
```

The plugin reads channels 0–1 for main audio, channel 2 for sidechain detection, and writes its output to channels 0–1. There is no separate sidechain buffer API — it's all one buffer with a wider channel count.

### Bus Layout Negotiation

Not all plugins support sidechain. A plugin must:
1. Declare a second input bus in its `BusesProperties`
2. Accept a layout where that bus is enabled

PluginProcessor must query the plugin's bus layout to determine:
- Whether sidechain is supported at all
- How many channels the sidechain bus expects
- The total channel count needed for the processing buffer

This query happens at construction or `prepare()` time, not at runtime.

## What Sidechain Means in Squeeze

A sidechain connection says: "this processor should use *that* source's (or bus's) audio for detection, while processing its own audio in-place."

Real-world examples:
- **Ducking compressor**: Bass synth has a compressor in its chain, sidechained from the kick drum. When the kick hits, the bass ducks.
- **Gated reverb**: Reverb bus has a gate, sidechained from the snare. Reverb only opens on snare hits.
- **De-esser**: Vocal chain has a de-esser that uses the vocal's own signal (self-sidechain, which is just the main input — no routing needed).

The sidechain source is always *read-only* — its audio is copied into the sidechain channels, never modified.

## Design Considerations

### 1. Routing is an Engine Concern

Sidechain creates a dependency: the sidechain source must be processed *before* the processor that reads it. This constrains processing order.

Currently, Engine processes all sources independently ("future: parallelizable"). Sidechain introduces ordering edges between sources. If source A's chain has a compressor sidechained from source B, then B must complete before A's chain can run.

This doesn't require a full topological sort over individual processors — it's an ordering constraint on *sources* (and buses). With typical session sizes (4–16 sources, 2–8 buses), a simple dependency sort is fine.

### 2. Where the Buffer Expansion Happens

Two options:

**Option A: PluginProcessor manages its own wider buffer.**
- PluginProcessor allocates an internal buffer with main + sidechain channels at `prepare()` time.
- Before `processBlock()`: copy main audio from the chain buffer into internal channels 0–N, copy sidechain audio into channels N+1–M.
- After `processBlock()`: copy channels 0–N back to the chain buffer.
- Pros: Self-contained. Chain and Engine don't need to know about sidechain.
- Cons: Extra copy per block. PluginProcessor needs a reference to the sidechain audio.

**Option B: Engine provides a wider buffer in the snapshot.**
- Snapshot allocates wider buffers for processors with sidechain assignments.
- Engine copies sidechain audio into the extra channels before calling `process()`.
- PluginProcessor just passes the wider buffer to `processBlock()` — no internal buffer needed.
- Pros: Zero extra copies (Engine already manages all buffers). Cleaner.
- Cons: Snapshot must know about sidechain assignments. PluginProcessor must know its expected channel count (it already does via `inputChannels_`/`outputChannels_`).

**Recommendation: Option A.** Option B leaks sidechain knowledge into the snapshot model and means Chain iteration must handle processors with different buffer widths. Option A keeps the complexity in PluginProcessor where it belongs. The extra copy is negligible — sidechain buffers are small (1–2 channels, one block long) and the copy is a `memcpy` that fits in cache.

### 3. The Sidechain Source Reference

PluginProcessor needs access to the sidechain audio each block. Options:

**Pointer set by Engine before processing:**
```cpp
// Engine, during processSubBlock, before processing source A's chain:
if (proc->hasSidechain()) {
    proc->setSidechainAudio(sidechainSourceBuffer);
}
```

This is simple but requires Engine to know which processors have sidechain and resolve the source buffer. The pointer is valid only for the current block — not stored long-term.

**Via snapshot:**
The snapshot entry for each chain processor could include an optional sidechain buffer pointer, pre-resolved at snapshot build time to point at the sidechain source's buffer in the same snapshot.

**Recommendation:** Pointer set by Engine. The snapshot already has the source buffers. Engine iterates chain processors in the snapshot and checks for sidechain assignments. When found, it passes the sidechain source's buffer (already computed earlier in the same block). Simple, no new snapshot fields needed beyond the assignment itself.

### 4. API Surface

**Engine (C++):**
```cpp
void sidechain(Processor* proc, Source* source);
void sidechain(Processor* proc, Bus* bus);
void removeSidechain(Processor* proc);
```

**C ABI:**
```c
void sq_sidechain_source(SqEngine engine, SqProc proc, SqSource src);
void sq_sidechain_bus(SqEngine engine, SqProc proc, SqBus bus);
void sq_remove_sidechain(SqEngine engine, SqProc proc);
```

**Python:**
```python
compressor = bass.chain.append("Compressor.vst3")
compressor.sidechain = kick        # Source object
compressor.sidechain = drum_bus    # Bus object
compressor.sidechain = None        # remove
```

### 5. PluginProcessor Changes

```cpp
class PluginProcessor : public Processor {
    // ... existing ...

    // Sidechain
    bool supportsSidechain() const;     // queries plugin bus layout
    int getSidechainChannels() const;   // 0 if no sidechain support

    // Called by Engine before process() each block.
    // Buffer is read-only, valid only for the current block.
    void setSidechainAudio(const juce::AudioBuffer<float>* buffer);

private:
    // Wider buffer: main channels + sidechain channels
    juce::AudioBuffer<float> wideBuffer_;
    const juce::AudioBuffer<float>* sidechainAudio_ = nullptr;
    int sidechainChannels_ = 0;
};
```

In `process()`:
```
if sidechainAudio_ != nullptr && sidechainChannels_ > 0:
    // Copy main audio into wideBuffer_ channels 0..outputChannels_-1
    // Copy sidechain audio into wideBuffer_ channels outputChannels_..outputChannels_+sidechainChannels_-1
    // Call processBlock(wideBuffer_, midi)
    // Copy wideBuffer_ channels 0..outputChannels_-1 back to buffer
else:
    // Normal path: processBlock(buffer, midi) directly
```

The `wideBuffer_` is pre-allocated in `prepare()` — no allocation on the audio thread.

### 6. Ordering Constraints

Engine must track sidechain dependencies to determine source processing order:

```
sidechainDeps_: map<Processor*, Source* or Bus*>
```

When building the snapshot, Engine topologically sorts sources such that any source used as a sidechain input is processed before sources that depend on it. For buses, the existing dependency-order processing already handles this (a bus used as sidechain input for another bus's chain must come earlier in the bus DAG).

Circular sidechain dependencies (A sidechains from B, B sidechains from A) must be detected and rejected, similar to bus DAG cycle detection.

### 7. Self-Sidechain

A processor sidechaining from its own source's audio is a common case (de-essers, some compressors). This requires no special handling — the source's buffer is available since the generator and preceding chain processors have already written to it. The sidechain just reads the same buffer.

However, the *tap point* matters: is the sidechain taken from the source's chain buffer at the current processor's position (pre-processor audio) or from the source's final output? Pre-processor is the natural choice (it's what's in the buffer at that point in the chain), and it matches how hardware insert-point sidechain works.

Self-sidechain doesn't need explicit routing — it's the default when no external sidechain source is assigned but the plugin reads its own sidechain bus (which will contain the main input audio).

## Specs Affected

1. **PluginProcessor** — `supportsSidechain()`, `getSidechainChannels()`, `setSidechainAudio()`, `wideBuffer_`, modified `process()` path
2. **Engine** — `sidechain()`, `removeSidechain()`, sidechain dependency tracking, source ordering in snapshot build, sidechain buffer passing in `processSubBlock`
3. **Engine C ABI** — `sq_sidechain_source`, `sq_sidechain_bus`, `sq_remove_sidechain`
4. **Python API** — `processor.sidechain` property
5. **MixerSnapshot** — optional: sidechain assignment metadata per chain processor entry (for Engine to resolve during processing)

## Suggested Tier

Sidechain is a post-MVP feature. It requires:
- All source/bus/chain processing to be working
- Snapshot model to be stable
- Plugin hosting to be proven

It should be its own tier after the core mixer is complete, likely alongside or after PDC (which also deals with cross-source timing relationships).

## Open Questions

1. **Bus-to-bus sidechain**: A gate on the drum bus sidechained from the kick source. The kick's audio is in the kick source's buffer, but by the time the drum bus processes, sources are done. This works naturally — no special ordering needed since sources always process before buses.

2. **Sidechain from a send**: Some workflows sidechain from a send tap rather than the source output. This is an unusual case. Deferring it is reasonable — the API can be extended later with a tap-point parameter.

3. **Multiple sidechain buses**: Some plugins have more than one sidechain input bus (rare). The current design assumes one sidechain bus. Supporting multiple would require `setSidechainAudio` to take a bus index. Defer until a real plugin requires it.

4. **Latency compensation for sidechain**: If the sidechain source has a different latency path than the main audio, the sidechain signal will be misaligned. Full sidechain PDC is complex (Logic Pro and Ableton both handle this differently). Defer — most users won't notice the misalignment, and those who do can insert a manual delay.
