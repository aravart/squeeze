# PerfMonitor Specification

## Overview

PerfMonitor measures audio thread performance and exposes it to the control thread. The audio thread writes timing data with zero allocation; the control thread reads a consistent snapshot on demand.

## Responsibilities

- Measure wall-clock duration of each audio callback
- Measure per-node processing time within each callback
- Compute CPU load as a percentage of the buffer budget
- Count xruns (callbacks that exceeded the buffer duration)
- Track MIDI input queue fill levels and overflow counts
- Publish stats from the audio thread to the control thread without blocking

## Data Model

### PerfSnapshot (control-thread-readable)

```cpp
struct PerfSnapshot {
    // Callback timing (rolling window, reset each publish)
    double callbackAvgUs;      // mean callback duration in microseconds
    double callbackPeakUs;     // worst-case callback duration in window
    double cpuLoadPercent;     // callbackAvgUs / bufferDurationUs * 100

    // Per-node timing (in execution order, matches snapshot slots)
    struct NodePerf {
        int nodeId;
        double avgUs;
        double peakUs;
    };
    std::vector<NodePerf> nodes;

    // Xruns
    int xrunCount;             // cumulative since start

    // MIDI queue health
    struct MidiQueuePerf {
        int nodeId;
        std::string deviceName;
        int fillLevel;         // items in queue at last sample
        int peakFillLevel;     // high-water mark since last reset
        int droppedCount;      // cumulative overflow count
    };
    std::vector<MidiQueuePerf> midi;

    // System context
    double sampleRate;
    int blockSize;
    double bufferDurationUs;   // blockSize / sampleRate * 1e6
};
```

## Interface

```cpp
namespace squeeze {

class PerfMonitor {
public:
    PerfMonitor();

    // --- Control thread ---

    void enable();
    void disable();
    bool isEnabled() const;

    // Enable/disable per-node timing breakdown.
    // When disabled (default), only callback-level timing is measured
    // (2 clock reads per callback). When enabled, adds 2 clock reads
    // per node per callback.
    void enableNodeProfiling();
    void disableNodeProfiling();
    bool isNodeProfilingEnabled() const;

    // Configure the budget for CPU load calculation.
    // Called once from Engine::audioDeviceAboutToStart or prepareForTesting.
    void prepare(double sampleRate, int blockSize);

    // Return the latest snapshot. Returns a default snapshot if disabled
    // or if no data has been published yet.
    PerfSnapshot getSnapshot() const;

    // Reset cumulative counters (xrunCount, dropped counts).
    void resetCounters();

    // --- Audio thread (all methods are RT-safe) ---

    // Call at the start of processBlock, before any work.
    void beginCallback();

    // Call at the end of processBlock, after all work including leaf summing.
    void endCallback();

    // Call around each node's process() call.
    // No-op when node profiling is disabled.
    // slotIndex is the position in GraphSnapshot::slots.
    // nodeId is the graph node ID (for identification in the snapshot).
    void beginNode(int slotIndex, int nodeId);
    void endNode(int slotIndex);

    // Report MIDI queue state for a node. Called once per callback
    // for each MidiInputNode, after draining its queue.
    void reportMidiQueue(int nodeId, int fillLevel, int dropped);
};

} // namespace squeeze
```

## RT-Side Accumulation

The audio thread writes into a fixed-size internal accumulator. No heap allocation occurs on the audio thread.

```
┌─────────────────────────────────────────────────┐
│ Audio Thread (writer)                           │
│                                                 │
│  beginCallback()  ─── record start time         │
│  for each node:                                 │
│    beginNode(i, id)  ─── record node start      │
│    node->process()                              │
│    endNode(i)        ─── record node end        │
│  endCallback()    ─── compute durations,        │
│                       update accumulators,       │
│                       maybe publish              │
└─────────────────────────────────────────────────┘
         │ publish (every ~100ms worth of callbacks)
         ▼
┌─────────────────────────────────────────────────┐
│ SeqLock double-buffer                           │
│  [sequence counter + two PerfData buffers]      │
└─────────────────────────────────────────────────┘
         │ getSnapshot() reads consistent copy
         ▼
┌─────────────────────────────────────────────────┐
│ Control Thread (reader)                         │
└─────────────────────────────────────────────────┘
```

### Fixed-Size Accumulators

