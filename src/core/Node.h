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

struct ParameterDescriptor {
    std::string name;
    float defaultValue;
    int numSteps;       // 0 = continuous, >0 = stepped
    bool automatable;
    bool boolean;
    std::string label;  // unit: "dB", "Hz", "%", ""
    std::string group;  // "" = ungrouped
};

class Node {
public:
    virtual ~Node() = default;

    // --- Lifecycle (control thread) ---
    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void release() = 0;

    // --- Processing (audio thread, RT-safe) ---
    virtual void process(ProcessContext& context) = 0;

    // --- Port declaration ---
    virtual std::vector<PortDescriptor> getInputPorts() const = 0;
    virtual std::vector<PortDescriptor> getOutputPorts() const = 0;

    // --- Parameters (string-based) ---
    virtual std::vector<ParameterDescriptor> getParameterDescriptors() const { return {}; }
    virtual float getParameter(const std::string& name) const { (void)name; return 0.0f; }
    virtual void setParameter(const std::string& name, float value) { (void)name; (void)value; }
    virtual std::string getParameterText(const std::string& name) const { (void)name; return ""; }
};

} // namespace squeeze
