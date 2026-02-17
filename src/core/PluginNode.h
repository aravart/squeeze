#pragma once

#include "core/Node.h"
#include "core/Logger.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace squeeze {

/// Node subclass that wraps a juce::AudioProcessor (VST/AU plugin or test processor).
/// Constructed with an already-instantiated processor and explicit channel/MIDI config.
class PluginNode : public Node {
public:
    /// Takes ownership of the processor. inputChannels/outputChannels/acceptsMidi
    /// define the port layout (may differ from processor's internal bus layout).
    explicit PluginNode(std::unique_ptr<juce::AudioProcessor> processor,
                        int inputChannels, int outputChannels, bool acceptsMidi);
    ~PluginNode() override;

    // --- Lifecycle (control thread) ---
    void prepare(double sampleRate, int blockSize) override;
    void release() override;

    // --- Processing (audio thread, RT-safe) ---
    void process(ProcessContext& context) override;

    // --- Port declaration ---
    std::vector<PortDescriptor> getInputPorts() const override;
    std::vector<PortDescriptor> getOutputPorts() const override;

    // --- Parameters ---
    std::vector<ParameterDescriptor> getParameterDescriptors() const override;
    float getParameter(const std::string& name) const override;
    void setParameter(const std::string& name, float value) override;
    std::string getParameterText(const std::string& name) const override;

    // --- Accessors ---
    const std::string& getPluginName() const;
    juce::AudioProcessor* getProcessor();

private:
    std::unique_ptr<juce::AudioProcessor> processor_;
    int inputChannels_;
    int outputChannels_;
    bool acceptsMidi_;
    std::string pluginName_;
    std::unordered_map<std::string, int> parameterMap_;

    void buildParameterMap();
};

} // namespace squeeze
