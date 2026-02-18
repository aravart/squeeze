#pragma once

#include "core/Processor.h"

#include <string>
#include <vector>

namespace squeeze {

class GainProcessor : public Processor {
public:
    GainProcessor() : Processor("Gain") {}

    using Processor::process;

    void prepare(double /*sampleRate*/, int /*blockSize*/) override {}

    void process(juce::AudioBuffer<float>& buffer) override
    {
        buffer.applyGain(gain_);
    }

    int getParameterCount() const override { return 1; }

    std::vector<ParamDescriptor> getParameterDescriptors() const override
    {
        return {{"gain", 1.0f, 0.0f, 1.0f, 0, true, false, "", ""}};
    }

    float getParameter(const std::string& name) const override
    {
        if (name == "gain") return gain_;
        return 0.0f;
    }

    void setParameter(const std::string& name, float value) override
    {
        if (name == "gain") gain_ = value;
    }

    std::string getParameterText(const std::string& name) const override
    {
        if (name == "gain") return std::to_string(gain_);
        return "";
    }

private:
    float gain_ = 1.0f;
};

/// Simple Processor that writes a constant signal. For testing only.
class ConstGenerator : public Processor {
public:
    explicit ConstGenerator(float level = 0.5f)
        : Processor("ConstGenerator"), level_(level) {}

    using Processor::process;

    void prepare(double /*sampleRate*/, int /*blockSize*/) override {}

    void process(juce::AudioBuffer<float>& buffer) override
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample(ch, i, level_);
    }

    int getParameterCount() const override { return 1; }

    std::vector<ParamDescriptor> getParameterDescriptors() const override
    {
        return {{"level", 0.5f, 0.0f, 1.0f, 0, true, false, "", ""}};
    }

    float getParameter(const std::string& name) const override
    {
        if (name == "level") return level_;
        return 0.0f;
    }

    void setParameter(const std::string& name, float value) override
    {
        if (name == "level") level_ = value;
    }

    std::string getParameterText(const std::string& name) const override
    {
        if (name == "level") return std::to_string(level_);
        return "";
    }

private:
    float level_;
};

} // namespace squeeze
