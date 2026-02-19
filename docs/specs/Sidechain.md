# Sidechain Specification

## Responsibilities

- Allow a processor's sidechain input to be fed from a Source or Bus
- Query whether a plugin supports sidechain input
- Manage the wider processing buffer (main + sidechain channels) inside PluginProcessor
- Track sidechain dependencies and enforce source and bus processing order
- Detect and reject circular sidechain dependencies (source-to-source and bus-to-bus)
- Clean up sidechain assignments when processors, sources, or buses are removed
- Provide sidechain assignment and removal through Engine, C ABI, and Python

## Overview

A sidechain connection says: "this processor should use *that* source's (or bus's) audio for detection, while processing its own main audio in-place." The classic example is a compressor on a bass source, sidechained from the kick drum — when the kick hits, the compressor ducks the bass.

JUCE plugins implement sidechain by declaring a second input bus in their `BusesProperties`. At `processBlock()` time, the sidechain audio must appear as extra channels in the *same* `AudioBuffer` — there is no separate sidechain buffer API. For a stereo main + mono sidechain, the buffer has 3 channels: 0–1 main, 2 sidechain.

Sidechain is a cross-cutting feature. It touches PluginProcessor (wider buffer management), Engine (dependency tracking, buffer passing, ordering), the C ABI, and Python. This spec covers the full surface.

### Signal flow

```
                    sidechain source
                          │
                          ▼ (read-only copy)
main audio ──▶ [Processor] ──▶ main audio (modified)
                  (e.g. compressor uses sidechain
                   for detection, processes main audio)
```

The sidechain source audio is **read-only** — it is copied into the processor's sidechain channels, never modified.

## Interface

### C++ — PluginProcessor additions

```cpp
class PluginProcessor : public Processor {
public:
    // ... existing interface ...

    // --- Sidechain queries ---
    bool supportsSidechain() const;     // true if plugin declares a sidechain input bus
    int getSidechainChannels() const;   // 0 if no sidechain support

    // --- Sidechain audio (called by Engine before process(), audio thread) ---
    // Pointer is read-only, valid only for the current block. nullptr = no sidechain this block.
    void setSidechainAudio(const juce::AudioBuffer<float>* buffer);

private:
    // Wide buffer: main channels + sidechain channels, pre-allocated in prepare()
    juce::AudioBuffer<float> wideBuffer_;
    const juce::AudioBuffer<float>* sidechainAudio_ = nullptr;
    int sidechainChannels_ = 0;  // cached from plugin bus layout query
};
```

### C++ — Engine additions

```cpp
class Engine {
public:
    // ... existing interface ...

    // --- Sidechain (control thread) ---
    bool sidechain(Processor* proc, Source* source);
    bool sidechain(Processor* proc, Bus* bus);
    bool removeSidechain(Processor* proc);
    bool hasSidechain(Processor* proc) const;
    Source* getSidechainSource(Processor* proc) const;  // nullptr if none or kind != source
    Bus* getSidechainBus(Processor* proc) const;        // nullptr if none or kind != bus
};
```

### C ABI

```c
// Sidechain assignment
bool     sq_sidechain_source(SqEngine engine, SqProc proc, SqSource src, char** error);
bool     sq_sidechain_bus(SqEngine engine, SqProc proc, SqBus bus, char** error);
bool     sq_remove_sidechain(SqEngine engine, SqProc proc);

// Sidechain queries
bool     sq_has_sidechain(SqEngine engine, SqProc proc);
SqSource sq_get_sidechain_source(SqEngine engine, SqProc proc);  // NULL if none or bus type
SqBus    sq_get_sidechain_bus(SqEngine engine, SqProc proc);     // NULL if none or source type
bool     sq_supports_sidechain(SqEngine engine, SqProc proc);
int      sq_sidechain_channels(SqEngine engine, SqProc proc);
```

`sq_get_sidechain_source()` and `sq_get_sidechain_bus()` return the current assignment. Exactly one returns non-NULL when `sq_has_sidechain()` is true. The Python getter calls both and wraps the non-NULL result.

