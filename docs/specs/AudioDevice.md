# AudioDevice Specification

## Responsibilities

- Wrap `juce::AudioDeviceManager` — configure, open, and close audio devices
- Implement `juce::AudioIODeviceCallback` — call `Engine::processBlock()` from the JUCE audio callback
- Provide start/stop control over the audio device
- Report actual device sample rate and block size
- Handle `audioDeviceAboutToStart` — prepare Engine and all nodes with actual device parameters

## Overview

AudioDevice is the bridge between the JUCE audio device system and the Engine. It owns the `juce::AudioDeviceManager`, opens/closes audio devices, and forwards the audio callback to `Engine::processBlock()`. Engine has no dependency on JUCE audio devices — AudioDevice depends on Engine, not the other way around. For headless testing, `Engine::prepareForTesting()` bypasses AudioDevice entirely.

## Interface

### C++ (`squeeze::AudioDevice`)

```cpp
namespace squeeze {

class AudioDevice : public juce::AudioIODeviceCallback {
public:
    explicit AudioDevice(Engine& engine);
    ~AudioDevice();

    // Non-copyable, non-movable
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    // --- Control thread ---
    bool start(double sampleRate, int blockSize, std::string& error);
    void stop();
    bool isRunning() const;
    double getSampleRate() const;
    int getBlockSize() const;

    // --- JUCE AudioIODeviceCallback (audio thread) ---
    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData, int numInputChannels,
        float** outputChannelData, int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    Engine& engine_;
    juce::AudioDeviceManager deviceManager_;
    bool running_ = false;
    double sampleRate_ = 0.0;
    int blockSize_ = 0;
};

} // namespace squeeze
```

### C ABI (`squeeze_ffi.h`)

```c
// Audio device control (operates on EngineHandle's AudioDevice)
bool sq_start(SqEngine engine, double sample_rate, int block_size, char** error);
void sq_stop(SqEngine engine);
bool sq_is_running(SqEngine engine);
double sq_sample_rate(SqEngine engine);
int sq_block_size(SqEngine engine);
```

### Python API

```python
engine.start(sample_rate=44100.0, block_size=512)
engine.stop()
engine.is_running      # property
engine.sample_rate     # property (0.0 if not running)
engine.block_size      # property (0 if not running)
```

## Invariants

- `isRunning()` returns false before `start()` is called
- `isRunning()` returns true after a successful `start()` and before `stop()`
- `getSampleRate()` and `getBlockSize()` return 0 when not running
- `getSampleRate()` and `getBlockSize()` return the actual device values (not the hints) when running
- `stop()` when not running is a no-op
- `start()` when already running stops first, then restarts with new parameters
- The audio callback calls `Engine::processBlock()` exactly once per invocation
- `audioDeviceAboutToStart` prepares all nodes via Engine before the first callback
- AudioDevice never acquires `controlMutex_` — Engine's processBlock is lock-free

## Error Conditions

- `start()` with no available audio device: returns false, sets error
- `start()` with unsupported sample rate / block size: returns false, sets error (device may select nearest valid values — this is a JUCE behavior, not an error)
- Device disappears while running: JUCE calls `audioDeviceStopped()`, `isRunning()` becomes false
- `start()` after `stop()`: succeeds (re-entrant lifecycle)

## Does NOT Handle

- **Graph topology, node management** — Engine
- **processBlock logic** — Engine (AudioDevice just calls it)
- **Testing bypass** — `Engine::prepareForTesting()` handles headless tests
- **Device enumeration / selection** — future (currently opens default device)
- **MIDI device management** — MidiDeviceManager
- **Message pump** — `sq_pump()` at FFI level

## Dependencies

- Engine (reference — calls `processBlock()` and `prepareForTesting()` equivalent)
- JUCE (`juce_audio_devices`: AudioDeviceManager, AudioIODeviceCallback)

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| `start()` | Control | Acquires no Engine locks; configures JUCE device manager |
| `stop()` | Control | Removes callback, closes device |
| `isRunning()` / `getSampleRate()` / `getBlockSize()` | Control | Reads local state |
| `audioDeviceIOCallbackWithContext()` | Audio | Calls Engine::processBlock() — never locks |
| `audioDeviceAboutToStart()` | Audio | Called by JUCE before first callback |
| `audioDeviceStopped()` | Audio | Called by JUCE when device stops |

`start()` and `stop()` must not be called concurrently with each other. The FFI layer serializes them via `controlMutex_` in the EngineHandle.

## Example Usage

### C ABI

```c
char* error = NULL;
SqEngine engine = sq_engine_create(&error);

// Start audio with hints (device may choose actual values)
if (!sq_start(engine, 44100.0, 512, &error)) {
    fprintf(stderr, "Audio start failed: %s\n", error);
    sq_free_string(error);
}

printf("Actual SR: %.0f, BS: %d\n", sq_sample_rate(engine), sq_block_size(engine));

// ... audio is processing ...

sq_stop(engine);
sq_engine_destroy(engine);
```

### Python

```python
from squeeze import Squeeze

s = Squeeze()
s.start()

print(f"SR: {s.sample_rate}, BS: {s.block_size}")

s.stop()
s.close()
```

### Headless testing (no AudioDevice)

```cpp
Engine engine;
engine.prepareForTesting(44100.0, 512);

// Process directly — no AudioDevice involved
float* outputs[2] = { leftBuf, rightBuf };
engine.processBlock(outputs, 2, 512);
```
