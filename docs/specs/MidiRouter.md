# MidiRouter Specification

## Summary

MidiRouter replaces MidiInputNode with a centralized MIDI routing system. Instead of representing each MIDI device as a node in the audio graph, MidiRouter owns all MIDI device connections and distributes incoming messages directly to destination nodes' input MidiBuffers at the top of each audio callback. MIDI routing is configured via a routing table, independent of the audio graph topology.

## Motivation

MidiInputNode exists only to shuttle bytes from a device SPSCQueue to a MidiBuffer. This creates phantom nodes in the graph, couples MIDI routing to graph topology (requiring snapshot rebuilds for routing changes), and makes auto-loaded devices awkward to discover from Lua. MIDI is message dispatch, not signal flow — it belongs in a routing table, not a graph.

## Responsibilities

- Own MIDI device connections (open, close, receive callbacks)
- Bridge MIDI thread to audio thread via per-device lock-free queues
- Maintain a routing table mapping devices to destination nodes with channel filters
- At the top of each audio callback, drain device queues and populate destination nodes' input MidiBuffers
- Track queue fill levels and dropped message counts for performance monitoring
- Support hot-plug: add/remove devices without rebuilding the audio graph

## Interface

```cpp
struct MidiRoute {
    int id;                 // unique route ID
    std::string deviceName; // source device
    int nodeId;             // destination node
    int channelFilter;      // 0 = all, 1-16 = specific channel
};

class MidiRouter : public juce::MidiInputCallback {
public:
    MidiRouter();
    ~MidiRouter();

    // --- Control thread (called under Engine::controlMutex_) ---

    // Device management
    std::vector<std::string> getAvailableDevices() const;
    bool openDevice(const std::string& deviceName, std::string& error);
    void closeDevice(const std::string& deviceName);
    bool isDeviceOpen(const std::string& deviceName) const;
    std::vector<std::string> getOpenDevices() const;

    // Routing
    int addRoute(const std::string& deviceName, int nodeId,
                 int channelFilter, std::string& error);
    bool removeRoute(int routeId);
    bool removeRoutesForNode(int nodeId);
    bool removeRoutesForDevice(const std::string& deviceName);
    std::vector<MidiRoute> getRoutes() const;

    // Publish updated routing table to audio thread
    void commit();

    // --- Audio thread ---

    // Called at the top of processBlock, before any node processing.
    // Drains all device queues and writes into destination MidiBuffers.
    // nodeBuffers: map from nodeId to the MidiBuffer* that node will read.
    void dispatchMidi(const std::unordered_map<int, juce::MidiBuffer*>& nodeBuffers);

    // --- MIDI thread (juce::MidiInputCallback) ---
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

    // --- Monitoring (RT-safe reads) ---
    int getQueueFillLevel(const std::string& deviceName) const;
    int getDroppedCount(const std::string& deviceName) const;
    void resetDroppedCounts();
};
```

## Routing Table

The routing table is a list of `MidiRoute` entries. Multiple routes can share the same device (fan-out) or the same destination node (fan-in). Channel filtering is per-route.

Example configurations:

```
Device "KeyStep"  → SamplerNode 5, channel 0 (all)
Device "KeyStep"  → PluginNode 8,  channel 2 (filter to ch 2 only)
Device "Launchpad" → SamplerNode 5, channel 10 (drums only)
```

### Commit Model

Control-thread changes to routes (add/remove) are staged. `commit()` publishes the new routing table to the audio thread. Engine calls `commit()` after any routing change, similar to how `updateGraph()` pushes graph changes.

The routing table is small (tens of entries at most), so it is copied in full on each commit. The audio thread swaps to the new table via an atomic pointer. Old tables are garbage-collected on the control thread (same pattern as GraphSnapshot).

## Audio Thread Dispatch

`dispatchMidi()` runs once per audio callback, before the node processing loop:

```
for each open device:
    drain SPSCQueue into a temporary MidiBuffer (per-device)

for each route in the active routing table:
    for each message in the route's device buffer:
        if channelFilter == 0 or message.channel == channelFilter:
            append message to nodeBuffers[route.nodeId]
```

