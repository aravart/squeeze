# MidiRouter Specification

## Responsibilities

- Maintain a routing table mapping MIDI source devices to destination Sources with channel and note range filters
- Own per-device SPSC queues (MIDI callback thread → audio thread)
- Dispatch incoming MIDI messages to destination Source MidiBuffers on the audio thread
- Track per-device queue fill levels and dropped message counts
- Support CC → parameter mapping (device + CC number → processor handle + param name)

## Overview

MidiRouter is the centralized MIDI routing system. Instead of representing MIDI routing as graph connections (v1), MidiRouter distributes incoming messages directly to destination Sources' MidiBuffers at the top of each audio callback. Routing is configured via a routing table using Source MIDI assignments, independent of the audio structure.

MidiRouter does NOT own JUCE device connections — that is MidiDeviceManager's job. MidiRouter provides the queues and routing; MidiDeviceManager provides the hardware interface. MidiRouter can function without MidiDeviceManager (e.g., programmatic MIDI from EventScheduler goes directly to Source MidiBuffers, bypassing MidiRouter entirely).

## Interface

### C++ (`squeeze::MidiRouter`)

```cpp
namespace squeeze {

struct MidiEvent {
    uint8_t data[3];
    int size;  // 1, 2, or 3 bytes
};

struct MidiRoute {
    int id;                 // unique route ID
    std::string deviceName; // source device
    Source* source;         // destination source
    int channelFilter;      // 0 = all channels, 1-16 = specific channel
    int noteLow;            // note range low (0-127)
    int noteHigh;           // note range high (0-127)
};

struct CCMapping {
    int id;
    std::string deviceName;
    int ccNumber;
    int procHandle;         // target processor handle
    std::string paramName;  // target parameter name
};

// Opaque handle to a per-device SPSC queue.
// Returned by createDeviceQueue(), cached by MidiDeviceManager,
// passed to pushMidiEvent() on the MIDI callback thread.
// This avoids any map lookup or string operation on the hot path.
struct MidiDeviceQueue;

class MidiRouter {
public:
    MidiRouter();
    ~MidiRouter();

    MidiRouter(const MidiRouter&) = delete;
    MidiRouter& operator=(const MidiRouter&) = delete;

    // --- Device queue management (control thread) ---
    MidiDeviceQueue* createDeviceQueue(const std::string& deviceName, std::string& error);
    void removeDeviceQueue(const std::string& deviceName);
    bool hasDeviceQueue(const std::string& deviceName) const;

    // --- Routing (control thread) ---
    int addRoute(const std::string& deviceName, Source* source,
                 int channelFilter, int noteLow, int noteHigh,
                 std::string& error);
    bool removeRoute(int routeId);
    bool removeRoutesForSource(Source* source);
    bool removeRoutesForDevice(const std::string& deviceName);
    std::vector<MidiRoute> getRoutes() const;

    // --- CC mapping (control thread) ---
    int addCCMapping(const std::string& deviceName, int ccNumber,
                     int procHandle, const std::string& paramName,
                     std::string& error);
    bool removeCCMapping(int mappingId);
    std::vector<CCMapping> getCCMappings() const;

    // Publish updated routing table to audio thread
    void commit();

    // --- MIDI input (MIDI callback thread) ---
    // Caller passes the opaque MidiDeviceQueue* obtained from createDeviceQueue().
    // No map lookup, no string ops — just an SPSC push.
    bool pushMidiEvent(MidiDeviceQueue* queue, const MidiEvent& event);

    // --- Audio thread ---
    void dispatch(const std::unordered_map<Source*, juce::MidiBuffer*>& sourceBuffers,
                  int numSamples);

    // --- Monitoring (control thread) ---
    int getQueueFillLevel(const std::string& deviceName) const;
    int getDroppedCount(const std::string& deviceName) const;
    void resetDroppedCounts();

private:
    struct MidiDeviceQueueImpl {
        std::string name;                       // for diagnostics only
        SPSCQueue<MidiEvent, 1024> queue;
        std::atomic<int> droppedCount{0};
    };
    // MidiDeviceQueue is an alias for the impl — opaque to callers.
    static_assert(true, "MidiDeviceQueue = MidiDeviceQueueImpl (opaque to callers)");

    std::unordered_map<std::string, std::unique_ptr<MidiDeviceQueueImpl>> devices_;

    std::vector<MidiRoute> stagedRoutes_;
    std::vector<CCMapping> stagedCCMappings_;
    int nextRouteId_ = 1;
    int nextCCMappingId_ = 1;

    struct RoutingTable {
        std::vector<MidiRoute> routes;
        std::vector<CCMapping> ccMappings;
        std::vector<MidiDeviceQueueImpl*> deviceQueues;  // snapshot for dispatch()
    };
    std::atomic<RoutingTable*> activeTable_{nullptr};
    RoutingTable* pendingGarbage_ = nullptr;
};

} // namespace squeeze
```

## Routing Table

