#pragma once

#include "core/Node.h"

namespace squeeze {

class OutputNode : public Node {
public:
    void prepare(double /*sampleRate*/, int /*blockSize*/) override {}
    void release() override {}

    void process(ProcessContext& /*context*/) override {}

    std::vector<PortDescriptor> getInputPorts() const override
    {
        return {{"in", PortDirection::input, SignalType::audio, 2}};
    }

    std::vector<PortDescriptor> getOutputPorts() const override
    {
        return {};
    }
};

} // namespace squeeze
