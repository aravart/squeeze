# CommandQueue Specification

## Responsibilities
- Provide a lock-free SPSC command queue (control thread → audio thread)
- Provide a lock-free SPSC garbage queue (audio thread → control thread)
- Process pending commands on the audio thread via caller-supplied handler
- Collect and destroy garbage on the control thread
- Carry infrastructure commands only — not musical events (EventScheduler) or MIDI (MidiRouter)

## Overview

CommandQueue is the lock-free bridge for infrastructure commands from the control thread to the audio thread. It owns two SPSC queues: one for commands going in, one for garbage (used-up pointers) coming back. The audio thread never allocates or frees — it reads commands and pushes spent pointers to the garbage queue for the control thread to delete later.

CommandQueue is a pure transport layer. It does not interpret commands — the Engine provides a handler callback that gives commands their meaning. This keeps CommandQueue decoupled from Engine internals.

### What flows through CommandQueue

| Command | Payload | When |
|---------|---------|------|
| `swapSnapshot` | `MixerSnapshot*` | After any structural or routing change |
| `transportPlay` | — | User hits play |
| `transportStop` | — | User hits stop |
| `transportPause` | — | User hits pause |
| `setTempo` | `double bpm` | Tempo change |
| `setTimeSignature` | `int numerator, int denominator` | Time signature change |
| `seekSamples` | `int64_t samples` | Seek to sample position |
| `seekBeats` | `double beats` | Seek to beat position |
| `setLoopPoints` | `double startBeats, double endBeats` | Loop region change |
| `setLooping` | `bool enabled` | Loop enable/disable |

### What does NOT flow through CommandQueue

- **Parameter changes (immediate):** `setParameter()` is called directly on the control thread. Processors use atomic storage internally; the audio thread reads updated values on the next `process()` call.
- **Parameter changes (beat-synced):** Routed through EventScheduler.
- **MIDI device input:** Routed through MidiRouter's own SPSC queues.
- **Musical events (noteOn, noteOff, CC):** Routed through EventScheduler.

## Interface

```cpp
namespace squeeze {

struct Command {
    enum class Type {
        swapSnapshot,
        transportPlay,
        transportStop,
        transportPause,
        setTempo,
        setTimeSignature,
        seekSamples,
        seekBeats,
        setLoopPoints,
        setLooping
    };

    Type type;

    // Payload — which fields are valid depends on `type`
    void* ptr = nullptr;          // swapSnapshot: MixerSnapshot* (ownership transferred)
    double doubleValue1 = 0.0;    // setTempo: bpm, seekBeats: beats, setLoopPoints: startBeats
    double doubleValue2 = 0.0;    // setLoopPoints: endBeats
    int64_t int64Value = 0;       // seekSamples: sample position
    int intValue1 = 0;            // setTimeSignature: numerator, setLooping: 1=on/0=off
    int intValue2 = 0;            // setTimeSignature: denominator
};

struct GarbageItem {
    void* ptr = nullptr;
    void (*deleter)(void*) = nullptr;

    void destroy();

    template<typename T>
    static GarbageItem wrap(T* p);
};

class CommandQueue {
public:
    CommandQueue() = default;
    ~CommandQueue() = default;

    CommandQueue(const CommandQueue&) = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;

    // --- Control thread ---
    bool sendCommand(const Command& cmd);

    // --- Audio thread ---
    template<typename Handler>
    int processPending(Handler&& handler);

    bool sendGarbage(const GarbageItem& item);

    // --- Control thread ---
    int collectGarbage();

private:
    static constexpr int kCommandCapacity = 256;
    static constexpr int kGarbageCapacity = 256;

    SPSCQueue<Command, kCommandCapacity> commandQueue_;
    SPSCQueue<GarbageItem, kGarbageCapacity> garbageQueue_;
};

} // namespace squeeze
```

## Command

`Command` is a trivially copyable POD struct sized to fit in a cache line. All fields are plain types — no `std::string`, no `std::function`, no heap allocation.

The `type` field determines which payload fields are valid. This is a deliberate trade-off: a flat struct is trivially copyable, RT-safe, and cache-friendly.

### `swapSnapshot`

Transfers ownership of a heap-allocated `MixerSnapshot*` from the control thread to the audio thread. The audio thread installs the new snapshot and pushes the old one to the garbage queue via `sendGarbage(GarbageItem::wrap(oldSnapshot))`.

## GarbageItem

Type-erasing wrapper for deferred deletion. Stores a `void*` and a plain function pointer — no `std::function`, no heap allocation.

### `GarbageItem::wrap<T>(T* p)`

