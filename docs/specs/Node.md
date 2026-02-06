# Node Specification

## Overview

Node is the abstract interface for anything that processes audio in the graph. Port is a lightweight struct describing a connection point on a node, defined alongside Node rather than as a separate component.

## Port

```cpp
struct Port {
    enum class Direction { input, output };
    enum class SignalType { audio, midi };

    std::string name;
    Direction direction;
    SignalType signalType;
    int channels;  // for audio (1=mono, 2=stereo, N); ignored for midi
};
```

## ProcessContext

Engine provides buffers to each node via a context struct. For this milestone, a node receives one audio input buffer, one audio output buffer, one MIDI input buffer, and one MIDI output buffer. Multi-port routing (e.g., sidechain) is deferred.

```cpp
struct ProcessContext {
    juce::AudioBuffer<float>& inputAudio;
    juce::AudioBuffer<float>& outputAudio;
    juce::MidiBuffer& inputMidi;
    juce::MidiBuffer& outputMidi;
    int numSamples;
};
```

## Responsibilities

- Declare input and output ports
- Process audio and MIDI data provided by Engine
- Expose parameters by name

## Interface

```cpp
class Node {
public:
    virtual ~Node() = default;

    // Lifecycle
    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void process(ProcessContext& context) = 0;
    virtual void release() = 0;

    // Port declaration (static after construction)
    virtual std::vector<Port> getInputPorts() const = 0;
    virtual std::vector<Port> getOutputPorts() const = 0;

    // Parameters
    virtual std::vector<std::string> getParameterNames() const { return {}; }
    virtual float getParameter(const std::string& name) const { return 0.0f; }
    virtual void setParameter(const std::string& name, float value) {}
};
```

## Invariants

- `process()` is realtime-safe: no allocation, no blocking, no unbounded work
- Port configuration is fixed after construction — `getInputPorts()` and `getOutputPorts()` always return the same values
- `prepare()` is called before the first `process()` call
- `release()` is called before destruction
- Node does not own or allocate its processing buffers

## Error Conditions

- `getParameter()` with an unknown name returns 0.0f
- `setParameter()` with an unknown name is a no-op
- `process()` called before `prepare()` is undefined behavior (caller's responsibility)

## Does NOT Handle

- Buffer allocation (Engine)
- Position in execution order (Graph)
- Connection logic (Graph)
- Thread safety of mutations (Scheduler)

## Dependencies

- JUCE (`juce::AudioBuffer<float>`, `juce::MidiBuffer`) for buffer types

## Thread Safety

- `prepare()` and `release()` are called from the control thread
- `process()` is called from the audio thread
- `getInputPorts()` / `getOutputPorts()` are safe from any thread (immutable after construction)
- `getParameter()` / `setParameter()` are called from the control thread; audio-thread parameter reads happen inside `process()` via the node's own internal state

## Example Usage

```cpp
// A simple gain node
class GainNode : public Node {
    float gain = 1.0f;
    double sampleRate = 0;
    int blockSize = 0;

public:
    void prepare(double sr, int bs) override {
        sampleRate = sr;
        blockSize = bs;
    }

    void process(ProcessContext& ctx) override {
        for (int ch = 0; ch < ctx.outputAudio.getNumChannels(); ++ch) {
            ctx.outputAudio.copyFrom(ch, 0, ctx.inputAudio, ch, 0, ctx.numSamples);
            ctx.outputAudio.applyGain(ch, 0, ctx.numSamples, gain);
        }
    }

    void release() override {}

    std::vector<Port> getInputPorts() const override {
        return {{ "in", Port::Direction::input, Port::SignalType::audio, 2 }};
    }

    std::vector<Port> getOutputPorts() const override {
        return {{ "out", Port::Direction::output, Port::SignalType::audio, 2 }};
    }

    std::vector<std::string> getParameterNames() const override { return { "gain" }; }
    float getParameter(const std::string& name) const override { return gain; }
    void setParameter(const std::string& name, float value) override { gain = value; }
};
```
