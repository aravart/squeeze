#pragma once

#include "core/Chain.h"
#include "core/Types.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <string>
#include <vector>

namespace squeeze {

class Bus {
public:
    Bus(const std::string& name, bool isMaster = false);
    ~Bus();

    // Non-copyable, non-movable
    Bus(const Bus&) = delete;
    Bus& operator=(const Bus&) = delete;

    // --- Lifecycle (control thread) ---
    void prepare(double sampleRate, int blockSize);
    void release();

    // --- Identity ---
    const std::string& getName() const;
    int getHandle() const;
    void setHandle(int h);
    bool isMaster() const;

    // --- Insert chain ---
    Chain& getChain();
    const Chain& getChain() const;

    // --- Gain and Pan (control thread write, audio thread read) ---
    void setGain(float linear);
    float getGain() const;
    void setPan(float pan);
    float getPan() const;

    // --- Bus routing (control thread) ---
    void routeTo(Bus* bus);
    Bus* getOutputBus() const;

    // --- Sends (control thread) ---
    int addSend(Bus* bus, float levelDb, SendTap tap = SendTap::postFader);
    bool removeSend(int sendId);
    void setSendLevel(int sendId, float levelDb);
    void setSendTap(int sendId, SendTap tap);
    std::vector<Send> getSends() const;

    // --- Bypass (control thread write, audio thread read) ---
    void setBypassed(bool bypassed);
    bool isBypassed() const;

    // --- Metering (audio thread writes, any thread reads) ---
    float getPeak() const;
    float getRMS() const;
    void updateMetering(const juce::AudioBuffer<float>& buffer, int numSamples);
    void resetMetering();

    // --- Latency ---
    int getLatencySamples() const;

private:
    std::string name_;
    int handle_ = -1;
    bool master_;
    Chain chain_;
    std::atomic<float> gain_{1.0f};
    std::atomic<float> pan_{0.0f};
    Bus* outputBus_ = nullptr;
    std::vector<Send> sends_;
    std::atomic<bool> bypassed_{false};
    int nextSendId_ = 1;

    // Metering
    std::atomic<float> peak_{0.0f};
    std::atomic<float> rms_{0.0f};
};

} // namespace squeeze
