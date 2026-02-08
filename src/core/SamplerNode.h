#pragma once

#include "core/Node.h"
#include "core/VoiceAllocator.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace squeeze {

class SamplerNode : public Node {
public:
    explicit SamplerNode(int maxVoices = 1);

    void prepare(double sampleRate, int blockSize) override;
    void process(ProcessContext& context) override;
    void release() override;
    std::vector<PortDescriptor> getInputPorts() const override;
    std::vector<PortDescriptor> getOutputPorts() const override;

    std::vector<ParameterDescriptor> getParameterDescriptors() const override;
    float getParameter(int index) const override;
    void setParameter(int index, float value) override;
    std::string getParameterText(int index) const override;
    int findParameterIndex(const std::string& name) const override;

    void setBuffer(const Buffer* buffer);
    const Buffer* getBuffer() const;

private:
    static constexpr int kNumParams = 32;

    SamplerParams params_;
    VoiceAllocator allocator_;
    const Buffer* buffer_ = nullptr;
    float normalizedParams_[kNumParams] = {};
    std::unordered_map<std::string, int> paramNameToIndex_;
};

} // namespace squeeze
