# PluginNode Specification

## Overview

PluginNode wraps a JUCE AudioPluginInstance to host VST3 (and AU) plugins as nodes in the audio graph. Plugins are loaded from a pre-existing cache file (`plugin-cache.xml`) — no scanning is performed. A companion `PluginCache` class handles reading and querying the cache.

## PluginCache

A lightweight helper that reads a JUCE KnownPluginList XML file and provides lookup by plugin name.

```cpp
class PluginCache {
public:
    bool loadFromFile(const juce::File& xmlFile);
    bool loadFromXml(const juce::String& xmlString);

    const juce::PluginDescription* findByName(const juce::String& name) const;
    std::vector<juce::String> getAvailablePluginNames() const;
    int getNumPlugins() const;
};
```

### PluginCache Responsibilities

- Parse JUCE KnownPluginList XML into a `juce::KnownPluginList`
- Provide exact-match lookup by plugin name (case-sensitive)
- Report number of loaded plugins and their names
- Never scan for plugins

### PluginCache Invariants

- `loadFromFile` / `loadFromXml` can be called multiple times (replaces previous list)
- `findByName` returns nullptr if no match found
- After a successful load, `getNumPlugins() > 0`
- Thread-safe for concurrent reads after loading is complete

### PluginCache Error Conditions

- File does not exist: `loadFromFile` returns false, list is empty
- Malformed XML: `loadFromFile` / `loadFromXml` returns false, list is empty
- Plugin name not found: `findByName` returns nullptr

## PluginNode

### Responsibilities

- Wrap a `juce::AudioPluginInstance` and implement the Node interface
- Declare ports based on the plugin's channel configuration
- Forward `process()` to the plugin's `processBlock()`
- Expose the plugin's parameters via the Node parameter interface

### Interface

```cpp
class PluginNode : public Node {
public:
    // Takes ownership of a processor (AudioPluginInstance or test processor).
    // inputChannels/outputChannels are used for port declarations.
    explicit PluginNode(std::unique_ptr<juce::AudioProcessor> processor,
                        int inputChannels, int outputChannels,
                        bool acceptsMidi);

    // Factory: look up a plugin by description and instantiate it.
    // Must be called on control thread. Returns nullptr on failure.
    static std::unique_ptr<PluginNode> create(
        const juce::PluginDescription& description,
        juce::AudioPluginFormatManager& formatManager,
        double sampleRate, int blockSize,
        juce::String& errorMessage);

    // Node interface
    void prepare(double sampleRate, int blockSize) override;
    void process(ProcessContext& context) override;
    void release() override;

    std::vector<PortDescriptor> getInputPorts() const override;
    std::vector<PortDescriptor> getOutputPorts() const override;

    std::vector<std::string> getParameterNames() const override;
    float getParameter(const std::string& name) const override;
    void setParameter(const std::string& name, float value) override;

    // Plugin-specific queries
    const juce::String& getName() const;
    juce::AudioProcessor* getProcessor();
    juce::AudioPluginInstance* getPluginInstance();  // dynamic_cast, nullptr for test processors
};
```

### Port Declaration

Ports are determined at construction from the plugin's channel configuration:

- **Audio input**: If `inputChannels > 0`, declares one audio input port named `"in"` with that many channels
- **Audio output**: If `outputChannels > 0`, declares one audio output port named `"out"` with that many channels
- **MIDI input**: If `acceptsMidi` is true, declares one MIDI input port named `"midi_in"`
- **MIDI output**: Always declares one MIDI output port named `"midi_out"` (for MIDI pass-through or generated MIDI)

Typical configurations:
- **Instrument** (e.g., B-3 V2): MIDI in, audio out (2ch). No audio in.
- **Effect** (e.g., Chorus DIMENSION-D): Audio in (2ch), audio out (2ch). May have MIDI in.

### Processing (process)

1. If plugin has audio inputs: copy `inputAudio` → `outputAudio` (channel-wise, up to the lesser of input/output channels)
2. If plugin has no audio inputs (instrument): clear `outputAudio`
3. Copy `inputMidi` → `outputMidi` (so the plugin can read from it; JUCE processes MIDI in-place)
4. Call `instance->processBlock(outputAudio, outputMidi)`

The plugin processes audio in-place in `outputAudio` and MIDI in-place in `outputMidi`.

### Parameters

Parameters are exposed by their JUCE parameter name. Values are normalized 0.0–1.0 as JUCE provides them.

- `getParameterNames()`: returns all parameter names from the plugin
- `getParameter(name)`: returns the current normalized value, or 0.0 if name unknown
- `setParameter(name, value)`: sets the normalized value, no-op if name unknown

### Invariants

- `process()` is realtime-safe (delegates to the plugin's `processBlock`, which is expected to be RT-safe)
- Port configuration is fixed after construction
- `prepare()` is called before the first `process()`
- `release()` is called before destruction
- The PluginNode owns the `AudioPluginInstance` and destroys it on destruction

### Error Conditions

- `create()` returns nullptr if plugin file is missing, format unsupported, or instantiation fails
- `create()` writes the error description to `errorMessage`
- `getParameter()` with unknown name returns 0.0f
- `setParameter()` with unknown name is a no-op

### Does NOT Handle

- Plugin scanning (uses pre-existing cache)
- Plugin GUI / editor
- Plugin state save/restore (presets) — future milestone
- Multi-bus routing (single input/output bus for this milestone)
- Side-chain inputs

## Dependencies

- Node (base class)
- Port (PortDescriptor, PortDirection, SignalType)
- JUCE (AudioPluginFormatManager, AudioPluginInstance, KnownPluginList, PluginDescription, AudioBuffer, MidiBuffer)

## Thread Safety

- `PluginCache::loadFromFile` / `loadFromXml`: control thread only
- `PluginCache::findByName`, `getAvailablePluginNames`, `getNumPlugins`: safe from any thread after loading
- `PluginNode::create()`: control thread only (allocates, does I/O)
- `PluginNode::prepare()`, `release()`: control thread only
- `PluginNode::process()`: audio thread only
- `PluginNode::getInputPorts()`, `getOutputPorts()`: any thread (immutable)
- `PluginNode::getParameter()`, `setParameter()`: control thread (audio-thread reads happen inside `process()`)

## Example Usage

```cpp
// Load the plugin cache
PluginCache cache;
cache.loadFromFile(juce::File("plugin-cache.xml"));

// Set up JUCE format manager
juce::AudioPluginFormatManager formatManager;
formatManager.addDefaultFormats();

// Find and create a plugin
auto* desc = cache.findByName("Pigments");
if (desc) {
    juce::String error;
    auto node = PluginNode::create(*desc, formatManager, 44100.0, 512, error);
    if (node) {
        // Add to graph
        int id = graph.addNode(std::move(node));
        graph.connect({midiSource, PortDirection::output, "midi_out"},
                      {id, PortDirection::input, "midi_in"});
        graph.connect({id, PortDirection::output, "out"},
                      {audioOut, PortDirection::input, "in"});
    }
}
```
