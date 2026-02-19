# PerfMonitor Specification

## Responsibilities

- Measure wall-clock duration of each `processBlock` callback on the audio thread
- Compute rolling statistics (average, peak) over ~100ms windows
- Compute CPU load as a percentage of the buffer budget
- Detect and count xruns (callbacks that exceeded a configurable fraction of the budget)
- Optionally measure per-slot processing time (each Source and Bus in the processing loop)
- Publish stats from the audio thread to the control thread via a seqlock — zero allocation, never blocks the writer
- Track cumulative callback count for xrun rate computation

## Overview

PerfMonitor is an internal Engine component that instruments the audio thread with near-zero overhead when disabled. The audio thread calls `beginBlock()` / `endBlock()` around the entire `processBlock` body and optionally `beginSlot()` / `endSlot()` around each Source and Bus `process()` call. Stats are accumulated over a rolling window (~100ms) and published to a seqlock-protected buffer that the control thread reads on demand.

There are three overhead tiers:

| State | Cost per processBlock |
|-------|----------------------|
| Disabled (default) | 1 relaxed atomic load + branch in `beginBlock`; everything else skipped |
| Enabled, slot profiling off | 2 `steady_clock::now()` calls (start + end of block) |
| Enabled, slot profiling on | 2 + 2N `steady_clock::now()` calls (N = number of sources + buses) |

## Data Model

### PerfSnapshot (control-thread-readable)

```cpp
struct PerfSnapshot {
    // Callback timing (rolling window, reset each publish)
    double callbackAvgUs;      // mean callback duration in microseconds
    double callbackPeakUs;     // worst-case callback duration in window
    double cpuLoadPercent;     // callbackAvgUs / bufferDurationUs * 100

    // Cumulative counters (persist across windows, reset by resetCounters())
    int xrunCount;             // callbacks that exceeded xrun threshold
    int64_t callbackCount;     // total callbacks since prepare() (or last reset)

    // Per-slot timing (populated only when slot profiling is enabled)
    struct SlotPerf {
        int handle;            // Source or Bus handle (correlate via existing FFI)
        double avgUs;
        double peakUs;
    };
    std::vector<SlotPerf> slots;

    // System context
    double sampleRate;
    int blockSize;
    double bufferDurationUs;   // blockSize / sampleRate * 1e6
};
```

## Interface

### C++ (`squeeze::PerfMonitor`)

```cpp
namespace squeeze {

class PerfMonitor {
public:
    PerfMonitor();

    // --- Control thread ---

    void enable();
    void disable();
    bool isEnabled() const;

    void enableSlotProfiling();
    void disableSlotProfiling();
    bool isSlotProfilingEnabled() const;

    // Set the budget for xrun detection and CPU load calculation.
    // Called from Engine constructor (engine is always prepared from birth).
    void prepare(double sampleRate, int blockSize);

    // Return the latest snapshot. Returns a default (zeroed) snapshot if
    // disabled or if no data has been published yet.
    PerfSnapshot getSnapshot() const;

    // Reset cumulative counters (xrunCount, callbackCount).
    void resetCounters();

    // Set the xrun threshold as a fraction of the buffer budget.
    // Default is 1.0 (xrun when callback duration > bufferDurationUs).
    // A value of 0.9 means xrun is detected at 90% of budget (early warning).
    // Clamped to [0.1, 2.0].
    void setXrunThreshold(double factor);
    double getXrunThreshold() const;

    // --- Audio thread (all methods are RT-safe) ---

    // Call at the top of processBlock, before any work.
    void beginBlock();

    // Call at the bottom of processBlock, after all work.
    void endBlock();

    // Call around each Source/Bus process() call in processBlock.
    // No-op when slot profiling is disabled.
    // slotIndex: position in the iteration (0-based).
    // handle: the Source or Bus handle (for identification in the snapshot).
    void beginSlot(int slotIndex, int handle);
    void endSlot(int slotIndex);
};

} // namespace squeeze
```

