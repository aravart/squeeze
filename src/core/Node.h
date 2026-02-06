#pragma once

#include "core/Port.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <string>
#include <vector>

namespace squeeze {

struct ProcessContext {
    juce::AudioBuffer<float>& inputAudio;
    juce::AudioBuffer<float>& outputAudio;
    juce::MidiBuffer& inputMidi;
    juce::MidiBuffer& outputMidi;
    int numSamples;
};

class Node {
public:
    virtual ~Node() = default;

    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void process(ProcessContext& context) = 0;
    virtual void release() = 0;

    virtual std::vector<PortDescriptor> getInputPorts() const = 0;
    virtual std::vector<PortDescriptor> getOutputPorts() const = 0;

    virtual std::vector<std::string> getParameterNames() const { return {}; }
    virtual float getParameter(const std::string& name) const { return 0.0f; }
    virtual void setParameter(const std::string& name, float value) {}
    virtual void setParameterByIndex(int index, float value) {}
};

} // namespace squeeze