The routing table maps MIDI device input to Source destinations. Routes are derived from Source MidiAssignments:

- When `sq_midi_assign(engine, source, device, channel)` is called, the Engine translates this into a MidiRouter route entry
- When `sq_midi_note_range(engine, source, low, high)` is called, the route's note range is updated

Multiple routes can share the same device (fan-out) or the same destination source (fan-in). Channel and note range filtering are per-route.

Example:
```
Device "Keylab"  → Source "Lead",  channel 1, notes 0-127
Device "MPD"     → Source "Kick",  channel 10, notes 36-36
Device "MPD"     → Source "Snare", channel 10, notes 38-38
Device "MPD"     → Source "Hat",   channel 10, notes 42-42
```

### CC Mapping

CC messages can be mapped to processor parameters:

```python
engine.midi_cc_map(device="Keylab", cc=74, target=filter_plugin, param="cutoff")
```

When a CC message matches a mapping, the Engine calls `setParameter()` on the target processor with the CC value scaled to the parameter range.

### Commit Model

Control-thread changes to routes (add/remove) are staged. `commit()` builds a new `RoutingTable` containing the staged routes, CC mappings, and a snapshot of all current `MidiDeviceQueueImpl*` pointers (from `devices_`). It then publishes the table to the audio thread via an atomic pointer swap. Engine calls `commit()` after any routing or device queue change.

Because `dispatch()` only accesses device queues through the `RoutingTable` snapshot, and `pushMidiEvent()` takes a pre-resolved `MidiDeviceQueue*`, neither the audio thread nor the MIDI callback thread ever touches `devices_`. Only the control thread reads/writes `devices_`.

## Audio Thread Dispatch

`dispatch()` runs once per audio callback, before source processing. It reads only the atomically-published `RoutingTable` — it never touches `devices_`.

```
table = activeTable_.load(acquire)
if table == nullptr: return

// Drain all device queues listed in the routing table snapshot.
// These pointers were captured by commit() — no map lookup needed.
for each queue in table->deviceQueues:
    drain queue->queue into a temporary MidiBuffer (per-device)

for each route in table->routes:
    for each message in the route's device buffer:
        if channel matches (channelFilter == 0 or message.channel == channelFilter)
           and note matches (within noteLow..noteHigh range):
            append message to sourceBuffers[route.source] at samplePosition 0

for each CC mapping in table->ccMappings:
    for each CC message in the mapping's device buffer:
        if CC number matches:
            // Engine applies parameter change
```

All destination MidiBuffers must be cleared before calling `dispatch()`.

### Note Range Filtering

Unlike the old MidiRouter's single-note filter, the new router supports note ranges:

- `noteLow=0, noteHigh=127` — all notes (default)
- `noteLow=36, noteHigh=36` — single note (kick drum)
- `noteLow=60, noteHigh=72` — one octave range

Note range filtering applies only to note-on, note-off, and polyphonic aftertouch messages. Other message types (CC, pitch bend, channel pressure) pass through regardless of note range.

## Invariants

- A route can only reference a device that has a queue
- Multiple routes may target the same source (fan-in)
- Multiple routes may source from the same device (fan-out with different filters)
- `dispatch()` is RT-safe: no allocation, no blocking, no map lookups — only iterates arrays and drains SPSC queues from the atomically-published `RoutingTable`
- `commit()` is O(N) where N is route count + device count (bounded, expected < 100)
- `commit()` must be called after `createDeviceQueue()` / `removeDeviceQueue()` so `dispatch()` sees the updated device list
- Removing a route does not remove the device queue
- Removing a device queue removes all its routes
- Route IDs are monotonically increasing, never reused
- `pushMidiEvent()` is lock-free and wait-free — it receives a pre-resolved `MidiDeviceQueue*` and performs only an SPSC push and an atomic increment (on overflow)
- The `devices_` map is only accessed from the control thread (under `controlMutex_`). Neither the MIDI callback thread nor the audio thread ever touches it.
- A `MidiDeviceQueue*` returned by `createDeviceQueue()` remains valid until `removeDeviceQueue()` is called for that device. The caller (MidiDeviceManager) must stop the MIDI input and discard the pointer before calling `removeDeviceQueue()`.
- SysEx messages (> 3 bytes) are rejected by `pushMidiEvent()`

## Error Conditions

| Operation | Error | Behavior |
|-----------|-------|----------|
| `createDeviceQueue()` for existing device | Already exists | Returns existing `MidiDeviceQueue*` (idempotent) |
| `removeDeviceQueue()` for unknown device | Not found | No-op |
| `addRoute()` for device with no queue | Device not registered | Returns -1, sets error |
| `addRoute()` with invalid channel (< 0 or > 16) | Invalid filter | Returns -1, sets error |
| `addRoute()` with invalid note range | Invalid filter | Returns -1, sets error |
| `removeRoute()` with invalid ID | Route not found | Returns false |
| `pushMidiEvent()` when queue is full | Queue overflow | Returns false, increments dropped count (atomic) |
| `pushMidiEvent()` with null queue | Invalid handle | Returns false |
| `dispatch()` with no active routing table | No routes committed | No-op |

