#pragma once

#include "core/Processor.h"
#include "core/PlaybackCursor.h"

#include <atomic>
#include <string>
#include <vector>

namespace squeeze {

class PlayerProcessor : public Processor {
public:
    PlayerProcessor();
    ~PlayerProcessor() override;

    // Processor interface
    void prepare(double sampleRate, int blockSize) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void release() override;
    void reset() override;

    // Parameters (string-based, inherited from Processor)
    int getParameterCount() const override;
    ParamDescriptor getParameterDescriptor(int index) const override;
    std::vector<ParamDescriptor> getParameterDescriptors() const override;
    float getParameter(const std::string& name) const override;
    void setParameter(const std::string& name, float value) override;
    std::string getParameterText(const std::string& name) const override;
    int getLatencySamples() const override;

    // Buffer assignment (control thread)
    void setBuffer(const Buffer* buffer);
    const Buffer* getBuffer() const;

    using Processor::process;

private:
    PlaybackCursor cursor_;

    // Parameters
    float playing_ = 0.0f;
    float speed_ = 1.0f;
    float loopMode_ = 0.0f;
    float loopStart_ = 0.0f;
    float loopEnd_ = 1.0f;
    float fadeMs_ = 5.0f;

    // Buffer pointer (atomic for cross-thread visibility)
    std::atomic<const Buffer*> buffer_{nullptr};

    // Seek via parameter write
    std::atomic<bool> seekPending_{false};
    std::atomic<float> seekTarget_{0.0f};

    // Audio thread state
    double sampleRate_ = 44100.0;
    bool wasPlaying_ = false;
    float fadeGain_ = 0.0f;

    double fadeSamplesFromMs() const;

    static constexpr int kParamCount = 7;
    static const ParamDescriptor kDescriptors[kParamCount];
};

} // namespace squeeze
