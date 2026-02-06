# Engine Specification

## Overview

Engine owns the audio callback and orchestrates all processing. It processes pending commands from the Scheduler, then walks the graph snapshot in execution order, routing buffers between nodes. The processing logic is decoupled from the audio device so it can be tested without hardware.

## GraphSnapshot

An immutable, pre-allocated structure that the audio thread processes. Built on the control thread from Graph state, swapped atomically via the Scheduler.

```cpp
struct GraphSnapshot {
    struct NodeSlot {
        Node* node;
        int audioSourceIndex;  // index in slots for audio input, or -1 (silence)
        int midiSourceIndex;   // index in slots for MIDI input, or -1 (empty)
    };

    std::vector<NodeSlot> slots;  // in execution order

    // Pre-allocated buffers, one per slot
    std::vector<juce::AudioBuffer<float>> audioOutputs;
    std::vector<juce::MidiBuffer> midiOutputs;

    // For unconnected inputs
    juce::AudioBuffer<float> silenceBuffer;
    juce::MidiBuffer emptyMidi;
};
```

## Responsibilities

- Process pending commands from Scheduler each block
- Process nodes in topological order
- Route audio and MIDI buffers between connected nodes
- Copy final output to the device output buffer
- Build GraphSnapshots from Graph state on the control thread
- Manage audio device lifecycle (start/stop)

## Interface

```cpp
class Engine : public juce::AudioIODeviceCallback {
public:
    explicit Engine(Scheduler& scheduler);
    ~Engine();

    // Control thread: device management
    bool start(double sampleRate = 44100.0, int blockSize = 512);
    void stop();
    bool isRunning() const;
    double getSampleRate() const;
    int getBlockSize() const;

    // Control thread: graph management
    void updateGraph(const Graph& graph);

    // Processing (public for testing without a device)
    void processBlock(juce::AudioBuffer<float>& outputBuffer,
                      juce::MidiBuffer& outputMidi,
                      int numSamples);

    // JUCE AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
};
```

## Per-Block Processing (processBlock)

1. **Drain Scheduler**: Process all pending commands
   - `swapGraph`: swap `activeSnapshot_` pointer, send old to garbage
   - `setParameter`: call `node->setParameter()`
2. **If no active snapshot**: write silence, return
3. **For each slot in execution order**:
   - Resolve audio input: source slot's `audioOutputs[i]` or `silenceBuffer`
   - Resolve MIDI input: source slot's `midiOutputs[i]` or `emptyMidi`
   - Clear this slot's output buffers
   - Call `node->process(context)`
4. **Copy to device output**: last slot's audio output goes to the device buffer. If no slots, silence.

## Building a Snapshot (updateGraph)

1. Get execution order and connections from Graph
2. Map node IDs to slot indices
3. For each node in order, find which source slot feeds its audio and MIDI inputs
4. Pre-allocate output buffers sized to each node's output channel count
5. Send snapshot via Scheduler `swapGraph` command

## Invariants

- `processBlock` is realtime-safe: no allocation, no blocking
- All buffers are pre-allocated in the snapshot on the control thread
- The audio thread only swaps pointers and reads pre-allocated data
- `updateGraph` can be called while audio is running (goes through Scheduler)
- Nodes are prepared (via `prepare()`) before being included in a snapshot

## Error Conditions

- `processBlock` with no active snapshot: outputs silence
- `start()` returns false if device fails to open
- Empty graph produces silence

## Does NOT Handle

- Transport (no tempo, no position for this milestone)
- Meters
- Plugin loading (PluginNode's responsibility)
- Graph topology (Graph's responsibility)

## Dependencies

- Scheduler (command queue)
- Graph (topology queries for building snapshots)
- Node (processing)
- Port (connection routing)
- JUCE (AudioIODeviceCallback, AudioBuffer, MidiBuffer, AudioDeviceManager)

## Thread Safety

- `start()`, `stop()`, `updateGraph()`: control thread only
- `processBlock()`: audio thread (or test thread)
- `getSampleRate()`, `getBlockSize()`, `isRunning()`: safe from any thread (atomic reads)
