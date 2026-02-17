#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace squeeze {

/// Concrete juce::AudioProcessor for unit testing.
/// Configurable input/output channels and MIDI acceptance.
/// Has "Gain" and "Mix" parameters for testing parameter mapping.
/// Records state for test inspection.
class TestProcessor : public juce::AudioProcessor
{
public:
    TestProcessor(int numInputChannels, int numOutputChannels, bool midi)
        : juce::AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::canonicalChannelSet(
                  std::max(numInputChannels, 1)), numInputChannels > 0)
              .withOutput("Output", juce::AudioChannelSet::canonicalChannelSet(
                  std::max(numOutputChannels, 1)), true)),
          numInputChannels_(numInputChannels),
          numOutputChannels_(numOutputChannels),
          acceptsMidi_(midi)
    {
        addParameter(gainParam_ = new juce::AudioParameterFloat(
            {"gain", 1}, "Gain", 0.0f, 1.0f, 1.0f));
        addParameter(mixParam_ = new juce::AudioParameterFloat(
            {"mix", 1}, "Mix", 0.0f, 1.0f, 0.5f));
    }

    const juce::String getName() const override { return "TestProcessor"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        preparedSampleRate = sampleRate;
        preparedBlockSize = samplesPerBlock;
    }

    void releaseResources() override
    {
        preparedSampleRate = 0.0;
        preparedBlockSize = 0;
    }

    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& /*midiMessages*/) override
    {
        processBlockCalled = true;
        lastBlockSize = buffer.getNumSamples();

        // Apply gain parameter to all channels
        float g = gainParam_->get();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.applyGain(ch, 0, buffer.getNumSamples(), g);
    }

    bool acceptsMidi() const override { return acceptsMidi_; }
    bool producesMidi() const override { return acceptsMidi_; }
    double getTailLengthSeconds() const override { return 0.0; }

    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    // --- Test inspection state ---
    bool processBlockCalled = false;
    int lastBlockSize = 0;
    double preparedSampleRate = 0.0;
    int preparedBlockSize = 0;

    int getNumInputChannels_() const { return numInputChannels_; }
    int getNumOutputChannels_() const { return numOutputChannels_; }

private:
    int numInputChannels_;
    int numOutputChannels_;
    bool acceptsMidi_;
    juce::AudioParameterFloat* gainParam_ = nullptr;  // owned by AudioProcessor
    juce::AudioParameterFloat* mixParam_ = nullptr;    // owned by AudioProcessor

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TestProcessor)
};

} // namespace squeeze
