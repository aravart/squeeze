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
        MidiDeviceQueue* queue;  // cached from MidiRouter::createDeviceQueue()
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
```

MIDI routing is handled through Source-level assignment (`sq_midi_assign`, `sq_midi_note_range`) — see Source spec and Engine spec. MidiDeviceManager only manages device connections.

### Python API

```python
# Via squeeze.midi sub-object
devs = s.midi.devices           # list of MidiDevice
dev = s.midi.devices[0]
dev.open()
dev.close()
open_devs = s.midi.open_devices # list of MidiDevice
```

## Invariants

- `getAvailableDevices()` returns the current system MIDI devices (may change between calls due to hot-plug)
- `openDevice()` for an already-open device is a no-op (returns true)
- `openDevice()` creates a SPSC queue in MidiRouter (`createDeviceQueue()`), caches the returned `MidiDeviceQueue*`, then starts the `juce::MidiInput`
- `closeDevice()` stops the `juce::MidiInput` first (so no more callbacks can fire), discards the cached `MidiDeviceQueue*`, then tells MidiRouter to remove the device's queue and all associated routes. Order matters: stop input before invalidating the queue.
- `closeDevice()` for a device that is not open is a no-op
- `closeAllDevices()` stops all open devices (called from destructor)
- SysEx messages are silently dropped (messages > 3 bytes)
- `handleIncomingMidiMessage` pushes to the cached `MidiDeviceQueue*` via `router_.pushMidiEvent(queue, event)` — no map lookup, no string allocation, never blocks
- Device names are case-sensitive exact match (JUCE device identifiers)

## Error Conditions

- `openDevice()` with a name not in `getAvailableDevices()`: returns false, sets error
- `openDevice()` when JUCE `MidiInput::openDevice()` fails (device busy, permissions): returns false, sets error
- `closeDevice()` with unknown name: no-op (not an error)
- MIDI queue full (messages arrive faster than audio callback drains): messages dropped, logged at warn via MidiRouter

## Does NOT Handle

- **Routing configuration** — via Source MidiAssignment (sq_midi_assign, sq_midi_note_range)
- **MIDI dispatch to sources** — MidiRouter (drain queues in processBlock)
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
| `handleIncomingMidiMessage()` | MIDI callback | Uses cached `MidiDeviceQueue*` for lock-free SPSC push. No map lookup, no string ops, never blocks. |

All control-thread methods are called from the FFI layer under `controlMutex_`. The MIDI callback thread is a separate system thread managed by JUCE — `handleIncomingMidiMessage` must be lock-free and non-allocating. The `MidiDeviceQueue*` is resolved at `openDevice()` time and cached in `OpenDevice`, so the hot path is a single SPSC push.

## Example Usage

### C ABI

```c
char* error = NULL;
SqEngine engine = sq_create(44100.0, 512, &error);

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

// Create a source and assign MIDI
SqSource synth = sq_add_source_plugin(engine, "Lead", "Diva.vst3", &error);
sq_midi_assign(engine, synth, "Launchpad Pro", 1);

// Later: close device
sq_midi_close(engine, "Launchpad Pro");

sq_destroy(engine);
```

### Python

```python
from squeeze import Squeeze

s = Squeeze()

# Enumerate and open
print(s.midi.devices)
dev = s.midi.devices[0]
dev.open()

# Create a source and assign MIDI
synth = s.add_source("Diva", plugin="Diva.vst3")
synth.midi_assign(device=dev.name, channel=1)

# Cleanup
dev.close()
s.close()
```