### C ABI (`squeeze_ffi.h`)

```c
// --- Performance snapshot (value type, no heap allocation needed) ---
typedef struct {
    double callback_avg_us;
    double callback_peak_us;
    double cpu_load_percent;
    int    xrun_count;
    int64_t callback_count;
    double sample_rate;
    int    block_size;
    double buffer_duration_us;
} SqPerfSnapshot;

// --- Per-slot timing (heap-allocated list, caller must free) ---
typedef struct {
    int    handle;
    double avg_us;
    double peak_us;
} SqSlotPerf;

typedef struct {
    SqSlotPerf* items;
    int count;
} SqSlotPerfList;

// Snapshot — returns a value, no allocation
SqPerfSnapshot sq_perf_snapshot(SqEngine engine);

// Enable / disable
void sq_perf_enable(SqEngine engine, int enabled);
int  sq_perf_is_enabled(SqEngine engine);

// Slot profiling
void sq_perf_enable_slots(SqEngine engine, int enabled);
int  sq_perf_is_slot_profiling_enabled(SqEngine engine);

// Xrun threshold
void   sq_perf_set_xrun_threshold(SqEngine engine, double factor);
double sq_perf_get_xrun_threshold(SqEngine engine);

// Reset cumulative counters
void sq_perf_reset(SqEngine engine);

// Per-slot timing — caller must free with sq_free_slot_perf_list
SqSlotPerfList sq_perf_slots(SqEngine engine);
void sq_free_slot_perf_list(SqSlotPerfList list);
```

### Python (`Squeeze` methods)

```python
class Squeeze:
    def perf_enable(self, enabled: bool = True) -> None:
        """Enable or disable performance monitoring."""
        ...

    def perf_is_enabled(self) -> bool:
        """Return whether performance monitoring is enabled."""
        ...

    def perf_enable_slots(self, enabled: bool = True) -> None:
        """Enable or disable per-slot (source/bus) profiling."""
        ...

    def perf_is_slot_profiling_enabled(self) -> bool:
        """Return whether slot profiling is enabled."""
        ...

    def perf_set_xrun_threshold(self, factor: float) -> None:
        """Set xrun threshold as fraction of budget (default 1.0)."""
        ...

    def perf_get_xrun_threshold(self) -> float:
        """Return the current xrun threshold factor."""
        ...

    def perf_snapshot(self) -> dict:
        """Return the latest performance snapshot as a dict.

        Keys: callback_avg_us, callback_peak_us, cpu_load_percent,
              xrun_count, callback_count, sample_rate, block_size,
              buffer_duration_us.
        """
        ...

    def perf_slots(self) -> list[dict]:
        """Return per-slot timing as a list of dicts.

        Each dict has keys: handle, avg_us, peak_us.
        Empty list if slot profiling is disabled or no data yet.
        """
        ...

    def perf_reset(self) -> None:
        """Reset cumulative counters (xrun_count, callback_count)."""
        ...
```

## RT-Side Accumulation

The audio thread writes into fixed-size internal accumulators. No heap allocation occurs on the audio thread.

```
┌─────────────────────────────────────────────────┐
│ Audio Thread (writer)                           │
│                                                 │
│  beginBlock()     ─── record start time         │
│  for each source/bus:                           │
│    beginSlot(i, h) ─── record slot start        │
│    source/bus.process()                         │
│    endSlot(i)      ─── record slot end          │
│  endBlock()       ─── compute duration,         │
│                       update accumulators,       │
│                       detect xrun,              │
│                       maybe publish              │
└─────────────────────────────────────────────────┘
         │ publish (every ~100ms worth of callbacks)
         ▼
┌─────────────────────────────────────────────────┐
│ SeqLock (single published buffer)               │
│  [atomic sequence counter + RTPublishedData]    │
└─────────────────────────────────────────────────┘
         │ getSnapshot() reads consistent copy
         ▼
┌─────────────────────────────────────────────────┐
│ Control Thread (reader)                         │
└─────────────────────────────────────────────────┘
```

