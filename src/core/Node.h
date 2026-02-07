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
    int index;
    float defaultValue;
    int numSteps;       // 0 = continuous
    bool automatable;
    bool boolean;
    std::string label;  // unit: "dB", "Hz", "%"
    std::string group;  // "" = ungrouped
};

class Node {
public:
    virtual ~Node() = default;

    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void process(ProcessContext& context) = 0;
    virtual void release() = 0;

    virtual std::vector<PortDescriptor> getInputPorts() const = 0;
    virtual std::vector<PortDescriptor> getOutputPorts() const = 0;

    // Parameter interface (index-based virtuals)
    virtual std::vector<ParameterDescriptor> getParameterDescriptors() const { return {}; }
    virtual float getParameter(int index) const { return 0.0f; }
    virtual void setParameter(int index, float value) { (void)index; (void)value; }
    virtual std::string getParameterText(int index) const { (void)index; return ""; }

    // Virtual so PluginNode can override with O(1) map lookup
    virtual int findParameterIndex(const std::string& name) const
    {
        auto descs = getParameterDescriptors();
        for (const auto& d : descs)
        {
            if (d.name == name)
                return d.index;
        }
        return -1;
    }

    // Non-virtual name-based convenience
    float getParameterByName(const std::string& name) const
    {
        int idx = findParameterIndex(name);
        if (idx < 0) return 0.0f;
        return getParameter(idx);
    }

    bool setParameterByName(const std::string& name, float value)
    {
        int idx = findParameterIndex(name);
        if (idx < 0) return false;
        setParameter(idx, value);
        return true;
    }
};

} // namespace squeeze