### Python

```python
# Sidechain is a property on Processor
class Processor:
    @property
    def sidechain(self) -> Source | Bus | None:
        """The current sidechain source, or None."""
        ...

    @sidechain.setter
    def sidechain(self, target: Source | Bus | None) -> None:
        """Assign a sidechain source/bus, or None to remove."""
        ...

    @property
    def supports_sidechain(self) -> bool:
        """Whether this processor's plugin supports sidechain input."""
        ...

    @property
    def sidechain_channels(self) -> int:
        """Number of sidechain channels the plugin expects (0 if unsupported)."""
        ...
```

Usage:

```python
kick = s.add_source("Kick", sampler="kick.wav")
bass = s.add_source("Bass", plugin="Diva.vst3")

compressor = bass.chain.append("Compressor.vst3")
compressor.sidechain = kick       # sidechain from kick source

# Sidechain from a bus
drum_bus = s.add_bus("Drums")
gate = reverb_bus.chain.append("Gate.vst3")
gate.sidechain = drum_bus         # sidechain from bus

# Remove sidechain
compressor.sidechain = None
```

## PluginProcessor — Bus Layout Query

At construction, PluginProcessor queries the plugin's bus layout to determine sidechain support:

1. Check if the plugin declares more than one input bus (`getBusCount(true) > 1`)
2. If so, check if the second input bus can be enabled (`setBusLayout()` with the sidechain bus active)
3. Cache the sidechain channel count (`getBus(true, 1)->getNumberOfChannels()`)

This query happens once at construction, not at runtime. The result is immutable.

```cpp
// In PluginProcessor constructor:
auto* plugin = processor_.get();
if (plugin->getBusCount(true) > 1) {
    auto layout = plugin->getBusesLayout();
    auto scBus = layout.getChannelSet(true, 1);
    if (!scBus.isDisabled()) {
        sidechainChannels_ = scBus.size();
    } else {
        // Try enabling it
        layout.getChannelSet(true, 1) = juce::AudioChannelSet::mono();
        if (plugin->setBusesLayout(layout)) {
            sidechainChannels_ = 1;
        }
        // else: plugin doesn't support sidechain
    }
}
```

## PluginProcessor — Wide Buffer Processing

When a sidechain is active, PluginProcessor uses a wider internal buffer. The buffer is pre-allocated in `prepare()` — no allocation on the audio thread.

### `prepare()` additions

```cpp
void PluginProcessor::prepare(double sampleRate, int blockSize) {
    // ... existing prepare ...

    if (sidechainChannels_ > 0) {
        wideBuffer_.setSize(outputChannels_ + sidechainChannels_, blockSize);
    }
}
```

### Modified `process()` path

```
process(buffer, midi):
    if sidechainAudio_ != nullptr && sidechainChannels_ > 0:
        // 1. Copy main audio into wideBuffer_ channels 0..outputChannels_-1
        for ch in 0..outputChannels_-1:
            wideBuffer_.copyFrom(ch, 0, buffer, ch, 0, numSamples)

        // 2. Copy sidechain audio into wideBuffer_ channels outputChannels_..end
        scChannels = min(sidechainAudio_->getNumChannels(), sidechainChannels_)
        for ch in 0..scChannels-1:
            wideBuffer_.copyFrom(outputChannels_ + ch, 0,
                                 *sidechainAudio_, ch, 0, numSamples)
        // Zero any remaining sidechain channels (if plugin expects stereo but source is mono)
        for ch in scChannels..sidechainChannels_-1:
            wideBuffer_.clear(outputChannels_ + ch, 0, numSamples)

        // 3. Process
        processor_->processBlock(wideBuffer_, tempMidi_)

        // 4. Copy main output back
        for ch in 0..outputChannels_-1:
            buffer.copyFrom(ch, 0, wideBuffer_, ch, 0, numSamples)

        // 5. Clear pointer (single-block lifetime)
        sidechainAudio_ = nullptr
    else:
        // Normal path: processBlock(buffer, tempMidi_) directly
        processor_->processBlock(buffer, tempMidi_)
```