```cpp
template<typename T>
GarbageItem GarbageItem::wrap(T* p) {
    return {p, [](void* raw) { delete static_cast<T*>(raw); }};
}
```

### `GarbageItem::destroy()`

Calls `deleter(ptr)` if both are non-null, then sets `ptr = nullptr`. Safe to call multiple times.

## Control Thread API

### `sendCommand(cmd)`

Pushes a command onto the command queue.
- Returns `true` on success
- Returns `false` if the queue is full — the command is **dropped**
- The caller is responsible for handling the failure
- Logs at warn level on failure

### `collectGarbage()`

Drains the garbage queue and calls `destroy()` on each item. Returns the number of items destroyed.

Engine calls `collectGarbage()` at the top of every control-thread operation that acquires `controlMutex_`.

## Audio Thread API

### `processPending(handler)`

Drains the command queue and calls `handler(cmd)` for each command in FIFO order. Returns the number of commands processed. RT-safe.

### `sendGarbage(item)`

Pushes a garbage item onto the garbage queue. Returns `true` on success, `false` if full (item leaked — preferable to blocking on the audio thread).

## Invariants

- Both queues are lock-free: no mutexes, no blocking, no allocation
- Commands are processed in FIFO order
- `sendCommand()` is called only from the control thread (single producer)
- `processPending()` is called only from the audio thread (single consumer)
- `sendGarbage()` is called only from the audio thread (single producer)
- `collectGarbage()` is called only from the control thread (single consumer)
- The audio thread never calls `delete` or `free`
- Queue capacities are fixed at compile time
- `Command` is trivially copyable
- Engine's `controlMutex_` ensures the SPSC single-producer invariant for `sendCommand()`

## Error Conditions

- `sendCommand()` with full queue: returns `false`, command dropped, logged at warn
- `sendGarbage()` with full queue: returns `false`, item leaked, logged at warn (RT-safe)
- `processPending()` with no pending commands: returns `0`
- `collectGarbage()` with no garbage: returns `0`
- `GarbageItem::destroy()` with null ptr or deleter: no-op

## Does NOT Handle

- What commands mean (Engine interprets them via the handler callback)
- Source/Bus/Chain topology or management (Engine)
- Parameter changes (direct or via EventScheduler)
- Musical events or beat-timed scheduling (EventScheduler)
- MIDI device input (MidiRouter)
- Deciding when to collect garbage (Engine calls `collectGarbage()`)

## Dependencies

- SPSCQueue (lock-free ring buffer)

No dependency on Processor, Source, Bus, or Engine.

## Thread Safety

| Method | Thread | Serialization |
|--------|--------|---------------|
| `sendCommand()` | Control | Engine's `controlMutex_` ensures single producer |
| `collectGarbage()` | Control | Engine's `controlMutex_` (or any single consumer) |
| `processPending()` | Audio | Single consumer by design |
| `sendGarbage()` | Audio | Single producer by design |

## C ABI

CommandQueue has no direct C ABI surface. It is an internal Engine component. Commands are sent implicitly through Engine-level C functions:

```c
void sq_transport_play(SqEngine engine);    // internally sends transportPlay command
void sq_transport_stop(SqEngine engine);
void sq_transport_set_tempo(SqEngine engine, double bpm);

// Structural changes trigger swapSnapshot internally
SqProc sq_source_append(SqEngine engine, SqSource src, const char* plugin_path);
void   sq_route(SqEngine engine, SqSource src, SqBus bus);
```

## Example Usage

```cpp
// Engine sends a snapshot swap
Command cmd;
cmd.type = Command::Type::swapSnapshot;
cmd.ptr = newSnapshot;  // ownership transferred
if (!commandQueue_.sendCommand(cmd)) {
    SQ_WARN("CommandQueue full, dropping snapshot");
    delete newSnapshot;
}

// Audio thread processes commands
commandQueue_.processPending([this](const Command& cmd) {
    switch (cmd.type) {
        case Command::Type::swapSnapshot: {
            auto* old = activeSnapshot_;
            activeSnapshot_ = static_cast<MixerSnapshot*>(cmd.ptr);
            if (old)
                commandQueue_.sendGarbage(GarbageItem::wrap(old));
            break;
        }
        case Command::Type::transportPlay:
            transport_.play();
            break;
        case Command::Type::transportStop:
            transport_.stop();
            eventScheduler_.clear();
            break;
        case Command::Type::setTempo:
            transport_.setTempo(cmd.doubleValue1);
            break;
    }
});

// Control thread drains garbage
commandQueue_.collectGarbage();
```
