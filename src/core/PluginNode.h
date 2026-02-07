#pragma once

#include "core/Node.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace squeeze {

class PluginNode : public Node {
public:
    // Takes ownership of a processor (AudioPluginInstance or test processor).
    explicit PluginNode(std::unique_ptr<juce::AudioProcessor> processor,
                        int inputChannels, int outputChannels,
                        bool acceptsMidi);

    // Factory: look up a plugin by description and instantiate it.
    // Must be called on control thread. Returns nullptr on failure.
    static std::unique_ptr<PluginNode> create(
        const juce::PluginDescription& description,
        juce::AudioPluginFormatManager& formatManager,
        double sampleRate, int blockSize,
        juce::String& errorMessage);

    // Node interface
    void prepare(double sampleRate, int blockSize) override;
    void process(ProcessContext& context) override;
    void release() override;

    std::vector<PortDescriptor> getInputPorts() const override;
    std::vector<PortDescriptor> getOutputPorts() const override;

    // Parameter interface (index-based)
    std::vector<ParameterDescriptor> getParameterDescriptors() const override;
    float getParameter(int index) const override;
    void setParameter(int index, float value) override;
    std::string getParameterText(int index) const override;
    int findParameterIndex(const std::string& name) const override;

    // Plugin-specific queries
    const juce::String& getName() const;
    juce::AudioProcessor* getProcessor();
    juce::AudioPluginInstance* getPluginInstance();

private:
    std::unique_ptr<juce::AudioProcessor> processor_;
    juce::String name_;
    int inputChannels_;
    int outputChannels_;
    bool acceptsMidi_;

    // Parameter name -> index in getParameters() array
    std::unordered_map<std::string, int> paramNameToIndex_;
    void buildParameterMap();
};

} // namespace squeeze