### Fixed-Size Accumulators

```cpp
static constexpr int kMaxSlots = 256;

struct RTAccumulator {
    // Callback-level
    double callbackSumUs = 0.0;
    double callbackPeakUs = 0.0;
    int windowCount = 0;            // callbacks in this window (for computing averages)

    // Per-slot
    struct SlotAcc {
        int handle = -1;
        double sumUs = 0.0;
        double peakUs = 0.0;
    };
    std::array<SlotAcc, kMaxSlots> slots;
    int slotCount = 0;
};
```

### RTPublishedData (seqlock buffer)

This is the fixed-size structure that sits behind the seqlock. The audio thread computes averages from the accumulator and writes **final values** here — the control thread copies it out as-is.

```cpp
struct RTPublishedData {
    // Callback-level (computed from accumulator at publish time)
    double callbackAvgUs = 0.0;     // callbackSumUs / windowCount
    double callbackPeakUs = 0.0;    // worst case in window
    double cpuLoadPercent = 0.0;    // callbackAvgUs / bufferDurationUs * 100

    // Per-slot (computed from accumulator at publish time)
    struct SlotData {
        int handle = -1;
        double avgUs = 0.0;         // sumUs / windowCount
        double peakUs = 0.0;        // worst case in window
    };
    std::array<SlotData, kMaxSlots> slots;
    int slotCount = 0;              // how many entries are valid

    // System context (set once in prepare(), never changes)
    double sampleRate = 0.0;
    int blockSize = 0;
    double bufferDurationUs = 0.0;
};
```

**Key properties:**

