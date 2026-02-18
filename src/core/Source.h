#pragma once

#include "core/Processor.h"
#include "core/Chain.h"
#include "core/Types.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace squeeze {

class Source {
public:
    Source(const std::string& name, std::unique_ptr<Processor> generator);
    ~Source();

    // Non-copyable, non-movable
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;

    // --- Lifecycle (control thread) ---
    void prepare(double sampleRate, int blockSize);
    void release();

    // --- Identity ---
    const std::string& getName() const;
    int getHandle() const;
    void setHandle(int h);

    // --- Generator ---
    Processor* getGenerator() const;
    void setGenerator(std::unique_ptr<Processor> generator);

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

    // --- MIDI assignment (control thread) ---
    void setMidiAssignment(const MidiAssignment& assignment);
    MidiAssignment getMidiAssignment() const;

    // --- Bypass (control thread write, audio thread read) ---
    void setBypassed(bool bypassed);
    bool isBypassed() const;

    // --- Processing (audio thread, RT-safe) ---
    void process(juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi);

    // --- Latency ---
    int getLatencySamples() const;

private:
    std::string name_;
    int handle_ = -1;
    std::unique_ptr<Processor> generator_;
    Chain chain_;
    std::atomic<float> gain_{1.0f};
    std::atomic<float> pan_{0.0f};
    Bus* outputBus_ = nullptr;
    std::vector<Send> sends_;
    MidiAssignment midiAssignment_ = MidiAssignment::none();
    std::atomic<bool> bypassed_{false};
    int nextSendId_ = 1;
    double sampleRate_ = 0.0;
    int blockSize_ = 0;
};

} // namespace squeeze
