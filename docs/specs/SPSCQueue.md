# SPSCQueue Specification

## Responsibilities

- Provide a lock-free, wait-free single-producer single-consumer (SPSC) queue
- Support fixed-capacity, compile-time-sized storage (no heap allocation after construction)
- Guarantee RT-safety: `tryPush` and `tryPop` never allocate, never block, never loop unboundedly
- Serve as the fundamental communication primitive for all thread bridges (control → audio, MIDI → audio, audio → control)

## Overview

SPSCQueue is a header-only, template-based ring buffer with exactly one producer thread and one consumer thread. It uses `std::atomic` with acquire/release semantics for synchronization — no mutexes, no CAS loops. The capacity is a compile-time template parameter; storage is a `std::array` with one extra slot to distinguish full from empty.

This is a self-contained implementation (no external library). v1's SPSCQueue is 50 lines and has been stable across the entire project — v2 carries the same design forward with minor interface additions.

## Interface

### C++ (`squeeze::SPSCQueue`)

```cpp
namespace squeeze {

template<typename T, int Capacity>
class SPSCQueue {
    static_assert(Capacity > 0, "Capacity must be positive");

public:
    // Push an item. Returns true if successful, false if queue is full.
    // Producer thread only.
    bool tryPush(const T& item);

    // Pop an item. Returns true if successful, false if queue is empty.
    // Consumer thread only.
    bool tryPop(T& item);

    // Approximate number of items in the queue.
    // Safe to call from any thread (both reads are atomic).
    int size() const;

    // Whether the queue is empty. Safe from any thread.
    bool empty() const;

    // Reset the queue to empty state.
    // Only safe when neither producer nor consumer is active.
    void reset();

private:
    std::array<T, Capacity + 1> buffer_;   // one extra slot: full vs empty disambiguation
    std::atomic<int> readPos_{0};
    std::atomic<int> writePos_{0};

    int next(int pos) const { return (pos + 1) % (Capacity + 1); }
};

} // namespace squeeze
```

### Memory Ordering

- `tryPush`: relaxed load of `writePos_`, acquire load of `readPos_` (to see consumer progress), release store of `writePos_` (to publish the new item)
- `tryPop`: relaxed load of `readPos_`, acquire load of `writePos_` (to see producer progress), release store of `readPos_` (to publish the consumed slot)
- `size`: acquire loads of both positions (approximate — may be slightly stale)

No `memory_order_seq_cst` anywhere. Acquire/release is sufficient for SPSC.

## Invariants

- `Capacity` is a compile-time constant > 0
- The queue can hold at most `Capacity` items (not `Capacity + 1` — the extra slot is internal)
- `tryPush` on a full queue returns false and does not modify the queue
- `tryPop` on an empty queue returns false and does not modify the output parameter
- `size()` returns a value in [0, Capacity] — it may be transiently stale but never negative or > Capacity
- `empty()` is equivalent to `size() == 0` (same staleness caveat)
- `reset()` is only safe when no concurrent push/pop is in progress
- Items are delivered in FIFO order
- No heap allocation after construction
- No blocking, no spinning, no CAS loops — every operation is bounded O(1)

## Error Conditions

- `tryPush` when full: returns false (caller decides whether to drop, retry, or log)
- `tryPop` when empty: returns false
- Using more than one producer thread: undefined behavior
- Using more than one consumer thread: undefined behavior
- Calling `reset()` while producer or consumer is active: undefined behavior

## Does NOT Handle

- **Multiple producers or consumers** — strictly single-producer, single-consumer
- **Dynamic resizing** — capacity is fixed at compile time
- **Move-only types** — `tryPush` takes `const T&`; a `tryPush(T&&)` overload can be added if needed
- **Blocking waits** — no `push()` that blocks until space is available
- **Element destruction on overflow** — caller is responsible for handling full-queue scenarios

## Dependencies

- C++ standard library (`<array>`, `<atomic>`)
- No JUCE dependencies
- No other squeeze components

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| `tryPush()` | Producer (exactly one) | Lock-free, wait-free, O(1) |
| `tryPop()` | Consumer (exactly one) | Lock-free, wait-free, O(1) |
| `size()` | Any | Atomic reads, approximate result |
| `empty()` | Any | Atomic reads, approximate result |
| `reset()` | Setup only | Not safe during concurrent push/pop |

## Users

| Component | Producer | Consumer | Element Type | Capacity |
|-----------|----------|----------|-------------|----------|
| CommandQueue | Control thread | Audio thread | Command | 256 (typical) |
| MidiRouter (per-device) | MIDI callback thread | Audio thread | MidiEvent | 1024 |
| EventScheduler | Control thread | Audio thread | ScheduledEvent | 4096 (typical) |
| GarbageQueue | Audio thread | Control thread | unique_ptr | 64 (typical) |
| Logger | Any thread | Logger drain thread | LogEntry | 4096 |

Capacity values are typical defaults — each user chooses its own capacity as a template parameter.

## Example Usage

### Basic push/pop

```cpp
SPSCQueue<int, 128> queue;

// Producer thread
bool ok = queue.tryPush(42);
assert(ok);

// Consumer thread
int value;
ok = queue.tryPop(value);
assert(ok && value == 42);
```

### Audio thread command drain

```cpp
SPSCQueue<Command, 256> commandQueue;

// Control thread (producer)
commandQueue.tryPush(Command::SwapSnapshot{newSnapshot});

// Audio thread (consumer) — drain all pending commands
Command cmd;
while (commandQueue.tryPop(cmd)) {
    handleCommand(cmd);
}
```

### MIDI callback → audio thread

```cpp
SPSCQueue<MidiEvent, 1024> midiQueue;

// MIDI callback thread (producer)
MidiEvent event{data, size};
if (!midiQueue.tryPush(event)) {
    droppedCount_.fetch_add(1, std::memory_order_relaxed);
}

// Audio thread (consumer)
MidiEvent event;
while (midiQueue.tryPop(event)) {
    // dispatch to node MidiBuffer
}
```
