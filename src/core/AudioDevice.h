#pragma once

#include "core/Engine.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <string>

namespace squeeze {

/// Bridge between JUCE audio device system and Engine.
/// Owns juce::AudioDeviceManager, forwards audio callback to Engine::processBlock().
class AudioDevice : public juce::AudioIODeviceCallback {
public:
    explicit AudioDevice(Engine& engine);
    ~AudioDevice() override;

    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    // --- Control thread ---
    bool start(double sampleRate, int blockSize, std::string& error);
    void stop();
    bool isRunning() const;
    double getSampleRate() const;
    int getBlockSize() const;

    // --- JUCE AudioIODeviceCallback (audio thread) ---
    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    Engine& engine_;
    juce::AudioDeviceManager deviceManager_;
    std::atomic<bool> running_{false};
    double sampleRate_ = 0.0;
    int blockSize_ = 0;
};

} // namespace squeeze
