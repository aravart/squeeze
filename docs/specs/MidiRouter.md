# MidiRouter Specification

## Responsibilities

- Maintain a routing table mapping MIDI source devices to destination nodes with channel and note filters
- Own per-device SPSC queues (MIDI callback thread → audio thread)
- Dispatch incoming MIDI messages to destination node MidiBuffers on the audio thread
- Track per-device queue fill levels and dropped message counts
- Support route add/remove without rebuilding the audio graph

## Overview

MidiRouter is the centralized MIDI routing system. Instead of representing each MIDI device as a node in the audio graph (v1's MidiInputNode pattern), MidiRouter owns all per-device SPSC queues and distributes incoming messages directly to destination nodes' MidiBuffers at the top of each audio callback. MIDI routing is configured via a routing table, independent of the audio graph topology.

MidiRouter does NOT own JUCE device connections — that is MidiDeviceManager's job (tier 11). MidiRouter provides the queues and routing; MidiDeviceManager provides the hardware interface. MidiRouter can function without MidiDeviceManager (e.g., programmatic MIDI from EventScheduler goes directly to node MidiBuffers, bypassing MidiRouter entirely).

## Interface

### C++ (`squeeze::MidiRouter`)

```cpp
namespace squeeze {

struct MidiEvent {
    uint8_t data[3];
    int size;  // 1, 2, or 3 bytes
};

struct MidiRoute {
    int id;                 // unique route ID (monotonically increasing, never reused)
    std::string deviceName; // source device
    int nodeId;             // destination node
    int channelFilter;      // 0 = all channels, 1-16 = specific channel
    int noteFilter;         // -1 = all notes, 0-127 = specific note only
};

class MidiRouter {
public:
    MidiRouter();
    ~MidiRouter();

    // Non-copyable, non-movable
    MidiRouter(const MidiRouter&) = delete;
    MidiRouter& operator=(const MidiRouter&) = delete;

    // --- Device queue management (control thread) ---
    bool createDeviceQueue(const std::string& deviceName, std::string& error);
    void removeDeviceQueue(const std::string& deviceName);
    bool hasDeviceQueue(const std::string& deviceName) const;

    // --- Routing (control thread) ---
    int addRoute(const std::string& deviceName, int nodeId,
                 int channelFilter, int noteFilter, std::string& error);
    bool removeRoute(int routeId);
    bool removeRoutesForNode(int nodeId);
    bool removeRoutesForDevice(const std::string& deviceName);
    std::vector<MidiRoute> getRoutes() const;

    // Publish updated routing table to audio thread
    void commit();

    // --- MIDI input (MIDI callback thread — called by MidiDeviceManager) ---
    bool pushMidiEvent(const std::string& deviceName, const MidiEvent& event);

    // --- Audio thread ---
    // Drain all device queues and dispatch to destination node MidiBuffers.
    // Called at the top of processBlock, before any node processing.
    void dispatch(const std::unordered_map<int, juce::MidiBuffer*>& nodeBuffers,
                  int numSamples);

    // --- Monitoring (any thread, atomic reads) ---
    int getQueueFillLevel(const std::string& deviceName) const;
    int getDroppedCount(const std::string& deviceName) const;
    void resetDroppedCounts();

private:
    struct DeviceState {
        SPSCQueue<MidiEvent, 1024> queue;
        std::atomic<int> droppedCount{0};
    };
    std::unordered_map<std::string, std::unique_ptr<DeviceState>> devices_;

    // Routing table (control thread staging → atomic swap to audio thread)
    std::vector<MidiRoute> stagedRoutes_;
    int nextRouteId_ = 1;

    // Active routing table (audio thread reads via atomic pointer)
    struct RoutingTable {
        std::vector<MidiRoute> routes;
    };
    std::atomic<RoutingTable*> activeTable_{nullptr};
    RoutingTable* pendingGarbage_ = nullptr;  // old table awaiting deletion
};

} // namespace squeeze
```

### C ABI

MidiRouter is not directly exposed through the C ABI. Its functionality is accessed through MidiDeviceManager's FFI functions (`sq_midi_route`, `sq_midi_unroute`, `sq_midi_routes`) and Engine's processBlock (dispatch happens internally).

### Python API

Routing is exposed through the Engine wrapper (which delegates to MidiDeviceManager → MidiRouter):

```python
route_id = engine.midi_route("Launchpad Pro", synth_id, channel=0, note=-1)
engine.midi_unroute(route_id)
routes = engine.midi_routes  # list of MidiRoute
```

## Routing Table

The routing table is a list of `MidiRoute` entries. Multiple routes can share the same device (fan-out) or the same destination node (fan-in). Channel and note filtering are per-route.

Example configurations:

```
Device "KeyStep"   → PluginNode 5,  channel 0 (all), note -1 (all)
Device "KeyStep"   → PluginNode 8,  channel 2,       note -1 (all)
Device "KeyStep"   → SamplerNode 9, channel 0 (all), note 36 (kick only)
Device "Launchpad" → SamplerNode 5, channel 10,      note -1 (all)
```

Note filtering enables the **one-SamplerNode-per-slice** pattern: each MIDI note routes to a different SamplerNode, where each node points at a different buffer region. This gives full per-slice parameter independence.

### Commit Model

Control-thread changes to routes (add/remove) are staged. `commit()` publishes the new routing table to the audio thread via an atomic pointer swap. Engine calls `commit()` after any routing change.

The routing table is small (tens of entries at most), so it is copied in full on each commit. Old tables are garbage-collected on the control thread (same pattern as GraphSnapshot).

## Audio Thread Dispatch

`dispatch()` runs once per audio callback, before the node processing loop:

```
for each device with a queue:
    drain SPSCQueue into a temporary MidiBuffer (per-device)

for each route in the active routing table:
    for each message in the route's device buffer:
        if (channelFilter == 0 or message.channel == channelFilter)
           and (noteFilter == -1 or message.noteNumber == noteFilter):
            append message to nodeBuffers[route.nodeId] at samplePosition 0
```

All destination MidiBuffers must be cleared before calling `dispatch()`. The GraphSnapshot pre-allocates one MidiBuffer per node.

### Sample Position

All messages dispatched in a single block get `samplePosition = 0`. Sub-sample MIDI timing is a future enhancement.

## Invariants

- A route can only reference a device that has a queue. `addRoute()` fails if the device queue does not exist.
- Multiple routes may target the same node (fan-in). Messages from all matching routes are merged.
- Multiple routes may source from the same device (fan-out with different filters or destinations).
- `dispatch()` is RT-safe: no allocation, no blocking, no unbounded iteration.
- `commit()` is O(N) where N is route count. Route count is bounded (expected < 100).
- Removing a route does not remove the device queue. Removing a device queue removes all its routes.
- Route IDs are monotonically increasing, starting from 1, never reused.
- `pushMidiEvent()` is lock-free and wait-free (writes to SPSC queue).
- SysEx messages (> 3 bytes) are rejected by `pushMidiEvent()` (MidiEvent only holds 3 bytes).

## Error Conditions

| Operation | Error | Behavior |
|-----------|-------|----------|
| `createDeviceQueue()` for existing device | Already exists | No-op, returns true |
| `removeDeviceQueue()` for unknown device | Not found | No-op |
| `addRoute()` for device with no queue | Device not registered | Returns -1, sets error |
| `addRoute()` with invalid channel (< 0 or > 16) | Invalid filter | Returns -1, sets error |
| `addRoute()` with invalid note (< -1 or > 127) | Invalid filter | Returns -1, sets error |
| `removeRoute()` with invalid ID | Route not found | Returns false |
| `pushMidiEvent()` when queue is full | Queue overflow | Returns false, increments dropped count |
| `pushMidiEvent()` for unknown device | Device not found | Returns false |
| `dispatch()` with no active routing table | No routes committed | No-op (no messages dispatched) |

## Does NOT Handle

- **JUCE device connections** — MidiDeviceManager opens/closes hardware (tier 11)
- **SysEx messages** — MidiEvent is 3 bytes max; SysEx silently dropped at MidiDeviceManager level
- **Timestamp-based sample-accurate dispatch** — all messages get samplePosition=0 (future enhancement)
- **MIDI output** — future
- **MIDI-to-MIDI processing** (arpeggiators, etc.) — future
- **MIDI learn / auto-mapping** — future
- **Device enumeration** — MidiDeviceManager queries JUCE for available devices

## Dependencies

- SPSCQueue (lock-free queue for MIDI callback → audio thread)
- JUCE (`juce_audio_basics`: MidiBuffer, MidiMessage for dispatch)

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| `createDeviceQueue()` / `removeDeviceQueue()` | Control | Under Engine's controlMutex_ |
| `addRoute()` / `removeRoute()` / `removeRoutesFor*()` | Control | Stages changes, under controlMutex_ |
| `commit()` | Control | Atomic pointer swap, under controlMutex_ |
| `getRoutes()` | Control | Reads staged routes, under controlMutex_ |
| `pushMidiEvent()` | MIDI callback | Lock-free push to per-device SPSCQueue |
| `dispatch()` | Audio | Reads atomic pointer to routing table, drains queues |
| `getQueueFillLevel()` / `getDroppedCount()` | Any | Atomic reads |
| `resetDroppedCounts()` | Control | Atomic writes |

The routing table swap uses the same atomic-pointer pattern as GraphSnapshot: control thread builds new table, publishes via atomic store, audio thread reads via atomic load.

## Integration with Engine

Engine owns `MidiRouter midiRouter_` as a private member:

- **`processBlock()`**: calls `midiRouter_.dispatch(nodeBuffers, numSamples)` before the node processing loop
- **`removeNode()`**: calls `midiRouter_.removeRoutesForNode(nodeId)` to clean up routes for removed nodes
- **Control thread**: routing changes go through FFI → MidiDeviceManager → MidiRouter, serialized by `controlMutex_`

## Example Usage

### Control thread setup

```cpp
MidiRouter router;

// MidiDeviceManager creates a queue when opening a device
router.createDeviceQueue("KeyStep", error);

// Add routes
std::string error;
int r1 = router.addRoute("KeyStep", synthNodeId, 0, -1, error);   // all channels, all notes
int r2 = router.addRoute("KeyStep", kickNodeId,  0, 36, error);   // all channels, note 36 only

// Publish to audio thread
router.commit();
```

### Audio thread dispatch

```cpp
// In Engine::processBlock(), before node processing:
midiRouter_.dispatch(snapshot->nodeBuffers, numSamples);

// Each node's MidiBuffer now contains routed messages
for (auto& nodeId : snapshot->executionOrder) {
    auto& buffers = snapshot->nodeBuffers[nodeId];
    // buffers.midi has messages from MidiRouter
    node->process(context);
}
```

### MIDI callback (called by MidiDeviceManager)

```cpp
void MidiDeviceManager::handleIncomingMidiMessage(
    juce::MidiInput* source, const juce::MidiMessage& message) {
    if (message.getRawDataSize() > 3) return;  // drop SysEx

    MidiEvent event;
    std::memcpy(event.data, message.getRawData(), message.getRawDataSize());
    event.size = message.getRawDataSize();

    router_.pushMidiEvent(deviceName, event);
}
```
