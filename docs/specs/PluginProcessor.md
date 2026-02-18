# PluginProcessor Specification

## Responsibilities

- Wrap a `juce::AudioPluginInstance` and implement the Processor interface
- Process audio in-place via the plugin's `processBlock()`
- Expose the plugin's parameters via the Processor parameter interface (string-based names, normalized 0–1 values)
- Report the plugin's processing latency
- Provide access to the underlying `juce::AudioProcessor` for editor windows

## Overview

PluginProcessor hosts a single VST3 or AU plugin as a Processor in a Source chain or Bus chain. It takes ownership of a `juce::AudioProcessor` (typically an `AudioPluginInstance` from JUCE's plugin loading, or a test processor for unit tests). Channel configuration is determined at construction from the plugin's layout.

PluginProcessor does not load plugins itself — PluginManager handles cache lookup and instantiation, returning a `std::unique_ptr<Processor>`.

Unlike the old PluginNode, PluginProcessor has no ports. Audio processing is in-place — the plugin reads from and writes to the same buffer. JUCE plugins already process in-place internally (despite the separate input/output convention), so PluginProcessor aligns naturally with JUCE's actual behavior.

## Interface

### C++ (`squeeze::PluginProcessor`)

```cpp
namespace squeeze {

class PluginProcessor : public Processor {
public:
    // Takes ownership of a processor.
    // numChannels determines the buffer layout for processing.
    // acceptsMidi determines whether process(buffer, midi) uses the midi buffer.
    explicit PluginProcessor(std::unique_ptr<juce::AudioProcessor> processor,
                              int numChannels, bool acceptsMidi);
    ~PluginProcessor();

    // Non-copyable, non-movable (inherits from Processor)

    // --- Processor interface ---
    void prepare(double sampleRate, int blockSize) override;
    void reset() override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void process(juce::AudioBuffer<float>& buffer,
                 const juce::MidiBuffer& midi) override;
    void release() override;

    // --- Parameter interface (string-based, normalized 0–1) ---
    int getParameterCount() const override;
    ParamDescriptor getParameterDescriptor(int index) const override;
    std::vector<ParamDescriptor> getParameterDescriptors() const override;
    float getParameter(const std::string& name) const override;
    void setParameter(const std::string& name, float value) override;
    std::string getParameterText(const std::string& name) const override;

    // --- Latency ---
    int getLatencySamples() const override;

    // --- Plugin-specific queries ---
    const std::string& getPluginName() const;
    juce::AudioProcessor* getProcessor();

    // --- Sidechain support ---
    void setSidechainBuffer(const juce::AudioBuffer<float>* buffer);

private:
    std::unique_ptr<juce::AudioProcessor> processor_;
    int numChannels_;
    bool acceptsMidi_;
    std::string pluginName_;

    // Parameter name → JUCE parameter index lookup
    std::unordered_map<std::string, int> parameterMap_;

    // Optional sidechain buffer (read-only, set by Engine)
    const juce::AudioBuffer<float>* sidechainBuffer_ = nullptr;
};

} // namespace squeeze
```

### C ABI

PluginProcessor is not directly exposed through the C ABI. Plugin creation goes through `sq_source_append()`, `sq_bus_append()`, etc. (FFI orchestration: PluginManager creates PluginProcessor, Engine adds it to the appropriate chain). Plugin parameters are accessed through the generic parameter system (`sq_get_param`, `sq_set_param`, `sq_param_descriptors`) using the processor handle.

### Python API

```python
# Create a source with a plugin generator
synth = engine.add_source("Lead", plugin="Diva.vst3")

# Add plugin to a chain
eq = vocal.chain.append("EQ.vst3")

# Parameters accessed via processor handle (normalized 0-1 for plugins)
eq.set_param("high_gain", 0.4)
print(eq.get_param("high_gain"))       # e.g., 0.4
print(eq.get_param_text("high_gain"))  # e.g., "-3.0 dB"
print(eq.param_descriptors())
```

## Processing

`reset()` delegates to `processor_->reset()`, which clears the plugin's internal processing state (reverb tails, delay line contents, filter histories, gain reduction, etc.). Called by the Engine on the audio thread when un-bypassing — **must be RT-safe**. JUCE's `AudioProcessor::reset()` is expected to be RT-safe by convention.

`process()` delegates to the plugin's `processBlock()`:

1. Copy input MIDI to a working MIDI buffer (if `acceptsMidi_`)
2. Call `processor_->processBlock(buffer, midiBuffer)` — plugin processes audio in-place
3. If sidechain buffer is set, map it to the plugin's sidechain bus before processing

JUCE plugins process audio in-place. The `AudioBuffer` passed to `processBlock` serves as both input and output.

### Sidechain Support

Some plugins (compressors, gates) accept a sidechain input. This is a read-only auxiliary buffer that the plugin uses for detection while processing the main audio in-place.

```python
bass = engine.add_source("Bass", plugin="bass_synth.vst3")
kick = engine.add_source("Kick", sampler="kick.wav")

sc_comp = bass.chain.append("Compressor.vst3")
engine.sidechain(sc_comp, source=kick)
```

The Engine sets the sidechain buffer reference on the PluginProcessor before each process call. The buffer is read-only and does not participate in the in-place chain flow.

## Parameters

Parameters are exposed by their JUCE parameter name (`AudioProcessorParameter::getName()`). Values are **normalized 0.0–1.0** as required by the VST3/AU plugin specification. Use `getParameterText()` to get human-readable display values in real-world units.

- `getParameterCount()`: number of plugin parameters
- `getParameterDescriptors()`: returns name, default, min (0.0), max (1.0), and metadata for each parameter
- `getParameter(name)`: returns the current normalized value (0.0–1.0), or 0.0 if name unknown
- `setParameter(name, value)`: sets the normalized value (0.0–1.0), no-op if name unknown
- `getParameterText(name)`: returns the display text in real-world units (e.g., "440 Hz", "-12 dB")

Note: Built-in processors (GainProcessor, etc.) use real-world parameter ranges, not normalized 0–1. See Processor spec for details.

The parameter map (name → JUCE index) is built once at construction and is immutable.

## Invariants

- `process()` is RT-safe (delegates to the plugin's `processBlock`, which is expected to be RT-safe by the plugin vendor)
- `reset()` is RT-safe (delegates to the plugin's `reset()`, which clears internal state — reverb tails, delay buffers, filter histories)
- Channel configuration is fixed after construction
- `prepare()` is called before the first `process()` and after any sample rate or block size change
- `release()` is called before destruction
- PluginProcessor owns the `AudioProcessor` and destroys it on destruction
- The parameter map is immutable after construction
- `getParameter()` with unknown name returns 0.0f (not an error)
- `setParameter()` with unknown name is a no-op (not an error)
- `getLatencySamples()` delegates to the plugin's reported latency

## Error Conditions

- Constructor with null processor: undefined behavior (caller must validate — PluginManager handles this)
- `getParameter()` with unknown name: returns 0.0f
- `setParameter()` with unknown name: no-op, logged at debug level
- `getParameterText()` with unknown name: returns empty string
- Plugin's `processBlock` throws or crashes: not recoverable (plugin bug)

## Does NOT Handle

- **Plugin loading / instantiation** — PluginManager creates the AudioPluginInstance
- **Plugin scanning / cache management** — PluginManager
- **Plugin GUI / editor windows** — PluginEditorWindow (separate component)
- **Plugin state save / restore (presets)** — future
- **Sidechain buffer provision** — Engine maps sidechain sources to processor buffers
- **Parameter automation from EventScheduler** — Engine handles sub-block splitting and calls setParameter at split boundaries

## Dependencies

- Processor (base class)
- JUCE (`juce_audio_processors`: AudioProcessor, AudioPluginInstance, AudioProcessorParameter)
- JUCE (`juce_audio_basics`: AudioBuffer, MidiBuffer)

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| Constructor | Control | Builds parameter map, stores processor |
| `prepare()` / `release()` | Control | Delegates to processor |
| `reset()` | Audio | Delegates to processor->reset(); must be RT-safe |
| `process()` | Audio | Delegates to processor's processBlock |
| `getParameterDescriptors()` | Any | Immutable after construction |
| `getParameter()` | Control | Reads from JUCE parameter (atomic internally) |
| `setParameter()` | Control | Writes to JUCE parameter |
| `getParameterText()` | Control | Reads from JUCE parameter |
| `getPluginName()` / `getProcessor()` | Any | Immutable / stable pointer |
| `getLatencySamples()` | Control | Delegates to processor |
| `setSidechainBuffer()` | Control | Set before audio thread processes |

JUCE's parameter system handles the control-thread-write / audio-thread-read synchronization internally.

## Example Usage

### Created by PluginManager (typical path)

```cpp
auto instance = formatManager_.createPluginInstance(
    description, sampleRate, blockSize, errorMessage);

auto processor = std::make_unique<PluginProcessor>(
    std::move(instance),
    description.numOutputChannels,
    description.isInstrument);
```

### Used through Engine

```cpp
auto proc = pluginManager.createProcessor("Diva", 44100.0, 512, error);
auto* src = engine.addSource("Lead", std::move(proc));
// Source generator is now the plugin
// Parameters through Engine's generic interface
engine.setParameter(src->getGenerator()->getHandle(), "cutoff", 0.5f);  // normalized 0-1
```

### Unit testing with a mock processor

```cpp
auto mockProcessor = std::make_unique<MockAudioProcessor>();
auto proc = std::make_unique<PluginProcessor>(std::move(mockProcessor), 2, true);

proc->prepare(44100.0, 512);
juce::AudioBuffer<float> buffer(2, 512);
juce::MidiBuffer midi;
proc->process(buffer, midi);
proc->release();
```