```cpp
static constexpr int kMaxNodes = 256;
static constexpr int kMaxMidiNodes = 32;

struct RTAccumulator {
    // Callback-level
    double callbackSumUs = 0.0;
    double callbackPeakUs = 0.0;
    int callbackCount = 0;

    // Per-node
    struct NodeAcc {
        int nodeId = -1;
        double sumUs = 0.0;
        double peakUs = 0.0;
    };
    std::array<NodeAcc, kMaxNodes> nodes;
    int nodeCount = 0;

    // MIDI
    struct MidiAcc {
        int nodeId = -1;
        int fillLevel = 0;
        int peakFillLevel = 0;
        int droppedCount = 0;
    };
    std::array<MidiAcc, kMaxMidiNodes> midi;
    int midiCount = 0;

    // Xruns
    int xrunCount = 0;
};
```

### Publish Strategy

After each `endCallback()`, check if the accumulation window has elapsed (~100ms, computed as a callback count threshold: `sampleRate / blockSize / 10`). If so:

1. Compute averages from sums and counts
2. Write the completed data to the inactive side of the double-buffer
3. Increment the sequence counter (atomic, release)
4. Reset the accumulator for the next window

### SeqLock Protocol

The audio thread (writer) and control thread (reader) share two `RTPublishedData` buffers and an atomic sequence counter.

**Writer (audio thread, in `endCallback`):**
```
sequence.store(sequence + 1, release)   // odd = write in progress
write data to buffer[sequence / 2 % 2]
sequence.store(sequence + 1, release)   // even = write complete
```

**Reader (control thread, in `getSnapshot`):**
```
do {
    s1 = sequence.load(acquire)
    if (s1 is odd) continue              // write in progress, retry
    copy data from buffer[s1 / 2 % 2]
    s2 = sequence.load(acquire)
} while (s1 != s2)                       // data changed during read, retry
```

This is wait-free for the writer and lock-free for the reader. The reader may retry but never blocks the writer.

## Xrun Detection

An xrun is detected in `endCallback()` when `callbackDurationUs > bufferDurationUs`. The xrun counter is cumulative and persists across publish windows. `resetCounters()` zeroes it.

When an xrun is detected, `endCallback()` emits a log message via `SQ_LOG_RT_WARN`. This is RT-safe — it uses the existing Logger SPSC queue (fixed-size `snprintf` into a `LogEntry`, push to lock-free queue, drained on the control thread). The message includes the callback duration and the budget:

```
[000842][RT] PerfMonitor.cpp:95 xrun: 3412us (budget 2902us), total 3
```

`SQ_LOG_RT_WARN` gates on `level >= warn`. Since Logger defaults to `warn`, xrun messages appear without any logging flags. They can be silenced with `Logger::setLevel(LogLevel::off)` if needed.

## MIDI Queue Monitoring

`reportMidiQueue(nodeId, fillLevel, dropped)` is called by Engine during processBlock for each MidiInputNode. This requires:

- `MidiInputNode::getQueueFillLevel()` — returns current SPSC queue size
- `MidiInputNode::getDroppedCount()` — returns cumulative overflow count

These are new methods on MidiInputNode (see Dependencies).

Fill levels and dropped counts are accumulated per publish window. `peakFillLevel` tracks the maximum seen.

## Engine Integration

Engine calls PerfMonitor from `processBlock`:

```cpp
void Engine::processBlock(AudioBuffer<float>& out, MidiBuffer& outMidi, int numSamples)
{
    perfMonitor_.beginCallback();

    // ... drain scheduler ...

    for (int i = 0; i < slotCount; ++i) {
        perfMonitor_.beginNode(i, snap.slots[i].node->getId());  // no-op if node profiling disabled
        snap.slots[i].node->process(ctx);
        perfMonitor_.endNode(i);                                  // no-op if node profiling disabled
    }

    // ... sum leaf nodes ...

    // report MIDI queue health
    for (each MidiInputNode) {
        perfMonitor_.reportMidiQueue(nodeId, fillLevel, dropped);
    }

    perfMonitor_.endCallback();
}
```

**Gating behavior:**
- When PerfMonitor is **disabled**: all RT methods are no-ops (one relaxed atomic load + branch per `beginCallback`, all other calls skipped). Same pattern as Logger.
- When PerfMonitor is **enabled but node profiling is off**: `beginCallback`/`endCallback` measure total callback time (2 clock reads per callback). `beginNode`/`endNode` are no-ops (one relaxed atomic load + branch each). `PerfSnapshot::nodes` will be empty.
- When PerfMonitor is **enabled and node profiling is on**: full per-node timing (2 additional clock reads per node per callback).

