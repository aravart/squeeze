# MidiDeviceManager Specification

## Responsibilities

- Own JUCE `MidiInput` device connections (open, close, enumerate)
- Implement `juce::MidiInputCallback` — receive MIDI messages from hardware
- Push incoming MIDI to per-device SPSC queues (owned by MidiRouter)
- Provide device enumeration, open/close, and status queries

## Overview

MidiDeviceManager is the hardware MIDI layer. It wraps JUCE's MIDI input system, manages physical device connections, and feeds incoming messages into MidiRouter's per-device SPSC queues. MidiRouter handles routing, dispatch, and monitoring — MidiDeviceManager is strictly the device layer.

This is a two-class split: MidiRouter (tier 6) handles the routing table and audio-thread dispatch. MidiDeviceManager (tier 11) adds JUCE device management on top. MidiRouter can function without MidiDeviceManager (e.g., with programmatic MIDI via EventScheduler), but MidiDeviceManager requires MidiRouter.

## Interface

### C++ (`squeeze::MidiDeviceManager`)

```cpp
namespace squeeze {

class MidiDeviceManager : public juce::MidiInputCallback {
public:
    explicit MidiDeviceManager(MidiRouter& router);
    ~MidiDeviceManager();

    // Non-copyable, non-movable
    MidiDeviceManager(const MidiDeviceManager&) = delete;
    MidiDeviceManager& operator=(const MidiDeviceManager&) = delete;

    // --- Control thread ---
    std::vector<std::string> getAvailableDevices() const;
    bool openDevice(const std::string& name, std::string& error);
    void closeDevice(const std::string& name);
    bool isDeviceOpen(const std::string& name) const;
    std::vector<std::string> getOpenDevices() const;
    void closeAllDevices();

    // --- JUCE MidiInputCallback (MIDI thread) ---
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

private:
    MidiRouter& router_;
    struct OpenDevice {
        std::unique_ptr<juce::MidiInput> device;
        std::string name;
    };
    std::vector<OpenDevice> openDevices_;
};

} // namespace squeeze
```

### C ABI (`squeeze_ffi.h`)

```c
// Device management
SqStringList sq_midi_devices(SqEngine engine);
bool sq_midi_open(SqEngine engine, const char* name, char** error);
void sq_midi_close(SqEngine engine, const char* name);
SqStringList sq_midi_open_devices(SqEngine engine);

// Routing (delegates to MidiRouter)
int  sq_midi_route(SqEngine engine, const char* device, int node_id,
                   int channel_filter, int note_filter, char** error);
bool sq_midi_unroute(SqEngine engine, int route_id);
SqMidiRouteList sq_midi_routes(SqEngine engine);
```

### Python API

```python
devices = engine.midi_devices              # property, list of strings
engine.midi_open("Launchpad Pro")
engine.midi_close("Launchpad Pro")
open_devs = engine.midi_open_devices       # property, list of strings

# Routing (delegates to MidiRouter)
route_id = engine.midi_route("Launchpad Pro", synth_id, channel=0, note=-1)
engine.midi_unroute(route_id)
routes = engine.midi_routes                # property, list of MidiRoute
```

## Invariants

- `getAvailableDevices()` returns the current system MIDI devices (may change between calls due to hot-plug)
- `openDevice()` for an already-open device is a no-op (returns true)
- `openDevice()` creates a SPSC queue in MidiRouter for the device before starting the `juce::MidiInput`
- `closeDevice()` stops the `juce::MidiInput` and tells MidiRouter to remove the device's queue and all associated routes
- `closeDevice()` for a device that is not open is a no-op
- `closeAllDevices()` stops all open devices (called from destructor)
- SysEx messages are silently dropped (messages > 3 bytes)
- `handleIncomingMidiMessage` pushes to MidiRouter's device queue — never blocks, never allocates
- Device names are case-sensitive exact match (JUCE device identifiers)

## Error Conditions

- `openDevice()` with a name not in `getAvailableDevices()`: returns false, sets error
- `openDevice()` when JUCE `MidiInput::openDevice()` fails (device busy, permissions): returns false, sets error
- `closeDevice()` with unknown name: no-op (not an error)
- MIDI queue full (messages arrive faster than audio callback drains): messages dropped, logged at warn via MidiRouter

## Does NOT Handle

- **Routing configuration** — MidiRouter (addRoute/removeRoute)
- **MIDI dispatch to nodes** — MidiRouter (drain queues in processBlock)
- **Queue monitoring / drop counts** — MidiRouter
- **MIDI output** — future
- **Virtual MIDI ports** — future
- **SysEx processing** — silently dropped
- **MIDI clock / sync** — future

## Dependencies

- MidiRouter (reference — device queues and routing table)
- JUCE (`juce_audio_devices`: MidiInput, MidiInputCallback, MidiDeviceInfo)

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| `getAvailableDevices()` | Control | Queries JUCE for system MIDI devices |
| `openDevice()` | Control | Creates queue in MidiRouter, starts JUCE MidiInput |
| `closeDevice()` / `closeAllDevices()` | Control | Stops MidiInput, removes queue from MidiRouter |
| `isDeviceOpen()` / `getOpenDevices()` | Control | Read-only |
| `handleIncomingMidiMessage()` | MIDI callback | Lock-free push to SPSC queue — never blocks |

All control-thread methods are called from the FFI layer under `controlMutex_`. The MIDI callback thread is a system thread managed by JUCE — `handleIncomingMidiMessage` must be lock-free and non-allocating.

## Example Usage

### C ABI

```c
char* error = NULL;
SqEngine engine = sq_engine_create(&error);

// List available MIDI devices
SqStringList devices = sq_midi_devices(engine);
for (int i = 0; i < devices.count; i++) {
    printf("MIDI device: %s\n", devices.items[i]);
}
sq_free_string_list(devices);

// Open a device
if (!sq_midi_open(engine, "Launchpad Pro", &error)) {
    fprintf(stderr, "MIDI open failed: %s\n", error);
    sq_free_string(error);
}

// Route the device to a synth node
int synth = sq_add_plugin(engine, "Diva", &error);
int route = sq_midi_route(engine, "Launchpad Pro", synth, 0, -1, &error);

// Later: remove route and close device
sq_midi_unroute(engine, route);
sq_midi_close(engine, "Launchpad Pro");

sq_engine_destroy(engine);
```

### Python

```python
from squeeze import Engine

engine = Engine()
engine.load_plugin_cache("/path/to/plugin-cache.xml")

# Enumerate and open
print(engine.midi_devices)
engine.midi_open("Launchpad Pro")

# Route to a synth
synth = engine.add_plugin("Diva")
route_id = engine.midi_route("Launchpad Pro", synth, channel=0, note=-1)

# Cleanup
engine.midi_unroute(route_id)
engine.midi_close("Launchpad Pro")
engine.close()
```