The extra copy is negligible — sidechain buffers are small (1–2 channels, one block) and fit in L1 cache.

## Engine — Sidechain Dependency Tracking

Engine maintains a sidechain assignment map on the control thread:

```cpp
// In Engine private:
struct SidechainAssignment {
    enum class Kind { source, bus };
    Kind kind;
    Source* source = nullptr;  // if kind == source
    Bus* bus = nullptr;        // if kind == bus
};
std::unordered_map<Processor*, SidechainAssignment> sidechainMap_;
```

### Assignment

`Engine::sidechain(proc, source)` and `Engine::sidechain(proc, bus)`:

1. Acquire `controlMutex_`, collect garbage
2. Validate: processor exists in registry, plugin supports sidechain (`supportsSidechain()`)
3. Validate: no circular dependency (see below)
4. Store assignment in `sidechainMap_`
5. `buildAndSwapSnapshot()` — ordering may change
6. Return true on success

### Removal

`Engine::removeSidechain(proc)`:

1. Acquire `controlMutex_`, collect garbage
2. Erase from `sidechainMap_`
3. `buildAndSwapSnapshot()`
4. Return true if an assignment was removed, false if none existed

### Source Processing Order

Without sidechain, sources are independent and can be processed in any order. Sidechain introduces ordering edges: if source A has a processor sidechained from source B, then B must be processed before A.

Engine computes source processing order in `buildAndSwapSnapshot()`:

1. Build a dependency graph over sources: for each sidechain assignment where kind == source, add an edge from the sidechain source to the source that owns the processor
2. Topological sort the sources
3. Sources with no sidechain dependencies retain their original order (stable sort)

With typical session sizes (4–16 sources), this is trivially cheap.

### Bus Processing Order

Sidechain adds dependency edges to the bus DAG alongside the existing `routeTo` and send edges. If a processor in Bus A's chain is sidechained from Bus B, then Bus B must be processed before Bus A.

Engine already sorts buses in dependency order in `buildAndSwapSnapshot()`. Sidechain extends this:

1. Start with existing bus DAG edges (routeTo, sends)
2. For each sidechain assignment where kind == bus: add an edge from the sidechain bus to the bus whose chain contains the processor
3. Topological sort the combined DAG

This is the same dependency sort, with additional edges. Cost is unchanged — the bus DAG has ~4-8 nodes.

Source-to-bus-processor sidechain (a processor in a bus chain sidechained from a source) requires no bus ordering — sources always complete before buses.

### Cycle Detection

Circular sidechain dependencies must be detected and rejected. Sidechain edges participate in cycle detection alongside routeTo and send edges:

**Source-to-source cycles:**
- Source A has a processor sidechained from source B, and source B has a processor sidechained from source A

**Bus-to-bus cycles:**
- Bus A has a processor sidechained from Bus B, and Bus B routes to Bus A (or vice versa)
- Bus A has a processor sidechained from Bus B, and Bus B has a processor sidechained from Bus A

**Not cycles (work naturally):**
- Source A sidechained from Bus X, where Bus X receives input from source A — sources always process before buses, so the source buffer is ready when the bus chain runs

Source cycle detection is a DFS/BFS on the source dependency graph (sidechain edges only — sources have no other inter-source edges).

Bus cycle detection extends the existing bus DAG check: when `Engine::sidechain(proc, bus)` is called, the Engine adds the sidechain edge to the bus DAG and checks for cycles using the same BFS/DFS used for `busRoute()` and `busSend()`. The sidechain edge is treated identically to a routeTo or send edge for ordering and cycle purposes.

### Buffer Passing in processSubBlock

During `processSubBlock`, when Engine iterates chain processors for a source or bus:

