# PluginNode Specification

## Responsibilities

- Wrap a `juce::AudioPluginInstance` and implement the Node interface
- Declare ports based on the plugin's channel configuration
- Forward `process()` to the plugin's `processBlock()`
- Expose the plugin's parameters via the Node parameter interface (string-based names, normalized 0–1 values)

## Overview

PluginNode hosts a single VST3 or AU plugin as a node in the audio graph. It takes ownership of a `juce::AudioProcessor` (typically an `AudioPluginInstance` from JUCE's plugin loading, or a test processor for unit tests). Port configuration is determined at construction from the plugin's channel layout and MIDI capability.

PluginNode does not load plugins itself — PluginManager handles cache lookup and instantiation, returning a `std::unique_ptr<Node>` that the FFI layer passes to Engine.

## Interface

### C++ (`squeeze::PluginNode`)

```cpp
namespace squeeze {

class PluginNode : public Node {
public:
    // Takes ownership of a processor.
    // inputChannels/outputChannels determine port declarations.
    // acceptsMidi determines whether a MIDI input port is declared.
    explicit PluginNode(std::unique_ptr<juce::AudioProcessor> processor,
                        int inputChannels, int outputChannels,
                        bool acceptsMidi);
    ~PluginNode();

    // Non-copyable, non-movable (owns the processor)
    PluginNode(const PluginNode&) = delete;
    PluginNode& operator=(const PluginNode&) = delete;

    // --- Node interface ---
    void prepare(double sampleRate, int blockSize) override;
    void process(ProcessContext& context) override;
    void release() override;

    std::vector<PortDescriptor> getInputPorts() const override;
    std::vector<PortDescriptor> getOutputPorts() const override;

    // --- Parameter interface (string-based, normalized 0–1) ---
    std::vector<std::string> getParameterNames() const override;
    float getParameter(const std::string& name) const override;
    void setParameter(const std::string& name, float value) override;
    std::string getParameterText(const std::string& name) const override;
    std::vector<ParameterDescriptor> getParameterDescriptors() const override;

    // --- Plugin-specific queries ---
    const std::string& getPluginName() const;
    juce::AudioProcessor* getProcessor();

private:
    std::unique_ptr<juce::AudioProcessor> processor_;
    int inputChannels_;
    int outputChannels_;
    bool acceptsMidi_;
    std::string pluginName_;

    // Parameter name → JUCE parameter index lookup
    std::unordered_map<std::string, int> parameterMap_;
};

} // namespace squeeze
```

### C ABI

PluginNode is not directly exposed through the C ABI. Plugin creation goes through `sq_add_plugin()` (FFI orchestration: PluginManager creates PluginNode, Engine owns it). Plugin parameters are accessed through Engine's generic parameter system (`sq_get_param`, `sq_set_param`, `sq_param_descriptors`).

### Python API

```python
synth = engine.add_plugin("Diva")           # returns node ID
engine.set_param(synth, "cutoff", 0.75)     # generic parameter interface
print(engine.get_param(synth, "cutoff"))
print(engine.param_descriptors(synth))       # list of ParameterDescriptor
```

## Port Declaration

Ports are determined at construction from the plugin's channel configuration:

- **Audio input**: If `inputChannels > 0`, declares one audio input port named `"in"` with that many channels
- **Audio output**: If `outputChannels > 0`, declares one audio output port named `"out"` with that many channels
- **MIDI input**: If `acceptsMidi` is true, declares one MIDI input port named `"midi_in"`
- **MIDI output**: Always declares one MIDI output port named `"midi_out"` (for MIDI pass-through or generated MIDI)

Typical configurations:

| Plugin type | Audio in | Audio out | MIDI in | Example |
|-------------|----------|-----------|---------|---------|
| Instrument | none | stereo (2ch) | yes | Diva, Pigments |
| Effect | stereo (2ch) | stereo (2ch) | optional | Valhalla Delay |
| MIDI effect | none | none | yes | MIDI arpeggiator |

## Processing

`process()` delegates to the plugin's `processBlock()`:

1. If plugin has audio inputs: copy input audio → working buffer (JUCE processes in-place)
2. If plugin has no audio inputs (instrument): clear the working buffer
3. Copy input MIDI → working MIDI buffer (JUCE processes MIDI in-place)
4. Call `processor_->processBlock(audioBuffer, midiBuffer)`
5. Output audio is in the working buffer; output MIDI is in the working MIDI buffer

The plugin processes both audio and MIDI in-place.

## Parameters

Parameters are exposed by their JUCE parameter name (`AudioProcessorParameter::getName()`). Values are normalized 0.0–1.0.

- `getParameterNames()`: returns all parameter names from the plugin, in JUCE's parameter order
- `getParameter(name)`: returns the current normalized value, or 0.0 if name unknown
- `setParameter(name, value)`: sets the normalized value, no-op if name unknown
- `getParameterText(name)`: returns the display text for the current value (e.g., "440 Hz", "-12 dB")
- `getParameterDescriptors()`: returns name, min (always 0), max (always 1), and default value for each parameter

The parameter map (name → JUCE index) is built once at construction and is immutable. This avoids string lookups on the audio thread — `setParameter` on the control thread resolves the index, and the JUCE parameter system handles audio-thread reads internally.

## Invariants

- `process()` is RT-safe (delegates to the plugin's `processBlock`, which is expected to be RT-safe by the plugin vendor)
- Port configuration is fixed after construction (never changes)
- `prepare()` is called before the first `process()` and after any sample rate or block size change
- `release()` is called before destruction
- PluginNode owns the `AudioProcessor` and destroys it on destruction
- The parameter map is immutable after construction
- `getParameter()` with unknown name returns 0.0f (not an error)
- `setParameter()` with unknown name is a no-op (not an error)

## Error Conditions

- Constructor with null processor: undefined behavior (caller must validate — PluginManager handles this)
- `getParameter()` with unknown name: returns 0.0f
- `setParameter()` with unknown name: no-op, logged at debug level
- `getParameterText()` with unknown name: returns empty string
- Plugin's `processBlock` throws or crashes: not recoverable (plugin bug, not PluginNode's fault)

## Does NOT Handle

- **Plugin loading / instantiation** — PluginManager creates the AudioPluginInstance
- **Plugin scanning / cache management** — PluginManager
- **Plugin GUI / editor windows** — future (PluginEditorWindow)
- **Plugin state save / restore (presets)** — future
- **Multi-bus routing** — single input bus, single output bus (covers the vast majority of plugins)
- **Side-chain inputs** — future
- **Parameter automation from EventScheduler** — Engine handles sub-block splitting and calls setParameter at split boundaries

## Dependencies

- Node (base class — provides port/parameter interface)
- Port (PortDescriptor, SignalType)
- JUCE (`juce_audio_processors`: AudioProcessor, AudioPluginInstance, AudioProcessorParameter)
- JUCE (`juce_audio_basics`: AudioBuffer, MidiBuffer)

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| Constructor | Control | Builds parameter map, stores processor |
| `prepare()` / `release()` | Control | Delegates to processor |
| `process()` | Audio | Delegates to processor's processBlock |
| `getInputPorts()` / `getOutputPorts()` | Any | Immutable after construction |
| `getParameterNames()` / `getParameterDescriptors()` | Any | Immutable after construction |
| `getParameter()` | Control | Reads from JUCE parameter (atomic internally) |
| `setParameter()` | Control | Writes to JUCE parameter |
| `getParameterText()` | Control | Reads from JUCE parameter |
| `getPluginName()` / `getProcessor()` | Any | Immutable / stable pointer |

JUCE's parameter system handles the control-thread-write / audio-thread-read synchronization internally.

## Example Usage

### Created by PluginManager (typical path)

```cpp
// PluginManager::createNode does this internally:
auto instance = formatManager_.createPluginInstance(
    description, sampleRate, blockSize, errorMessage);

auto node = std::make_unique<PluginNode>(
    std::move(instance),
    description.numInputChannels,
    description.numOutputChannels,
    description.isInstrument);
```

### Used through Engine

```cpp
// FFI layer orchestrates:
auto node = pluginManager.createNode("Diva", 44100.0, 512, error);
int synthId = engine.addNode(std::move(node), "Diva");

int output = engine.getOutputNodeId();
engine.connect(synthId, "out", output, "in", error);

// Parameters through Engine's generic interface
engine.setParameter(synthId, "cutoff", 0.5f);
float val = engine.getParameter(synthId, "cutoff");
```

### Unit testing with a mock processor

```cpp
// For testing without a real plugin:
auto mockProcessor = std::make_unique<MockAudioProcessor>();
// configure mock: 0 inputs, 2 outputs, accepts MIDI
auto node = std::make_unique<PluginNode>(std::move(mockProcessor), 0, 2, true);

node->prepare(44100.0, 512);
// ... feed MIDI, process, verify output ...
node->release();
```
