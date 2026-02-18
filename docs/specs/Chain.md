# Chain Specification

## Responsibilities

- Own an ordered list of Processors
- Process audio sequentially through all processors in-place on the same buffer
- Support structural modification (append, insert, remove, move) on the control thread
- Swap the active processor array atomically at the next block boundary for glitch-free live changes
- Report total latency as the sum of all processor latencies

## Overview

Chain is the **insert rack**. It owns an ordered sequence of Processors and calls each one sequentially on the same audio buffer — zero-copy serial processing. Every Source and every Bus owns a Chain for insert effects.

Structural modifications (adding, removing, reordering processors) happen on the control thread. The Chain builds a new processor array internally, which is swapped atomically at the next block boundary via the Engine's snapshot mechanism. The audio thread never sees a partially-modified chain.

## Interface

### C++ (`squeeze::Chain`)

```cpp
namespace squeeze {

class Chain {
public:
    Chain();
    ~Chain();

    // Non-copyable, non-movable
    Chain(const Chain&) = delete;
    Chain& operator=(const Chain&) = delete;

    // --- Lifecycle (control thread) ---
    void prepare(double sampleRate, int blockSize);
    void release();

    // --- Processing (audio thread, RT-safe) ---
    void process(juce::AudioBuffer<float>& buffer);
    void process(juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi);
    void resetAll();  // calls reset() on every processor; used by Engine on source/bus un-bypass

    // --- Structural modification (control thread only) ---
    // All return the processor handle assigned by Engine.
    void append(Processor* p);
    void insert(int index, Processor* p);
    Processor* remove(int index);
    void move(int fromIndex, int toIndex);
    void clear();

    // --- Query ---
    int size() const;
    Processor* at(int index) const;
    Processor* findByHandle(int handle) const;
    int indexOf(Processor* p) const;

    // --- Latency ---
    int getLatencySamples() const;

    // --- Snapshot support ---
    // Returns an immutable copy of the processor pointer array for the snapshot.
    // The audio thread reads this array — it never accesses the chain directly.
    std::vector<Processor*> getProcessorArray() const;

private:
    std::vector<std::unique_ptr<Processor>> processors_;
    std::vector<bool> wasBypassed_;  // per-processor bypass transition tracking (audio thread only)
    double sampleRate_ = 0.0;
    int blockSize_ = 0;
};

} // namespace squeeze
```

## Processing

`process()` calls each processor in sequence on the **same buffer**, skipping bypassed processors:

```
process(buffer, midi):
    for i in 0..size():
        p = processors_[i]
        bypassed = p->isBypassed()

        if bypassed:
            wasBypassed_[i] = true
            continue

        if wasBypassed_[i]:
            p->reset()
            wasBypassed_[i] = false

        p->process(buffer, midi)
```

The audio-only variant is identical but calls `p->process(buffer)`.

This is zero-copy serial processing. Each processor reads from the buffer, modifies it in-place, and the next processor sees the modified result. The buffer's channel count and sample count are unchanged throughout.

### Bypass enforcement

Chain is responsible for enforcing Processor bypass. It tracks per-processor bypass state across blocks (`wasBypassed_[]`, audio thread only — no atomics needed). When a processor transitions from bypassed→active, Chain calls `reset()` on it before the first `process()` call to clear stale internal state (see Processor spec, Bypass section). Bypassed processors are skipped entirely — no virtual call overhead.

### `resetAll()`

Calls `reset()` on every processor in the chain. Used by the Engine when un-bypassing a Source or Bus to clear all stale chain state at once.

```cpp
void resetAll() {
    for (auto& p : processors_)
        p->reset();
    // Also reset wasBypassed_ tracking
    std::fill(wasBypassed_.begin(), wasBypassed_.end(), false);
}
```

## Structural Modification

All structural operations happen on the control thread and take effect at the next snapshot swap:

### `append(p)`

Add a processor to the end of the chain. The chain takes ownership. Calls `p->prepare(sampleRate, blockSize)` if the chain is already prepared.

### `insert(index, p)`

Insert a processor at the given index. Elements at and after `index` shift right. The chain takes ownership. Calls `p->prepare()` if already prepared.

- If `index == size()`, equivalent to `append()`.
- If `index < 0` or `index > size()`, clamps to valid range.

### `remove(index)`

