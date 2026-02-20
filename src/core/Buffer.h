#pragma once

#include "core/Logger.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <memory>
#include <string>

namespace squeeze {

class Buffer {
public:
    /// Create a zeroed buffer for recording or programmatic use.
    /// Returns nullptr for invalid parameters.
    static std::unique_ptr<Buffer> createEmpty(
        int numChannels, int lengthInSamples, double sampleRate,
        const std::string& name = "");

    /// Create a buffer from existing audio data (used by BufferLibrary after file load).
    /// Takes ownership of `data` via move. Returns nullptr for invalid parameters.
    static std::unique_ptr<Buffer> createFromData(
        juce::AudioBuffer<float>&& data, double sampleRate,
        const std::string& name, const std::string& filePath = "");

    ~Buffer();

    // Non-copyable, non-moveable (managed via unique_ptr; contains atomic)
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) = delete;
    Buffer& operator=(Buffer&&) = delete;

    // --- Audio data access (RT-safe, lock-free) ---

    /// Returns read pointer for channel, or nullptr if channel is out of range.
    const float* getReadPointer(int channel) const;

    /// Returns write pointer for channel, or nullptr if channel is out of range.
    float* getWritePointer(int channel);

    // --- Metadata (immutable after construction) ---

    int getNumChannels() const;
    int getLengthInSamples() const;
    double getSampleRate() const;
    double getLengthInSeconds() const;
    const std::string& getName() const;
    const std::string& getFilePath() const;

    // --- Recording ---

    /// Current write position (samples from buffer start).
    /// Audio thread stores with release; control thread loads with acquire.
    std::atomic<int> writePosition{0};

    /// Zero all sample data and reset writePosition to 0. Control thread only.
    void clear();

private:
    Buffer();

    juce::AudioBuffer<float> data_;
    double sampleRate_ = 0.0;
    std::string name_;
    std::string filePath_;
};

} // namespace squeeze
