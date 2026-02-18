# Chain Specification

## Responsibilities

- Own an ordered list of Processors
- Support structural modification (append, insert, remove, move) on the control thread
- Provide the processor array to the Engine for snapshot building
- Report total latency as the sum of all processor latencies

## Overview

Chain is the **insert rack**. It owns an ordered sequence of Processors. Every Source and every Bus owns a Chain for insert effects.

Chain is a **control-thread-only** container. The audio thread never accesses the Chain directly. Instead, during snapshot build, the Engine calls `getProcessorArray()` to copy the current processor pointer list into the snapshot. The audio thread iterates the snapshot's array, calling each processor sequentially on the same buffer — zero-copy serial processing.

Structural modifications (adding, removing, reordering processors) happen on the control thread and are invisible to the audio thread until the next snapshot swap.

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

    // --- Structural modification (control thread only) ---
    void append(std::unique_ptr<Processor> p);
    void insert(int index, std::unique_ptr<Processor> p);
    std::unique_ptr<Processor> remove(int index);
    void move(int fromIndex, int toIndex);
    void clear();  // destroys all processors immediately

    // --- Query ---
    int size() const;
    Processor* at(int index) const;
    Processor* findByHandle(int handle) const;
    int indexOf(Processor* p) const;

    // --- Latency ---
    int getLatencySamples() const;

    // --- Snapshot support ---
    // Returns a copy of the processor pointer array for snapshot building.
    // The audio thread reads the snapshot's copy — it never accesses the Chain.
    std::vector<Processor*> getProcessorArray() const;

private:
    std::vector<std::unique_ptr<Processor>> processors_;
    double sampleRate_ = 0.0;
    int blockSize_ = 0;
};

} // namespace squeeze
```

## Audio-Thread Processing

Chain does not have a `process()` method. The audio thread iterates the snapshot's `vector<Processor*>` directly in the Engine's `processBlock()`. This is zero-copy serial processing: each processor reads from the buffer, modifies it in-place, and the next processor sees the modified result.

### Bypass enforcement

Bypass tracking is per-Processor (see Processor spec, `wasBypassed_`). The Engine reads `isBypassed()` and compares against `wasBypassed_` on each Processor during iteration. When a processor transitions from bypassed→active, the Engine calls `reset()` before the first `process()` call. This state lives on the Processor object itself, so it naturally survives snapshot swaps without any transfer logic.

### Source/Bus un-bypass

When un-bypassing a Source or Bus, the Engine calls `reset()` on the generator and every processor in the chain. This is done by iterating the snapshot's processor array — not by calling a method on Chain.

## Structural Modification

All structural operations happen on the control thread and take effect at the next snapshot swap:

### `append(unique_ptr<Processor> p)`

Add a processor to the end of the chain. Ownership transfers via `unique_ptr`. Calls `p->prepare(sampleRate, blockSize)` if the chain is already prepared.

### `insert(index, unique_ptr<Processor> p)`

Insert a processor at the given index. Elements at and after `index` shift right. Ownership transfers via `unique_ptr`. Calls `p->prepare()` if already prepared.

- If `index == size()`, equivalent to `append()`.
- If `index < 0` or `index > size()`, clamps to valid range.

### `remove(index)`

Remove and return the processor at `index` as a `unique_ptr`. The caller takes ownership and is responsible for deferred deletion (via Engine's garbage collection).

- If `index` is out of range, returns nullptr.

### `move(fromIndex, toIndex)`

Move a processor from one position to another within the chain. Other elements shift to fill the gap.

### `clear()`

Remove and destroy all processors immediately. Only safe when no snapshot references the processors (e.g., during shutdown).

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

During snapshot build, the Engine calls `getProcessorArray()` to get a copy of the current processor pointer list. The snapshot stores this `vector<Processor*>`. The audio thread iterates the snapshot's array — it never accesses the Chain.

Structural modifications (append, insert, remove, move) are invisible to the audio thread until the next snapshot swap. The control thread can modify the Chain freely while audio is processing.

## Invariants

- Chain is control-thread-only — the audio thread never accesses it
- The chain owns its processors — they are destroyed when the chain is destroyed (unless removed first)
- `prepare()` is forwarded to all current processors
- `release()` is forwarded to all current processors
- A newly appended/inserted processor is prepared if the chain is already prepared
- `getLatencySamples()` returns the sum of all processor latencies (>= 0)
- `size()` returns the current number of processors (>= 0)
- Structural modifications do not affect the audio thread until the next snapshot swap
- `remove()` returns a `unique_ptr` — the caller controls when it is destroyed

## Error Conditions

- `insert()` with out-of-range index: clamps to valid range (0 to size())
- `remove()` with out-of-range index: returns nullptr
- `at()` with out-of-range index: returns nullptr
- `move()` with out-of-range indices: no-op

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
| `append()` / `insert()` / `remove()` / `move()` / `clear()` | Control | Under Engine's controlMutex_ |
| `size()` / `at()` / `findByHandle()` / `indexOf()` | Control | Read-only queries |
| `getProcessorArray()` | Control | Called during snapshot build |
| `getLatencySamples()` | Control | Called during snapshot build |

Chain is only accessed by the control thread. The audio thread reads a snapshot copy of the processor array.

## C ABI

Chain operations are accessed through Source and Bus:

```c
// Source chain operations (plugin path — creates a PluginProcessor)
SqProc sq_source_append(SqEngine engine, SqSource src, const char* plugin_path);
SqProc sq_source_insert(SqEngine engine, SqSource src, int index, const char* plugin_path);

// Source chain operations (pre-created processor — for built-in processors)
SqProc sq_source_append_proc(SqEngine engine, SqSource src, SqProc proc);
SqProc sq_source_insert_proc(SqEngine engine, SqSource src, int index, SqProc proc);

void   sq_source_remove_proc(SqEngine engine, SqSource src, int index);
void   sq_source_move(SqEngine engine, SqSource src, int from_index, int to_index);
int    sq_source_chain_size(SqEngine engine, SqSource src);

// Bus chain operations (plugin path — creates a PluginProcessor)
SqProc sq_bus_append(SqEngine engine, SqBus bus, const char* plugin_path);
SqProc sq_bus_insert(SqEngine engine, SqBus bus, int index, const char* plugin_path);

// Bus chain operations (pre-created processor — for built-in processors)
SqProc sq_bus_append_proc(SqEngine engine, SqBus bus, SqProc proc);
SqProc sq_bus_insert_proc(SqEngine engine, SqBus bus, int index, SqProc proc);

void   sq_bus_remove_proc(SqEngine engine, SqBus bus, int index);
void   sq_bus_move(SqEngine engine, SqBus bus, int from_index, int to_index);
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

chain.append(std::move(eq));
chain.append(std::move(comp));
chain.append(std::move(limit));

// Audio thread processes via snapshot, not directly:
// for (auto* p : snapshot.chainProcessors) p->process(buffer);
```

### Live insert (hot-swap)

```cpp
// On control thread: insert a saturator between EQ and compressor
auto sat = std::make_unique<PluginProcessor>(loadPlugin("Saturator.vst3"));
chain.insert(1, std::move(sat));
// Engine rebuilds snapshot → audio thread picks up new chain at next block boundary

// Later: remove the saturator
auto removed = chain.remove(1);  // returns unique_ptr
// Engine defers deletion via garbage queue
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
