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

} // namespace squeeze
