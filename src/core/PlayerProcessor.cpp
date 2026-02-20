#include "core/PlayerProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <juce_audio_basics/juce_audio_basics.h>

namespace squeeze {

const ParamDescriptor PlayerProcessor::kDescriptors[kParamCount] = {
    {"playing",    0.0f, 0.0f, 1.0f,  2, true, false, "",   "Playback"},
    {"position",   0.0f, 0.0f, 1.0f,  0, true, false, "",   "Playback"},
    {"speed",      1.0f, -4.0f, 4.0f, 0, true, false, "x",  "Playback"},
    {"loop_mode",  0.0f, 0.0f, 2.0f,  3, true, false, "",   "Loop"},
    {"loop_start", 0.0f, 0.0f, 1.0f,  0, true, false, "",   "Loop"},
    {"loop_end",   1.0f, 0.0f, 1.0f,  0, true, false, "",   "Loop"},
    {"fade_ms",    5.0f, 0.0f, 50.0f, 0, true, false, "ms", "Playback"},
    {"tempo_lock", 0.0f, 0.0f, 1.0f,  2, true, true,  "",   "Playback"},
    {"transpose",  0.0f, -24.0f, 24.0f, 0, true, false, "st", "Playback"},
};

PlayerProcessor::PlayerProcessor()
    : Processor("Player")
{
    SQ_INFO("PlayerProcessor created");
}

PlayerProcessor::~PlayerProcessor()
{
    SQ_INFO("PlayerProcessor destroyed");
}

void PlayerProcessor::prepare(double sampleRate, int /*blockSize*/)
{
    sampleRate_ = sampleRate;
    cursor_.prepare(sampleRate);
    SQ_DEBUG("PlayerProcessor::prepare: sr=%.1f", sampleRate);
}

void PlayerProcessor::release()
{
    SQ_DEBUG("PlayerProcessor::release");
}

void PlayerProcessor::reset()
{
    cursor_.reset();
    wasPlaying_ = false;
    fadeGain_ = 0.0f;
    SQ_DEBUG("PlayerProcessor::reset");
}

void PlayerProcessor::setPlayHead(juce::AudioPlayHead* playHead)
{
    SQ_DEBUG("PlayerProcessor::setPlayHead: playHead=%p", (void*)playHead);
    playHead_ = playHead;
}

void PlayerProcessor::process(juce::AudioBuffer<float>& buffer)
{
    int numSamples = buffer.getNumSamples();
    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : L;

    const Buffer* buf = buffer_.load(std::memory_order_acquire);
    bool isPlaying = playing_ >= 0.5f;

    // Handle seek
    if (seekPending_.exchange(false, std::memory_order_acquire))
    {
        float target = seekTarget_.load(std::memory_order_relaxed);
        cursor_.seek(static_cast<double>(target), buf, fadeSamplesFromMs());
        SQ_TRACE_RT("PlayerProcessor: seek to %.3f", static_cast<double>(target));
    }

    // Compute effective speed with tempo_lock and transpose
    double effectiveSpeed = static_cast<double>(speed_);
    if (tempoLock_ >= 0.5f)
    {
        double engineTempo = 0.0;
        if (playHead_)
        {
            auto pos = playHead_->getPosition();
            if (pos.hasValue() && pos->getBpm().hasValue())
                engineTempo = *pos->getBpm();
        }
        double bufferTempo = buf ? buf->getTempo() : 0.0;
        if (engineTempo > 0.0 && bufferTempo > 0.0)
            effectiveSpeed = (engineTempo / bufferTempo) * static_cast<double>(speed_);
    }
    if (transpose_ != 0.0f)
        effectiveSpeed *= std::pow(2.0, static_cast<double>(transpose_) / 12.0);

    if (!isPlaying || !buf || effectiveSpeed == 0.0)
    {
        // Apply fade-out if we were playing
        if (wasPlaying_ && fadeMs_ > 0.0f)
        {
            double fadeSamples = fadeSamplesFromMs();
            float fadeStep = fadeSamples > 0.0 ? static_cast<float>(1.0 / fadeSamples) : 1.0f;

            for (int i = 0; i < numSamples && fadeGain_ > 0.0f; ++i)
            {
                L[i] *= fadeGain_;
                R[i] *= fadeGain_;
                fadeGain_ -= fadeStep;
                if (fadeGain_ < 0.0f) fadeGain_ = 0.0f;
            }
            if (fadeGain_ <= 0.0f)
                wasPlaying_ = false;
        }
        else
        {
            buffer.clear();
            wasPlaying_ = false;
        }
        return;
    }

    // Render from cursor
    LoopMode lm = LoopMode::off;
    if (loopMode_ >= 1.5f) lm = LoopMode::pingPong;
    else if (loopMode_ >= 0.5f) lm = LoopMode::forward;

    int rendered = cursor_.render(buf, L, R, numSamples,
                                   effectiveSpeed, lm,
                                   static_cast<double>(loopStart_),
                                   static_cast<double>(loopEnd_),
                                   fadeSamplesFromMs());

    // Apply fade-in if just started playing
    if (!wasPlaying_ && fadeMs_ > 0.0f)
    {
        double fadeSamples = fadeSamplesFromMs();
        float fadeStep = fadeSamples > 0.0 ? static_cast<float>(1.0 / fadeSamples) : 1.0f;
        fadeGain_ = 0.0f;
        for (int i = 0; i < rendered && fadeGain_ < 1.0f; ++i)
        {
            L[i] *= fadeGain_;
            R[i] *= fadeGain_;
            fadeGain_ += fadeStep;
            if (fadeGain_ > 1.0f) fadeGain_ = 1.0f;
        }
    }

    wasPlaying_ = true;
    fadeGain_ = 1.0f;

    // Auto-stop when cursor reaches end (loop off)
    if (cursor_.isStopped())
    {
        playing_ = 0.0f;
        wasPlaying_ = false;
        SQ_DEBUG_RT("PlayerProcessor: auto-stopped at end of buffer");
    }

    // Clear remaining samples if cursor didn't fill the whole buffer
    if (rendered < numSamples)
    {
        for (int i = rendered; i < numSamples; ++i)
        {
            L[i] = 0.0f;
            R[i] = 0.0f;
        }
    }
}

int PlayerProcessor::getParameterCount() const
{
    return kParamCount;
}

ParamDescriptor PlayerProcessor::getParameterDescriptor(int index) const
{
    if (index < 0 || index >= kParamCount) return {};
    return kDescriptors[index];
}

std::vector<ParamDescriptor> PlayerProcessor::getParameterDescriptors() const
{
    return {std::begin(kDescriptors), std::end(kDescriptors)};
}

float PlayerProcessor::getParameter(const std::string& name) const
{
    if (name == "playing")    return playing_;
    if (name == "position")
    {
        const Buffer* buf = buffer_.load(std::memory_order_acquire);
        return static_cast<float>(cursor_.getPosition(buf));
    }
    if (name == "speed")      return speed_;
    if (name == "loop_mode")  return loopMode_;
    if (name == "loop_start") return loopStart_;
    if (name == "loop_end")   return loopEnd_;
    if (name == "fade_ms")    return fadeMs_;
    if (name == "tempo_lock") return tempoLock_;
    if (name == "transpose")  return transpose_;
    return 0.0f;
}

void PlayerProcessor::setParameter(const std::string& name, float value)
{
    if (name == "playing")
    {
        playing_ = value >= 0.5f ? 1.0f : 0.0f;
        SQ_DEBUG("PlayerProcessor::setParameter: playing=%.0f", static_cast<double>(playing_));
    }
    else if (name == "position")
    {
        value = std::max(0.0f, std::min(1.0f, value));
        seekTarget_.store(value, std::memory_order_relaxed);
        seekPending_.store(true, std::memory_order_release);
        SQ_DEBUG("PlayerProcessor::setParameter: position=%.3f", static_cast<double>(value));
    }
    else if (name == "speed")
    {
        speed_ = std::max(-4.0f, std::min(4.0f, value));
    }
    else if (name == "loop_mode")
    {
        loopMode_ = std::max(0.0f, std::min(2.0f, std::round(value)));
    }
    else if (name == "loop_start")
    {
        loopStart_ = std::max(0.0f, std::min(1.0f, value));
    }
    else if (name == "loop_end")
    {
        loopEnd_ = std::max(0.0f, std::min(1.0f, value));
    }
    else if (name == "fade_ms")
    {
        fadeMs_ = std::max(0.0f, std::min(50.0f, value));
    }
    else if (name == "tempo_lock")
    {
        tempoLock_ = value >= 0.5f ? 1.0f : 0.0f;
    }
    else if (name == "transpose")
    {
        transpose_ = std::max(-24.0f, std::min(24.0f, value));
    }
}

std::string PlayerProcessor::getParameterText(const std::string& name) const
{
    char buf[64];

    if (name == "playing")
    {
        return playing_ >= 0.5f ? "Playing" : "Stopped";
    }
    if (name == "position")
    {
        const Buffer* b = buffer_.load(std::memory_order_acquire);
        float pos = static_cast<float>(cursor_.getPosition(b)) * 100.0f;
        std::snprintf(buf, sizeof(buf), "%.1f%%", static_cast<double>(pos));
        return buf;
    }
    if (name == "speed")
    {
        std::snprintf(buf, sizeof(buf), "%.1fx", static_cast<double>(speed_));
        return buf;
    }
    if (name == "loop_mode")
    {
        if (loopMode_ >= 1.5f) return "Ping-pong";
        if (loopMode_ >= 0.5f) return "Forward";
        return "Off";
    }
    if (name == "loop_start")
    {
        std::snprintf(buf, sizeof(buf), "%.1f%%", static_cast<double>(loopStart_ * 100.0f));
        return buf;
    }
    if (name == "loop_end")
    {
        std::snprintf(buf, sizeof(buf), "%.1f%%", static_cast<double>(loopEnd_ * 100.0f));
        return buf;
    }
    if (name == "fade_ms")
    {
        std::snprintf(buf, sizeof(buf), "%.1f ms", static_cast<double>(fadeMs_));
        return buf;
    }
    if (name == "tempo_lock")
    {
        return tempoLock_ >= 0.5f ? "On" : "Off";
    }
    if (name == "transpose")
    {
        if (transpose_ > 0.0f)
            std::snprintf(buf, sizeof(buf), "+%.1f st", static_cast<double>(transpose_));
        else
            std::snprintf(buf, sizeof(buf), "%.1f st", static_cast<double>(transpose_));
        return buf;
    }
    return "";
}

int PlayerProcessor::getLatencySamples() const
{
    return 0;
}

void PlayerProcessor::setBuffer(const Buffer* buffer)
{
    SQ_DEBUG("PlayerProcessor::setBuffer: %s",
             buffer ? buffer->getName().c_str() : "(null)");
    buffer_.store(buffer, std::memory_order_release);
    cursor_.reset();
    playing_ = 0.0f;
    wasPlaying_ = false;
    fadeGain_ = 0.0f;
}

const Buffer* PlayerProcessor::getBuffer() const
{
    return buffer_.load(std::memory_order_acquire);
}

double PlayerProcessor::fadeSamplesFromMs() const
{
    return fadeMs_ * sampleRate_ / 1000.0;
}

} // namespace squeeze