## Invariants

- All audio-thread methods are RT-safe: no allocation, no blocking, no I/O
- `enable()`/`disable()` use a relaxed atomic; safe from any thread
- `getSnapshot()` never blocks the audio thread
- Fixed-size arrays cap per-node tracking at `kMaxNodes` (256)
- Nodes beyond `kMaxNodes` are silently ignored (not tracked)
- A disabled PerfMonitor adds near-zero overhead to processBlock (one relaxed atomic load + branch)
- With node profiling off, callback-level timing costs only 2 clock reads per callback
- With node profiling on, adds 2 clock reads per node per callback
- Xrun log messages use `SQ_LOG_RT_WARN` — emitted at default log level, RT-safe
- Xrun count is cumulative and only reset by explicit `resetCounters()` call
- `getSnapshot()` returns default (zeroed) values if no data has been published yet
- `prepare()` must be called before meaningful data is produced

## Error Conditions

- `getSnapshot()` before `prepare()`: returns default snapshot with zero values
- More than `kMaxNodes` nodes: excess nodes not tracked, no crash
- More than `kMaxMidiNodes` MIDI nodes: excess not tracked, no crash
- `beginNode`/`endNode` with `slotIndex >= kMaxNodes`: ignored
- `getSnapshot()` during a write: SeqLock retries, returns previous consistent snapshot

## Does NOT Handle

- Per-node memory allocation tracking (would require global new/delete override)
- Disk I/O or network latency monitoring
- Historical data / time series (only latest window)
- Automatic xrun recovery or buffer size adjustment
- Thread identification (assumes single audio thread, single control thread)

## Dependencies

- `<chrono>` — `steady_clock` for timing
- `<atomic>` — sequence counter, enable flag, node profiling flag
- `<array>` — fixed-size RT accumulators
- Logger — `SQ_LOG_RT_WARN` macro for xrun messages
- MidiInputNode — new methods: `getQueueFillLevel()`, `getDroppedCount()`
- Engine — integration into `processBlock`

## Thread Safety

- `enable()`, `disable()`, `isEnabled()`: safe from any thread (atomic)
- `enableNodeProfiling()`, `disableNodeProfiling()`, `isNodeProfilingEnabled()`: safe from any thread (atomic)
- `prepare()`: control thread only
- `beginCallback()`, `endCallback()`, `beginNode()`, `endNode()`, `reportMidiQueue()`: audio thread only
- `getSnapshot()`: control thread only (lock-free, never blocks audio)
- `resetCounters()`: control thread only

## Lua API

Exposed via Engine, registered in LuaBindings:

```lua
-- Returns a table with the latest performance snapshot
local p = sq.perf()
p.cpu              -- CPU load percentage (e.g. 23.5)
p.callback_avg_us  -- average callback duration in microseconds
p.callback_peak_us -- peak callback duration
p.xruns            -- cumulative xrun count
p.budget_us        -- buffer duration in microseconds (the deadline)
p.sample_rate      -- current sample rate
p.block_size       -- current block size

-- Per-node breakdown (array in execution order)
for i, n in ipairs(p.nodes) do
    n.id            -- node ID
    n.avg_us        -- average processing time
    n.peak_us       -- peak processing time
end

-- MIDI queue health (array, one per MidiInputNode)
for i, m in ipairs(p.midi) do
    m.id             -- node ID
    m.device         -- device name
    m.fill           -- current queue fill level
    m.peak_fill      -- peak fill level
    m.dropped        -- cumulative dropped messages
end

-- Enable/disable per-node profiling
sq.perf_nodes(true)   -- enable per-node timing
sq.perf_nodes(false)  -- disable (default)

-- Reset cumulative counters
sq.perf_reset()
```

## Example Usage

```cpp
// In Engine constructor or start()
perfMonitor_.prepare(sampleRate, blockSize);

// Enable from main.cpp or Lua
engine.getPerfMonitor().enable();

// In Lua REPL
// > sq.perf()
// {
//   cpu = 18.3,
//   callback_avg_us = 532.1,
//   callback_peak_us = 891.4,
//   budget_us = 2902.5,
//   xruns = 0,
//   nodes = {
//     { id = 1, avg_us = 12.3, peak_us = 18.1 },
//     { id = 2, avg_us = 498.7, peak_us = 812.0 }
//   },
//   midi = {
//     { id = 3, device = "Arturia KeyStep", fill = 2, peak_fill = 14, dropped = 0 }
//   }
// }
```