- Entirely fixed-size (`std::array`, no `std::vector`, no pointers) — the seqlock copies it with a plain `memcpy`-equivalent and no allocation on either side.
- Stores post-computed averages, not raw sums — the division happens on the audio thread during publish so the reader gets ready-to-use values.
- `slotCount` tells the reader how many entries in `slots` are valid. Entries beyond `slotCount` are stale/zeroed and must be ignored.
- Cumulative counters (`xrunCount`, `callbackCount`) are **not** in this struct — they are separate `std::atomic` members read independently by `getSnapshot()`. This avoids coupling their update frequency to the ~100ms publish window.
```

### Publish Strategy

After each `endBlock()`, check if the accumulation window has elapsed. The window length is `sampleRate / blockSize / 10` callbacks (~100ms at any sample rate / block size combination, minimum 1). When the window elapses:

1. Compute averages from sums and counts
2. Write the completed data to the published buffer behind the seqlock
3. Reset the accumulator for the next window

Cumulative counters (`xrunCount`, `callbackCount`) are separate atomics, not part of the window — they are always current.

### Topology Changes

When the Engine swaps a new MixerSnapshot (sources/buses added or removed), slot indices and handles change mid-window. The accumulator is **not** reset — stale data from the old topology is mixed with new data until the current window publishes (at most ~100ms). This means:

- A published `SlotData` entry may average timing from two different entities if a slot index was reused across a topology change within the same window.
- The `handle` stored in each `SlotData` entry reflects whichever entity last called `beginSlot` at that index — it is not guaranteed to be consistent with the averaged timing data for that window.

This is acceptable for telemetry. The next full window after a topology change will be clean. PerfMonitor has no `notifyTopologyChanged()` method — the added complexity and audio-thread coordination are not justified for a cosmetic issue in a diagnostic display that refreshes every ~100ms.

### SeqLock Protocol

The audio thread (writer) and control thread (reader) share one `RTPublishedData` buffer and an atomic sequence counter.

**Writer (audio thread, in `endBlock`):**
```
sequence.store(sequence + 1, release)   // odd = write in progress
write data to published buffer
sequence.store(sequence + 1, release)   // even = write complete
```

**Reader (control thread, in `getSnapshot`):**
```
do {
    s1 = sequence.load(acquire)
    if (s1 is odd) continue              // write in progress, retry
    copy data from published buffer
    s2 = sequence.load(acquire)
} while (s1 != s2)                       // data changed during read, retry
```

Wait-free for the writer, lock-free for the reader. The reader retries if a publish is in flight, but this is vanishingly rare at ~10Hz publish rate vs ~86Hz audio callback rate (at 44100/512). The reader never blocks the writer.

## Xrun Detection

In `endBlock()`, an xrun is detected when:

```
callbackDurationUs > bufferDurationUs * xrunThreshold_
```

Where `xrunThreshold_` defaults to 1.0 and is configurable via `setXrunThreshold()`.

`endBlock()` unconditionally increments the cumulative `callbackCount_` atomic (relaxed), then checks for an xrun. When an xrun is detected:

1. Increment the cumulative `xrunCount_` atomic
2. Log via `SQ_WARN_RT("xrun: %.0fus (budget %.0fus, threshold %.0f%%), total %d", durationUs, budgetUs, threshold * 100, count)`

The xrun threshold is stored as a `std::atomic<float>` with relaxed ordering — settable from the control thread, readable from the audio thread without synchronization beyond the atomic. `float` precision is more than sufficient for a [0.1, 2.0] threshold factor, and `std::atomic<float>` is guaranteed lock-free on all target platforms (unlike `std::atomic<double>` which is not guaranteed lock-free in C++17).

## Engine Integration

Engine calls PerfMonitor from `processBlock`:

```cpp
void Engine::processBlock(float** outputChannels, int numChannels, int numSamples)
{
    perfMonitor_.beginBlock();

    // ... drain commands, advance transport ...

    int slot = 0;
    for (each source in snapshot) {
        perfMonitor_.beginSlot(slot, source->getHandle());
        // ... source processing ...
        perfMonitor_.endSlot(slot);
        ++slot;
    }

    for (each bus in snapshot, dependency order) {
        perfMonitor_.beginSlot(slot, bus->getHandle());
        // ... bus processing ...
        perfMonitor_.endSlot(slot);
        ++slot;
    }

    // ... copy master to output ...

    perfMonitor_.endBlock();
}
```

`prepare()` is called in the Engine constructor, since Engine is always prepared from birth in v2.

## Invariants

- All audio-thread methods are RT-safe: no allocation, no blocking, no I/O
- `enable()` / `disable()` use a relaxed atomic; safe from any thread
- `enableSlotProfiling()` / `disableSlotProfiling()` use a relaxed atomic; safe from any thread
- `getSnapshot()` never blocks the audio thread (seqlock reader)
- Fixed-size arrays cap per-slot tracking at `kMaxSlots` (256)
- Slots beyond `kMaxSlots` are silently ignored (not tracked, not crashed)
- A disabled PerfMonitor adds near-zero overhead (one relaxed atomic load + branch per `beginBlock`)
- Xrun count and callback count are cumulative and only reset by explicit `resetCounters()` call
- `getSnapshot()` returns default (zeroed) values if no data has been published yet
- `prepare()` must be called before meaningful data is produced (but calling other methods before `prepare()` is safe — they are no-ops or return defaults)
- The xrun threshold is clamped to [0.1, 2.0] — values outside this range are clamped silently
- The publish window is always at least 1 callback, even at extreme sample rate / block size combinations
- `callbackCount` in the snapshot reflects the total since last `resetCounters()` (or since `prepare()` if never reset)

## Error Conditions

- `getSnapshot()` before `prepare()`: returns default snapshot with zero values
- `getSnapshot()` before any data published: returns default snapshot with zero values
- More than `kMaxSlots` slots: excess slots not tracked, no crash
- `beginSlot` / `endSlot` with `slotIndex >= kMaxSlots`: ignored
- `setXrunThreshold` with value outside [0.1, 2.0]: clamped silently
- `getSnapshot()` during a write: seqlock retries, returns previous consistent snapshot
- `sq_perf_snapshot` on a NULL engine: returns zeroed `SqPerfSnapshot`
- `sq_perf_slots` on a NULL engine: returns `{NULL, 0}`
- `sq_free_slot_perf_list` with NULL items: no-op

## Does NOT Handle

- MIDI queue health monitoring — MIDI device stats belong on MidiDeviceManager, not PerfMonitor (v1 had this but it was dead code; Engine bypassed it entirely)
- Per-processor timing within a Chain — PerfMonitor operates at the Source/Bus granularity, not individual insert processors
- Historical data / time series — only the latest ~100ms window is available
- Automatic xrun recovery or buffer size adjustment
- Memory allocation tracking
- Disk I/O or network latency monitoring
- Thread identification (assumes single audio thread)

## Dependencies

- `<chrono>` — `steady_clock` for timing
- `<atomic>` — sequence counter, enable flags, xrun threshold, cumulative counters
- `<array>` — fixed-size RT accumulators
- Logger — `SQ_WARN_RT` macro for xrun messages

No dependencies on other Squeeze components. PerfMonitor is a leaf in the dependency tree (tier 15, no dependencies). Engine owns it and calls into it, but PerfMonitor does not call back into Engine.

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| `enable()` / `disable()` / `isEnabled()` | Any | Relaxed atomic |
| `enableSlotProfiling()` / `disableSlotProfiling()` / `isSlotProfilingEnabled()` | Any | Relaxed atomic |
| `setXrunThreshold()` / `getXrunThreshold()` | Any | Relaxed atomic |
| `prepare()` | Control | Sets budget, resets accumulators |
| `getSnapshot()` | Control | Seqlock reader, never blocks writer |
| `resetCounters()` | Control | Stores to cumulative atomics. Races with audio-thread increments are intentional — a reset concurrent with `endBlock()` may lose one increment. This is acceptable for best-effort telemetry counters. |
| `beginBlock()` / `endBlock()` | Audio | RT-safe, writes accumulators and seqlock |
| `beginSlot()` / `endSlot()` | Audio | RT-safe, writes slot accumulators |

## Example Usage

### C++

```cpp
// Engine constructor
perfMonitor_.prepare(sampleRate, blockSize);

