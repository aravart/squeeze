# Audio Thread as Publisher, Not Broadcaster

## The principle

The audio thread should never broadcast, notify, or signal other threads. It should only publish state that it was going to compute anyway, into locations that other threads can read without the audio thread's cooperation.

The audio thread is a **publisher**, never a **communicator**. It doesn't know or care if anyone reads its state. The publication mechanism (SeqLock, atomic store, lock-free ring buffer) has zero cost to the writer whether there are zero readers or fifty.

The moment the audio thread has to *notify* anyone — condition variable, callback, semaphore, even a CAS retry loop — you've introduced a latency path that depends on another thread's scheduling. That's the line you never cross.

## Practical example: transport / perf data

The audio thread updates a struct (position, playing, BPM, perf counters, whatever) via SeqLock or a single atomic snapshot. The UI or Lua thread polls it at whatever rate it wants (16ms for UI, 100ms for a Lua callback). If it catches a torn read, it retries — that's the *reader's* problem, not the audio thread's.

## Applying this to PerfMonitor

The current `PerfMonitor` design uses a centralized `kMaxNodes = 256` fixed array. The audio thread calls `beginNode(slotIndex, nodeId)` / `endNode(slotIndex)`, mapping into a parallel array. This works but raises the question: why have a centralized array at all?

The audio thread already touches each node when it calls `process()`. It could write timing data directly onto the node (or its slot in the `GraphSnapshot`), and readers would pull it from there. No parallel array, no arbitrary cap, no slot-index mapping.

### What per-node storage would look like

```cpp
// On Node or GraphSnapshot::NodeSlot
struct NodePerfData {
    double avgUs = 0.0;
    double peakUs = 0.0;
};
```

Audio thread writes after processing each node. Readers iterate nodes and read. Use a per-node SeqLock if you care about tearing, or just accept that a torn read of two doubles is cosmetically wrong for one poll cycle — it's telemetry, not audio data.

### What the centralized monitor is actually buying us

1. **Windowed averaging** — The `RTAccumulator` collects ~100ms of samples before publishing. Per-node storage would give you only last-callback timing, not an average. The accumulator logic needs to live somewhere.

2. **Aggregation** — Callback-level stats (total CPU load, xrun count) don't belong to any one node. That part genuinely needs a central place.

3. **Opt-in toggling** — `enableNodeProfiling()` gates the `std::chrono` calls. A global flag is still needed to avoid measurement overhead when nobody's watching.

### What's wrong with the current design

The `kMaxNodes = 256` fixed array is a design smell. The graph snapshot already knows how many nodes it has. Timing data should live alongside the slots, not in a parallel array that might disagree about count or ordering. The audio thread does `beginNode(slotIndex, nodeId)` — mapping slot indices into a separate fixed array — when it could just write to the slot it already has a pointer to.

### Why it was built this way

PerfMonitor was designed as a self-contained component following the CLAUDE.md workflow: spec in isolation, test in isolation, implement in isolation. Putting perf data on nodes or GraphSnapshot would have required modifying components that were already "done." It's a pragmatic shortcut, not an architectural ideal.

### If we revisit this

Move per-node timing into `GraphSnapshot::NodeSlot`. Keep PerfMonitor for the things that are genuinely global: callback timing, CPU load %, xrun count, MIDI queue stats. The node-level profiling becomes just "read the slots" — no fixed array, no cap, no slot-index indirection.

## Voice retrigger and the same principle

This publish-not-broadcast pattern extends beyond perf data. Any state the audio thread computes — voice activity, envelope levels, meter values, transport position — should follow the same model: the audio thread writes it as a side effect of work it was already doing, into storage that readers can poll without synchronization on the writer's side.
