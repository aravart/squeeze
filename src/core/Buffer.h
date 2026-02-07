#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>
#include <string>

namespace squeeze {

class Buffer {
public:
    // -- Factory methods (control thread only) --

    /// Load audio file into RAM. Returns nullptr on failure, sets errorMessage.
    static std::unique_ptr<Buffer> loadFromFile(
        const std::string& filePath,
        juce::AudioFormatManager& formatManager,
        std::string& errorMessage);

    /// Create empty buffer, zeroed. For recording.
    static std::unique_ptr<Buffer> createEmpty(
        int numChannels, int lengthInSamples, double sampleRate,
        const std::string& name = "");

    // -- Audio data access (audio thread safe) --

    const juce::AudioBuffer<float>& getAudioData() const;
    juce::AudioBuffer<float>& getAudioData();

    const float* getReadPointer(int channel) const;
    float* getWritePointer(int channel);

    // -- Metadata (immutable after construction) --

    int getNumChannels() const;
    int getLengthInSamples() const;
    double getSampleRate() const;
    double getLengthInSeconds() const;
    const std::string& getName() const;
    const std::string& getFilePath() const;

    // -- Recording support --

    /// Atomic write position for recording (samples from buffer start).
    std::atomic<int> writePosition{0};

    /// Clear all samples to zero (control thread only).
    void clear();

    /// Resize buffer (control thread only, NOT audio-thread safe).
    /// Preserves existing data up to min(old, new) length.
    void resize(int numChannels, int newLengthInSamples);

private:
    Buffer() = default;

    juce::AudioBuffer<float> data_;
    double sampleRate_ = 0.0;
    std::string name_;
    std::string filePath_;
};

} // namespace squeeze
