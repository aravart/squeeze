#pragma once

#include "core/Logger.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <string>
#include <vector>

namespace squeeze {

struct ParamDescriptor {
    std::string name;
    float defaultValue;
    float minValue;
    float maxValue;
    int numSteps;       // 0 = continuous, >0 = stepped
    bool automatable;
    bool boolean;
    std::string label;  // unit: "dB", "Hz", "%", ""
    std::string group;  // "" = ungrouped
};

class Processor {
public:
    explicit Processor(const std::string& name)
        : name_(name)
    {
        SQ_DEBUG("Processor created: name=%s", name_.c_str());
    }

    virtual ~Processor() = default;

    // Non-copyable, non-movable
    Processor(const Processor&) = delete;
    Processor& operator=(const Processor&) = delete;

    // --- Lifecycle (control thread) ---
    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void reset() {}
    virtual void release() {}

    // --- Processing (audio thread, RT-safe) ---
    virtual void process(juce::AudioBuffer<float>& buffer) = 0;
    virtual void process(juce::AudioBuffer<float>& buffer,
                         const juce::MidiBuffer& midi) {
        (void)midi;
        process(buffer);
    }

    // --- Parameters (string-based) ---
    virtual int getParameterCount() const { return 0; }
    virtual ParamDescriptor getParameterDescriptor(int index) const { (void)index; return {}; }
    virtual std::vector<ParamDescriptor> getParameterDescriptors() const { return {}; }
    virtual float getParameter(const std::string& name) const { (void)name; return 0.0f; }
    virtual void setParameter(const std::string& name, float value) { (void)name; (void)value; }
    virtual std::string getParameterText(const std::string& name) const { (void)name; return ""; }

    // --- Identity ---
    const std::string& getName() const { return name_; }

    // --- Bypass (control thread write, audio thread read) ---
    void setBypassed(bool b) { bypassed_.store(b, std::memory_order_relaxed); }
    bool isBypassed() const { return bypassed_.load(std::memory_order_relaxed); }

    // --- Latency ---
    virtual int getLatencySamples() const { return 0; }

    // --- PlayHead (control thread, called by Engine) ---
    virtual void setPlayHead(juce::AudioPlayHead* /*playHead*/) {}

    // --- Handle (set by Engine when processor is added) ---
    int getHandle() const { return handle_; }
    void setHandle(int h) { handle_ = h; }

private:
    std::string name_;
    int handle_ = -1;
    std::atomic<bool> bypassed_{false};
};

} // namespace squeeze