```
for each proc in snapshot.chainProcessors:
    bypassed = proc->isBypassed()

    if !bypassed && proc->wasBypassed_:
        proc->reset()

    if !bypassed:
        if proc has sidechain assignment:
            resolve the sidechain buffer:
                - if sidechain from source: use that source's snapshot buffer
                  (already computed earlier in this block, guaranteed by ordering)
                - if sidechain from bus: use that bus's snapshot buffer
                  (already computed — sources before buses, buses in dependency order)
            proc->setSidechainAudio(&sidechainBuffer)
        proc->process(buffer, midiBuffer)

    proc->wasBypassed_ = bypassed
```

`setSidechainAudio()` is only called when the processor will actually run. The pointer is cleared inside `process()` after the wide-buffer path completes. When bypassed, `setSidechainAudio()` is never called and no stale pointer persists.

### Sidechain Cleanup

Sidechain assignments must be cleaned up when the objects they reference are removed. This cleanup happens inside existing Engine methods, under `controlMutex_`, before `buildAndSwapSnapshot()`:

**Processor removed from chain** (`sourceRemove()`, `busRemove()`):
- Erase the removed processor from `sidechainMap_` if present

**Source removed** (`removeSource()`):
- Erase any `sidechainMap_` entries where the removed source is the sidechain target (kind == source, source == removedSource)
- Erase any `sidechainMap_` entries keyed by processors that belonged to the removed source's chain

**Bus removed** (`removeBus()`):
- Erase any `sidechainMap_` entries where the removed bus is the sidechain target (kind == bus, bus == removedBus)
- Erase any `sidechainMap_` entries keyed by processors that belonged to the removed bus's chain

In all cases, the subsequent `buildAndSwapSnapshot()` recomputes source ordering and bus dependency order without the removed edges.

### MixerSnapshot additions

```cpp
struct MixerSnapshot {
    // ... existing fields ...

    struct SourceEntry {
        // ... existing fields ...
        // Sidechain assignments for processors in this source's chain
        // Parallel to chainProcessors — nullptr means no sidechain for that slot
        struct SidechainRef {
            enum class Kind { none, source, bus };
            Kind kind = Kind::none;
            int sourceIndex = -1;  // index into snapshot.sources
            int busIndex = -1;     // index into snapshot.buses
        };
        std::vector<SidechainRef> sidechainRefs;
    };

    struct BusEntry {
        // ... existing fields ...
        std::vector<SourceEntry::SidechainRef> sidechainRefs;  // same structure
    };
};
```

Sidechain references are resolved to snapshot indices at build time. The audio thread uses these indices to find the correct buffer — no map lookups, no string operations on the audio thread.

## Self-Sidechain

**No sidechain assigned:** When no sidechain assignment exists for a processor, the wide-buffer path is never entered — `sidechainAudio_` is nullptr, so `process()` takes the normal path and calls `processBlock()` with the standard-width buffer. The plugin never sees its sidechain input bus. What the plugin does internally in this case depends on JUCE's default behavior (typically zeroed sidechain channels or the plugin's own fallback), but Squeeze does not populate sidechain channels.

**Explicit self-sidechain:** A processor can be sidechained from its own source. This is useful for de-essers and some compressors that use the input signal for detection:

```python
compressor.sidechain = bass  # bass is the source that owns this compressor
```

This works because the Engine passes the source's snapshot buffer as the sidechain audio. At the point in the chain where the compressor runs, that buffer contains the generator output plus all preceding chain processors — the current audio state. The copy into `wideBuffer_` (step 1 of the wide-buffer path) captures this state *before* `processBlock()` modifies the main channels, so the sidechain audio reflects the pre-processor signal. No special-casing required.

Self-sidechain introduces no ordering constraints — a source cannot depend on itself.

## Invariants

- `supportsSidechain()` and `getSidechainChannels()` are immutable after construction
- `wideBuffer_` is pre-allocated in `prepare()` — no allocation on the audio thread
- `setSidechainAudio()` pointer is valid only for the current block
- The sidechain source buffer is read-only — never modified by the consuming processor
- Source processing order respects sidechain dependencies (source-to-source edges)
- Bus processing order respects sidechain dependencies (sidechain edges added to bus DAG alongside routeTo and send edges)
- Circular sidechain dependencies are rejected (source-to-source and bus-to-bus)
- Sidechain assignments trigger a snapshot rebuild
- A processor can have at most one sidechain assignment
- Removing a processor from a chain also removes its sidechain assignment
- Removing a source or bus that is a sidechain target removes all assignments referencing it

