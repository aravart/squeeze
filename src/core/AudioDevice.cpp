#include "core/AudioDevice.h"
#include "core/Logger.h"

namespace squeeze {

AudioDevice::AudioDevice(Engine& engine)
    : engine_(engine)
{
    SQ_INFO("AudioDevice: created");
}

AudioDevice::~AudioDevice()
{
    stop();
    SQ_INFO("AudioDevice: destroyed");
}

// ═══════════════════════════════════════════════════════════════════
// Control thread
// ═══════════════════════════════════════════════════════════════════

bool AudioDevice::start(double sampleRate, int blockSize, std::string& error)
{
    SQ_INFO("AudioDevice::start: requested sr=%.0f bs=%d", sampleRate, blockSize);

    if (running_.load())
    {
        SQ_INFO("AudioDevice::start: already running, stopping first");
        stop();
    }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.sampleRate = sampleRate;
    setup.bufferSize = blockSize;

    auto err = deviceManager_.initialise(0, 2, nullptr, true, {}, &setup);
    if (err.isNotEmpty())
    {
        error = err.toStdString();
        SQ_WARN("AudioDevice::start: initialise failed: %s", error.c_str());
        return false;
    }

    deviceManager_.addAudioCallback(this);

    SQ_INFO("AudioDevice::start: device opened, actual sr=%.0f bs=%d",
            sampleRate_, blockSize_);
    return true;
}

void AudioDevice::stop()
{
    if (!running_.load())
        return;

    SQ_INFO("AudioDevice::stop");
    deviceManager_.removeAudioCallback(this);
    deviceManager_.closeAudioDevice();
    running_.store(false);
    sampleRate_ = 0.0;
    blockSize_ = 0;
}

bool AudioDevice::isRunning() const
{
    return running_.load();
}

double AudioDevice::getSampleRate() const
{
    return running_.load() ? sampleRate_ : 0.0;
}

int AudioDevice::getBlockSize() const
{
    return running_.load() ? blockSize_ : 0;
}

// ═══════════════════════════════════════════════════════════════════
// JUCE AudioIODeviceCallback
// ═══════════════════════════════════════════════════════════════════

void AudioDevice::audioDeviceIOCallbackWithContext(
    const float* const* /*inputChannelData*/, int /*numInputChannels*/,
    float* const* outputChannelData, int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    engine_.processBlock(outputChannelData, numOutputChannels, numSamples);
}

void AudioDevice::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    double sr = device->getCurrentSampleRate();
    int bs = device->getCurrentBufferSizeSamples();

    SQ_INFO("AudioDevice::audioDeviceAboutToStart: sr=%.0f bs=%d", sr, bs);

    if (sr != engine_.getSampleRate())
        SQ_WARN("AudioDevice: device SR %.0f differs from engine SR %.0f",
                sr, engine_.getSampleRate());

    sampleRate_ = sr;
    blockSize_ = bs;
    running_.store(true);
}

void AudioDevice::audioDeviceStopped()
{
    SQ_INFO("AudioDevice::audioDeviceStopped");
    running_.store(false);
    sampleRate_ = 0.0;
    blockSize_ = 0;
}

} // namespace squeeze