// Control thread — enable monitoring
engine.getPerfMonitor().enable();
engine.getPerfMonitor().enableSlotProfiling();

// Control thread — read stats
auto snap = engine.getPerfMonitor().getSnapshot();
printf("CPU: %.1f%%, avg: %.0fus, peak: %.0fus, xruns: %d\n",
       snap.cpuLoadPercent, snap.callbackAvgUs,
       snap.callbackPeakUs, snap.xrunCount);

for (auto& s : snap.slots) {
    printf("  slot handle=%d  avg=%.0fus  peak=%.0fus\n",
           s.handle, s.avgUs, s.peakUs);
}
```

### C ABI

```c
sq_perf_enable(engine, 1);
sq_perf_enable_slots(engine, 1);

// ... after some audio has been processed ...

SqPerfSnapshot snap = sq_perf_snapshot(engine);
printf("CPU: %.1f%%, xruns: %d/%lld callbacks\n",
       snap.cpu_load_percent, snap.xrun_count, snap.callback_count);

SqSlotPerfList slots = sq_perf_slots(engine);
for (int i = 0; i < slots.count; ++i) {
    printf("  handle=%d avg=%.0fus peak=%.0fus\n",
           slots.items[i].handle, slots.items[i].avg_us, slots.items[i].peak_us);
}
sq_free_slot_perf_list(slots);
```

### Python

```python
s = Squeeze()
s.perf_enable()
s.perf_enable_slots()

# ... after some audio has been processed ...

snap = s.perf_snapshot()
print(f"CPU: {snap['cpu_load_percent']:.1f}%, "
      f"xruns: {snap['xrun_count']}/{snap['callback_count']}")

for slot in s.perf_slots():
    print(f"  handle={slot['handle']} "
          f"avg={slot['avg_us']:.0f}us peak={slot['peak_us']:.0f}us")
```
