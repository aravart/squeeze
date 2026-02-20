#pragma once

#include "core/Processor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace squeeze {

/// Processor subclass that wraps a juce::AudioProcessor (VST/AU plugin or test processor).
/// Constructed with an already-instantiated processor and explicit channel/MIDI config.
class PluginProcessor : public Processor {
public:
    explicit PluginProcessor(std::unique_ptr<juce::AudioProcessor> processor,
                             int inputChannels, int outputChannels, bool acceptsMidi);
    ~PluginProcessor() override;

    // --- Lifecycle (control thread) ---
    void prepare(double sampleRate, int blockSize) override;
    void reset() override;
    void release() override;

    // --- Processing (audio thread, RT-safe) ---
    void process(juce::AudioBuffer<float>& buffer) override;
    void process(juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) override;

    // --- Parameters ---
    int getParameterCount() const override;
    ParamDescriptor getParameterDescriptor(int index) const override;
    std::vector<ParamDescriptor> getParameterDescriptors() const override;
    float getParameter(const std::string& name) const override;
    void setParameter(const std::string& name, float value) override;
    std::string getParameterText(const std::string& name) const override;

    // --- Latency ---
    int getLatencySamples() const override;

    // --- PlayHead ---
    void setPlayHead(juce::AudioPlayHead* playHead) override;

    // --- Accessors ---
    const std::string& getPluginName() const;
    juce::AudioProcessor* getJuceProcessor();

    bool hasMidi() const { return acceptsMidi_; }
    int getInputChannels() const { return inputChannels_; }
    int getOutputChannels() const { return outputChannels_; }

private:
    std::unique_ptr<juce::AudioProcessor> processor_;
    int inputChannels_;
    int outputChannels_;
    bool acceptsMidi_;
    std::string pluginName_;
    std::unordered_map<std::string, int> parameterMap_;
    juce::MidiBuffer tempMidi_;

    void buildParameterMap();
};

} // namespace squeeze
