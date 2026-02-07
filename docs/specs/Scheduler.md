# Scheduler Specification

## Overview

Scheduler safely passes commands from the control thread to the audio thread and garbage from the audio thread back to the control thread, using lock-free SPSC queues. The audio thread never allocates or frees — it only reads commands and enqueues used-up pointers for the control thread to delete later.

## Responsibilities

- Provide a lock-free command queue (control → audio)
- Provide a lock-free garbage queue (audio → control)
- Process pending commands on the audio thread
- Collect and destroy garbage on the control thread

## SPSCQueue

A generic, fixed-capacity, lock-free single-producer single-consumer ring buffer. Used internally by Scheduler.

```cpp
template<typename T, int Capacity>
class SPSCQueue {
public:
    bool tryPush(const T& item);  // returns false if full
    bool tryPop(T& item);         // returns false if empty
    int size() const;             // approximate count
};
```

## Command

A small, trivially-copyable struct representing work for the audio thread. No heap allocation, no std::string, no std::function.

```cpp
struct Command {
    enum class Type { swapGraph, setParameter };
    Type type;
    void* ptr = nullptr;       // swapGraph: new snapshot (ownership transferred)
    Node* node = nullptr;      // setParameter: target node
    int paramIndex = 0;        // setParameter: which parameter
    float paramValue = 0.0f;   // setParameter: new value
};
```

## GarbageItem

A type-erased pointer with a deleter, for safe deferred destruction on the control thread.

```cpp
struct GarbageItem {
    void* ptr = nullptr;
    void (*deleter)(void*) = nullptr;

    template<typename T>
    static GarbageItem wrap(T* p);
};
```

## Scheduler Interface

```cpp
class Scheduler {
public:
    // Control thread: queue a command for the audio thread
    bool sendCommand(const Command& cmd);

    // Audio thread: process all pending commands via callback
    // Handler signature: void(const Command&)
    // Returns the number of commands processed
    template<typename Handler>
    int processPending(Handler&& handler);

    // Audio thread: queue an item for deferred deletion
    bool sendGarbage(GarbageItem item);

    // Control thread: destroy all queued garbage
    void collectGarbage();
};
```

## Invariants

- Both queues are lock-free: no mutexes, no blocking
- Commands are processed in FIFO order
- `sendCommand()` is only called from the control thread
- `processPending()` is only called from the audio thread
- `sendGarbage()` is only called from the audio thread
- `collectGarbage()` is only called from the control thread
- The audio thread never calls `delete` or `free` — it uses `sendGarbage()` instead
- Queue capacity is fixed at construction time (template parameter)

## Error Conditions

- `sendCommand()` returns false if the command queue is full (command dropped)
- `sendGarbage()` returns false if the garbage queue is full (item not enqueued — caller must retry or handle)
- `processPending()` with no pending commands is a no-op (returns 0)
- `collectGarbage()` with no garbage is a no-op

## Does NOT Handle

- What commands mean (Engine interprets them)
- Graph topology or node management
- Buffer allocation
- Scheduling at specific times

## Dependencies

- Node (pointer in Command, not owned)
- Standard library atomics

## Thread Safety

- `sendCommand()` and `collectGarbage()`: control thread only. With multiple control threads (REPL, OSC, WebSocket), Engine's `controlMutex_` ensures only one thread calls `sendCommand()` at a time, maintaining the SPSC single-producer invariant. See [ConcurrencyModel](ConcurrencyModel.md).
- `processPending()` and `sendGarbage()`: audio thread only
- The SPSC queues enforce single-producer / single-consumer by design