All MidiBuffers in `nodeBuffers` must be cleared before calling `dispatchMidi()`. The snapshot pre-allocates one MidiBuffer per node slot (already the case today).

### Sample Position

All messages dispatched in a single block get `samplePosition = 0`. This matches MidiInputNode's current behavior. Timestamp-based sub-block placement is a future enhancement (see Does NOT Handle).

## Device Management

Each open device has:
- A `juce::MidiInput` instance (started on open, stopped on close)
- A `SPSCQueue<MidiEvent, 1024>` for MIDI thread → audio thread transfer
- An `std::atomic<int>` dropped count
- A device name and identifier

`MidiEvent` is unchanged from current implementation:

```cpp
struct MidiEvent {
    uint8_t data[3];
    int size;  // 1, 2, or 3
};
```

SysEx messages are silently dropped (same as current).

### Auto-Open at Startup

Engine calls `midiRouter_.openDevice()` for each available device at startup (replaces `autoLoadMidiInputs()`). Devices are opened but have no routes until Lua configures them.

### Hot-Plug

`refreshDevices()` can be added later to detect new/removed devices. Routing table entries for missing devices are silently skipped during dispatch (the device queue simply has no data). No graph rebuild needed.

## Invariants

1. A route can only reference an open device. `addRoute()` fails if the device is not open.
2. A route can only reference a valid node ID. Engine validates before calling `addRoute()`.
3. Multiple routes may target the same node (fan-in). Messages from all matching routes are merged into that node's MidiBuffer.
4. Multiple routes may source from the same device (fan-out with different channel filters or destinations).
5. `dispatchMidi()` is RT-safe: no allocation, no blocking, no unbounded iteration.
6. `commit()` is O(N) where N is route count. Route count is bounded (expected < 100).
7. Removing a route for a device does not close the device. Closing a device removes all its routes.
8. Route IDs are monotonically increasing, never reused.

## Error Conditions

| Operation | Error | Behavior |
|-----------|-------|----------|
| `openDevice()` with unknown name | Device not found | Returns false, sets error message |
| `openDevice()` for already-open device | Already open | No-op, returns true |
| `openDevice()` when OS rejects | Open failed | Returns false, sets error message |
| `addRoute()` for closed device | Device not open | Returns -1, sets error message |
| `removeRoute()` with invalid ID | Route not found | Returns false |
| Queue overflow on MIDI thread | Queue full | Message dropped, atomic counter incremented |

## Does NOT Handle

- SysEx messages (dropped, same as current)
- MIDI output (sending messages to devices)
- Timestamp-based sample-accurate dispatch (messages get samplePosition=0; nodes like SamplerNode do their own sub-block splitting based on MIDI content)
- MIDI-to-MIDI processing nodes (arpeggiators, etc.) — if needed later, can be added as a special routing chain
- MIDI learn / auto-mapping

## Dependencies

- `SPSCQueue` (existing, no changes)
- `juce_audio_devices` (for `juce::MidiInput`)
- `juce_audio_basics` (for `juce::MidiBuffer`)

## Thread Safety

| Method | Thread | Synchronization |
|--------|--------|-----------------|
| `openDevice`, `closeDevice` | Control | Under Engine::controlMutex_ |
| `addRoute`, `removeRoute`, `commit` | Control | Under Engine::controlMutex_ |
| `getRoutes`, `getAvailableDevices` | Control | Under Engine::controlMutex_ |
| `dispatchMidi` | Audio | Reads atomic pointer to routing table |
| `handleIncomingMidiMessage` | MIDI | Lock-free push to per-device SPSCQueue |
| `getQueueFillLevel`, `getDroppedCount` | Any | Atomic reads |

The routing table swap uses the same atomic-pointer pattern as GraphSnapshot: control thread builds new table, publishes via atomic store, audio thread reads via atomic load. Old tables are garbage-collected on control thread.