## Does NOT Handle

- **JUCE device connections** — MidiDeviceManager opens/closes hardware
- **SysEx messages** — MidiEvent is 3 bytes max
- **Timestamp-based sample-accurate dispatch** — all messages get samplePosition=0 (future)
- **MIDI output** — future
- **MIDI learn / auto-mapping** — future
- **Device enumeration** — MidiDeviceManager queries JUCE

## Dependencies

- SPSCQueue (lock-free queue for MIDI callback → audio thread)
- Source (routing target — forward declaration sufficient)
- JUCE (`juce_audio_basics`: MidiBuffer, MidiMessage for dispatch)

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| `createDeviceQueue()` / `removeDeviceQueue()` | Control | Under Engine's controlMutex_. Only thread that touches `devices_`. |
| `addRoute()` / `removeRoute()` / `removeRoutesFor*()` | Control | Stages changes |
| `addCCMapping()` / `removeCCMapping()` | Control | Stages changes |
| `commit()` | Control | Snapshots device queue pointers + routes, atomic pointer swap |
| `getRoutes()` / `getCCMappings()` | Control | Reads staged data |
| `pushMidiEvent()` | MIDI callback | Lock-free SPSC push via pre-resolved `MidiDeviceQueue*`. No map lookup, no string ops. |
| `dispatch()` | Audio | Loads atomic `RoutingTable*`, iterates device queue array + route array. No map lookup. |
| `getQueueFillLevel()` / `getDroppedCount()` | Control | Looks up `devices_` by name — safe under controlMutex_ |
| `resetDroppedCounts()` | Control | Iterates `devices_` — safe under controlMutex_ |

## Integration with Engine

Engine owns `MidiRouter midiRouter_` as a private member:

- **`processBlock()`**: calls `midiRouter_.dispatch(sourceBuffers, numSamples)` before source processing
- **`removeSource()`**: calls `midiRouter_.removeRoutesForSource(source)` to clean up routes
- **`midiAssign()`**: translates Source MidiAssignment into a MidiRouter route
- **`midiCCMap()`**: adds a CC mapping to MidiRouter

## C ABI

MidiRouter is not directly exposed through the C ABI. Its functionality is accessed through Engine-level functions:

```c
// MIDI assignment (translates to MidiRouter routes)
void sq_midi_assign(SqEngine engine, SqSource src, const char* device, int channel);
void sq_midi_note_range(SqEngine engine, SqSource src, int low, int high);

// CC mapping
void sq_midi_cc_map(SqEngine engine, SqProc proc, const char* param, int cc_number);
```

And through MidiDeviceManager's FFI functions for device management:

```c
SqStringList sq_midi_devices(SqEngine engine);
bool sq_midi_open(SqEngine engine, const char* device_name, char** error);
void sq_midi_close(SqEngine engine, const char* device_name);
SqStringList sq_midi_open_devices(SqEngine engine);
```

## Example Usage

### Control thread setup

```cpp
MidiRouter router;

// createDeviceQueue returns an opaque MidiDeviceQueue* handle.
// MidiDeviceManager caches this at device-open time.
MidiDeviceQueue* keylabQ = router.createDeviceQueue("Keylab", error);
MidiDeviceQueue* mpdQ    = router.createDeviceQueue("MPD", error);

// Routes from Source MIDI assignments
router.addRoute("Keylab", synthSource, 1, 0, 127, error);
router.addRoute("MPD", kickSource, 10, 36, 36, error);
router.addRoute("MPD", snareSource, 10, 38, 38, error);

// CC mapping
router.addCCMapping("Keylab", 74, filterProc->getHandle(), "cutoff", error);

// commit() snapshots device queue pointers + routes for the audio thread
router.commit();
```

### MIDI callback thread (via MidiDeviceManager)

```cpp
// MidiDeviceManager::handleIncomingMidiMessage — called by JUCE on a system thread.
// Uses the cached MidiDeviceQueue* — no map lookup, no string allocation.
void MidiDeviceManager::handleIncomingMidiMessage(juce::MidiInput* input,
                                                   const juce::MidiMessage& msg) {
    MidiDeviceQueue* queue = cachedQueues_[input];  // pre-resolved at open time
    MidiEvent event;
    event.size = msg.getRawDataSize();
    std::memcpy(event.data, msg.getRawData(), event.size);
    router_.pushMidiEvent(queue, event);
}
```

### Audio thread dispatch

```cpp
// In Engine::processBlock(), before source processing.
// dispatch() reads the atomic RoutingTable* — never touches devices_.
midiRouter_.dispatch(snapshot->sourceBuffers, numSamples);

for (auto& entry : snapshot->sources) {
    entry.source->process(entry.buffer, entry.midiBuffer);
}
```