Remove and return the processor at `index`. Ownership transfers back to the caller. The caller is responsible for deferred deletion (via Engine's garbage collection).

- If `index` is out of range, returns nullptr.

### `move(fromIndex, toIndex)`

Move a processor from one position to another within the chain. Other elements shift to fill the gap.

### `clear()`

Remove all processors. Ownership transfers to the caller via deferred deletion.

## Latency

```cpp
int getLatencySamples() const {
    int total = 0;
    for (auto& p : processors_)
        total += p->getLatencySamples();
    return total;
}
```

The chain's latency is the sum of all its processor latencies. This is used by PDC to compute compensation delays.

## Snapshot Integration

The Chain itself is not directly accessed by the audio thread. Instead, during snapshot build, the Engine reads `getProcessorArray()` to get a copy of the current processor pointer list. The snapshot stores this array. The audio thread iterates the snapshot's array, not the Chain's internal vector.

This means structural modifications to the Chain (append, insert, remove, move) are invisible to the audio thread until the next snapshot swap. The control thread can modify the chain freely while audio is processing.

## Invariants

- `process()` and `resetAll()` are RT-safe: no allocation, no blocking
- Processors are called in index order (0, 1, 2, ...)
- Bypassed processors are skipped during `process()`
- On bypass→active transition, `reset()` is called before the first `process()`
- The chain owns its processors — they are destroyed when the chain is destroyed (unless removed first)
- `prepare()` is forwarded to all current processors
- `release()` is forwarded to all current processors
- A newly appended/inserted processor is prepared if the chain is already prepared
- `getLatencySamples()` returns the sum of all processor latencies (>= 0)
- `size()` returns the current number of processors (>= 0)
- Structural modifications are control-thread-only — they do not affect the audio thread until the next snapshot swap

## Error Conditions

- `insert()` with out-of-range index: clamps to valid range (0 to size())
- `remove()` with out-of-range index: returns nullptr
- `at()` with out-of-range index: returns nullptr
- `move()` with out-of-range indices: no-op
- `process()` on an empty chain: no-op (buffer passes through unchanged)

## Does NOT Handle

- **Audio buffer allocation** — the buffer is provided by Source or Bus
- **Snapshot swap mechanism** — Engine builds snapshots and swaps them via CommandQueue
- **Processor creation** — PluginManager creates PluginProcessors; built-in processors are created by Engine helper methods
- **MIDI routing** — MIDI is provided by Source (from MidiRouter); Bus chains don't receive MIDI
- **Deferred deletion** — removed processors are garbage-collected by Engine
- **Parallel processing** — chains are strictly sequential

## Dependencies

- Processor (the abstract interface Chain owns and calls)
- JUCE (`juce::AudioBuffer<float>`, `juce::MidiBuffer`)

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| `prepare()` / `release()` | Control | Forwarded to all processors |
| `process()` / `resetAll()` | Audio | Called via snapshot array, not directly on Chain |
| `append()` / `insert()` / `remove()` / `move()` / `clear()` | Control | Under Engine's controlMutex_ |
| `size()` / `at()` / `findByHandle()` / `indexOf()` | Control | Read-only queries |
| `getProcessorArray()` | Control | Called during snapshot build |
| `getLatencySamples()` | Control | Called during snapshot build |

The Chain is only directly accessed by the control thread. The audio thread accesses a snapshot copy of the processor array.

## C ABI

Chain operations are accessed through Source and Bus:

```c
// Source chain operations
SqProc sq_source_append(SqEngine engine, SqSource src, const char* plugin_path);
SqProc sq_source_insert(SqEngine engine, SqSource src, int index, const char* plugin_path);
void   sq_source_remove(SqEngine engine, SqSource src, int index);
int    sq_source_chain_size(SqEngine engine, SqSource src);

// Bus chain operations
SqProc sq_bus_append(SqEngine engine, SqBus bus, const char* plugin_path);
SqProc sq_bus_insert(SqEngine engine, SqBus bus, int index, const char* plugin_path);
void   sq_bus_remove(SqEngine engine, SqBus bus, int index);
int    sq_bus_chain_size(SqEngine engine, SqBus bus);
```

## Example Usage

### Building a channel strip

```cpp
Chain chain;
chain.prepare(44100.0, 512);

auto eq = std::make_unique<PluginProcessor>(loadPlugin("EQ.vst3"));
auto comp = std::make_unique<PluginProcessor>(loadPlugin("Comp.vst3"));
auto limit = std::make_unique<PluginProcessor>(loadPlugin("Limiter.vst3"));

chain.append(eq.release());
chain.append(comp.release());
chain.append(limit.release());

// Process in-place
juce::AudioBuffer<float> buffer(2, 512);
chain.process(buffer);
// buffer has been processed through EQ → Compressor → Limiter
```

### Live insert (hot-swap)

```cpp
// On control thread: insert a saturator between EQ and compressor
auto sat = std::make_unique<PluginProcessor>(loadPlugin("Saturator.vst3"));
chain.insert(1, sat.release());
// Engine rebuilds snapshot → audio thread picks up new chain at next block boundary

// Later: remove the saturator
Processor* removed = chain.remove(1);
// Engine defers deletion of `removed` via garbage queue
```

### Python

```python
vocal = s.add_source("Vocal", plugin="audio_input")
vocal.chain.append("EQ.vst3")
vocal.chain.append("Compressor.vst3")

# Hot-swap: insert saturator at index 1
sat = vocal.chain.insert(1, "Saturator.vst3")

# Remove it later
vocal.chain.remove(1)

# Access processor in chain
eq = vocal.chain[0]
eq.set_param("high_gain", -3.0)
```
