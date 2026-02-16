#pragma once

#include "core/Node.h"

#include <string>

namespace squeeze {

class GainNode : public Node {
public:
    void prepare(double /*sampleRate*/, int /*blockSize*/) override {}
    void release() override {}

    void process(ProcessContext& ctx) override
    {
        for (int ch = 0; ch < ctx.outputAudio.getNumChannels(); ++ch)
        {
            ctx.outputAudio.copyFrom(ch, 0, ctx.inputAudio, ch, 0, ctx.numSamples);
            ctx.outputAudio.applyGain(ch, 0, ctx.numSamples, gain_);
        }
    }

    std::vector<PortDescriptor> getInputPorts() const override
    {
        return {{"in", PortDirection::input, SignalType::audio, 2}};
    }

    std::vector<PortDescriptor> getOutputPorts() const override
    {
        return {{"out", PortDirection::output, SignalType::audio, 2}};
    }

    std::vector<ParameterDescriptor> getParameterDescriptors() const override
    {
        return {{"gain", 1.0f, 0, true, false, "", ""}};
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