## Integration with Engine

Engine changes:

- **Remove**: `MidiInputNode` dependency, `midiDeviceNodes_` map, `addMidiInput*()` methods, `autoLoadMidiInputs()`
- **Add**: `MidiRouter midiRouter_` member
- **Startup**: `midiRouter_.openDevice()` for each available device (replaces `autoLoadMidiInputs()`)
- **`processBlock()`**: call `midiRouter_.dispatchMidi(nodeBuffers)` before the node loop, remove MidiInputNode-specific queue monitoring (replaced by `MidiRouter::getQueueFillLevel/getDroppedCount`)
- **`buildSnapshot()`**: remove `midiSources` and `midiInputNode` from NodeSlot. Each slot still has a pre-allocated MidiBuffer, but it's populated by MidiRouter, not by graph wiring.
- **`connect()`**: MIDI connections through the graph are removed. `connect()` only handles audio. MIDI routing uses `midiRouter_.addRoute()`.
- **Node removal**: call `midiRouter_.removeRoutesForNode(nodeId)` in `removeNode()`

## Integration with GraphSnapshot

GraphSnapshot simplifies:

```cpp
struct NodeSlot {
    Node* node;
    int nodeId;
    int audioSourceIndex;
    // midiSources removed — MidiRouter handles this
    bool isAudioLeaf;
    // midiInputNode removed — MidiRouter owns devices
};
```

The `filteredMidi` scratch buffer moves to MidiRouter (it handles channel filtering internally).

Each slot retains a MidiBuffer in the snapshot's `midiOutputs` vector. MidiRouter's `dispatchMidi()` writes to these buffers by nodeId lookup. Nodes that receive no MIDI simply see an empty buffer.

## Lua API

Replace current MIDI Lua functions with:

```lua
-- List available MIDI devices (whether open or not)
sq.list_midi_devices()          -- returns {"KeyStep", "Launchpad", ...}

-- List currently open devices
sq.open_midi_devices()          -- returns {"KeyStep", "Launchpad", ...}

-- Route a device to a node (opens device automatically if not already open)
-- Returns route ID on success, (nil, error) on failure
sq.midi_route("KeyStep", sampler)              -- all channels
sq.midi_route("KeyStep", sampler, {channel=3}) -- channel 3 only

-- Remove a route by ID
sq.midi_unroute(route_id)

-- List all active routes
sq.midi_routes()  -- returns {{id=1, device="KeyStep", node_id=5, channel=0}, ...}
```

### Auto-open behavior

`sq.midi_route()` auto-opens the device if not already open. This means the common case is a single call:

```lua
sq.midi_route("KeyStep", sampler)
```

No need to separately open devices and then route them.

### Bootstrap changes

- Remove `MidiInputNode` metatable and `sq.add_midi_input` wrapper
- No node wrapper needed for MIDI routes — they return a route ID, not a node object

## Performance Monitoring

PerfMonitor integration:

- `reportMidiQueue(deviceName, fillLevel, droppedCount)` — called from audio thread after dispatch
- Per-device metrics instead of per-node metrics
- `sq.perf()` Lua table includes `midi_devices` section with per-device queue stats

## Example Usage

```lua
-- Load a sample
local buf = sq.load_buffer("/path/to/kick.wav")

-- Create a sampler
local sampler = sq.add_sampler("drums", 8)
sampler:set_buffer(buf)

-- Route all MIDI devices to the sampler
for _, dev in ipairs(sq.list_midi_devices()) do
    sq.midi_route(dev, sampler)
end

-- Or route specific devices with channel filters
sq.midi_route("KeyStep", sampler, {channel=1})
sq.midi_route("Launchpad", sampler, {channel=10})

-- Start audio
sq.update()
sq.start(44100, 512)

-- Later: check routes
for _, r in ipairs(sq.midi_routes()) do
    print(r.device .. " -> node " .. r.node_id .. " (ch " .. r.channel .. ")")
end

-- Remove a specific route
sq.midi_unroute(route_id)
```