## Error Conditions

- `sidechain(proc, source)` where proc doesn't support sidechain: returns false, sets `*error` ("processor does not support sidechain input"), logged at warn
- `sidechain(proc, source)` where proc is not in any chain: returns false, sets `*error`
- `sidechain(proc, source)` that would create a circular source dependency: returns false, sets `*error` ("sidechain from source 'X' to source 'Y' would create a cycle"), logged at warn
- `sidechain(proc, bus)` that would create a cycle in the bus DAG: returns false, sets `*error` ("sidechain from bus 'X' to bus 'Y' would create a cycle"), logged at warn
- `removeSidechain(proc)` with no existing assignment: returns false (not an error, no log)
- `sq_supports_sidechain()` on a non-PluginProcessor: returns false (built-in processors don't have sidechain)
- `sq_sidechain_channels()` on a non-PluginProcessor: returns 0
- C ABI functions with unknown/invalid proc handle: `sq_sidechain_source()` and `sq_sidechain_bus()` return false and set `*error`; `sq_remove_sidechain()` returns false; `sq_has_sidechain()` and `sq_supports_sidechain()` return false; `sq_sidechain_channels()` returns 0; `sq_get_sidechain_source()` and `sq_get_sidechain_bus()` return NULL. Consistent with other `sq_*` functions' handling of unknown handles.
- Plugin rejects the sidechain bus layout during PluginProcessor construction: `sidechainChannels_` stays 0, `supportsSidechain()` returns false — no error, sidechain is simply unavailable for this plugin

## Does NOT Handle

- **Sidechain from a send tap** — sidechain reads the source or bus output buffer, not a send tap point. Could be extended later with a tap-point parameter.
- **Multiple sidechain input buses** — assumes one sidechain bus per plugin. Rare plugins with multiple sidechain buses are not supported. Can be extended later with a bus index parameter.
- **Sidechain latency compensation** — if the sidechain source has a different latency path than the main audio, the sidechain signal may be misaligned. Full sidechain PDC is deferred — most users won't notice, and those who do can insert a manual delay.
- **Sidechain for built-in processors** — only PluginProcessor supports sidechain (it requires JUCE bus layout negotiation). Built-in processors (GainProcessor, etc.) do not have sidechain inputs.
- **MIDI sidechain** — audio sidechain only.

## Dependencies

- **PluginProcessor** — `supportsSidechain()`, `getSidechainChannels()`, `setSidechainAudio()`, wide buffer management
- **Processor** — base class (sidechain is PluginProcessor-specific but the assignment is tracked by handle)
- **Engine** — `sidechain()`, `removeSidechain()`, dependency tracking, source ordering, buffer passing in `processSubBlock`
- **Source** — sidechain source (audio buffer provider)
- **Bus** — sidechain source (audio buffer provider)
- **MixerSnapshot** — sidechain reference metadata per chain processor entry
- **JUCE** — `AudioProcessor::getBusCount()`, `getBusesLayout()`, `setBusesLayout()`, `AudioChannelSet`

## Specs Requiring Updates

This feature is cross-cutting. The following existing specs must be updated when Sidechain is implemented:

- **PluginProcessor** — Remove "Sidechain — future" from "Does NOT Handle" (line 167). Add `supportsSidechain()`, `getSidechainChannels()`, `setSidechainAudio()`, `wideBuffer_`, and the modified `process()` path to the interface and processing sections.
- **Engine** — Add `sidechain()`, `removeSidechain()`, `hasSidechain()`, `getSidechainSource()`, `getSidechainBus()` to the C++ interface. Add `sq_sidechain_source`, `sq_sidechain_bus`, `sq_remove_sidechain`, `sq_has_sidechain`, `sq_get_sidechain_source`, `sq_get_sidechain_bus`, `sq_supports_sidechain`, `sq_sidechain_channels` to the C ABI. Update `processSubBlock` pseudocode to include sidechain buffer passing. Add sidechain cleanup to `removeSource()`, `removeBus()`, `sourceRemove()`, `busRemove()`. Add sidechain edges to bus DAG sort in `buildAndSwapSnapshot()`.
- **Architecture** — The processing loop's source iteration note ("independent — future: parallelizable") needs a caveat: sources with sidechain dependencies are ordered and cannot be parallelized with their dependencies.
- **PythonAPI** — Add `processor.sidechain`, `processor.supports_sidechain`, and `processor.sidechain_channels` properties.

## Thread Safety

| Method / Operation | Thread | Notes |
|---|---|---|
| `supportsSidechain()` | Any | Immutable after construction |
| `getSidechainChannels()` | Any | Immutable after construction |
| `setSidechainAudio()` | Audio | Called by Engine before `process()` each block |
| `Engine::sidechain()` | Control | Acquires `controlMutex_`, triggers snapshot rebuild |
| `Engine::removeSidechain()` | Control | Acquires `controlMutex_`, triggers snapshot rebuild |
| `Engine::hasSidechain()` | Control | Acquires `controlMutex_` |
| `Engine::getSidechainSource()` | Control | Acquires `controlMutex_` |
| `Engine::getSidechainBus()` | Control | Acquires `controlMutex_` |
| Sidechain dependency sort | Control | Inside `buildAndSwapSnapshot()` |
| Sidechain buffer resolution | Audio | Index lookup in snapshot arrays, RT-safe |
| Wide buffer copy | Audio | RT-safe (`memcpy` of 1–2 channels, one block) |

## Example Usage

### C ABI

```c
SqEngine engine = sq_create(44100.0, 512, &error);

SqSource kick = sq_add_source_plugin(engine, "Kick", "kick_sampler", &error);
SqSource bass = sq_add_source_plugin(engine, "Bass", "Diva.vst3", &error);

SqProc comp = sq_source_append(engine, bass, "Compressor.vst3");

if (sq_supports_sidechain(engine, comp)) {
    if (!sq_sidechain_source(engine, comp, kick, &error)) {
        fprintf(stderr, "Sidechain failed: %s\n", error);
        sq_free_string(error);
    }
}

sq_render(engine, 512);

// Remove sidechain
sq_remove_sidechain(engine, comp);

sq_destroy(engine);
```

### Python

```python
s = Squeeze()

kick = s.add_source("Kick", sampler="kick.wav")
bass = s.add_source("Bass", plugin="Diva.vst3")

comp = bass.chain.append("Compressor.vst3")

if comp.supports_sidechain:
    comp.sidechain = kick       # duck bass when kick hits

# Gated reverb: gate on reverb bus, sidechained from snare
snare = s.add_source("Snare", sampler="snare.wav")
reverb_bus = s.add_bus("Reverb")
reverb_bus.chain.append("ValhallaRoom.vst3")
gate = reverb_bus.chain.append("Gate.vst3")
gate.sidechain = snare

# Remove
comp.sidechain = None
```

### C++ headless test

```cpp
Engine engine(44100.0, 512);

auto kickGen = std::make_unique<TestSynthProcessor>();
auto* kick = engine.addSource("Kick", std::move(kickGen));

auto bassGen = std::make_unique<TestSynthProcessor>();
auto* bass = engine.addSource("Bass", std::move(bassGen));

auto comp = std::make_unique<PluginProcessor>(
    std::move(compPlugin), 2, 2, false);
auto* compProc = engine.sourceAppend(bass, std::move(comp));

if (compProc->supportsSidechain()) {
    engine.sidechain(compProc, kick);
}

engine.render(512);

// Verify kick source was processed before bass source
// Verify compressor received kick audio in sidechain channels
```
