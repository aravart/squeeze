#include "core/VoiceAllocator.h"

#include <algorithm>

namespace squeeze {

VoiceAllocator::VoiceAllocator(int maxVoices, const SamplerParams& params)
    : maxVoices_(std::max(1, maxVoices))
    , maxActiveVoices_(maxVoices_)
{
    int poolSize = maxVoices_ + 1;  // +1 for crossfade overlap
    voices_.reserve(poolSize);
    for (int i = 0; i < poolSize; ++i)
        voices_.emplace_back(params);
}

void VoiceAllocator::prepare(double sampleRate, int blockSize) {
    for (auto& voice : voices_)
        voice.prepare(sampleRate, blockSize);
}

void VoiceAllocator::release() {
    for (auto& voice : voices_)
        voice.release();
}

void VoiceAllocator::noteOn(const Buffer* buffer, int midiNote, float velocity) {
    if (buffer == nullptr) return;
    if (midiNote < 0 || midiNote > 127) return;

    if (mode_ == VoiceMode::mono) {
        // Release any currently playing voice (crossfade out)
        for (auto& v : voices_) {
            if (v.getState() == VoiceState::playing)
                v.noteOff(0);
        }

        // Find idle voice for new note
        auto* voice = findIdleVoice();
        if (!voice)
            voice = findReleasingVoiceToSteal();

        if (voice)
            voice->noteOn(buffer, midiNote, velocity, 0);
    } else {
        // Poly: check if we need to steal
        int playingCount = 0;
        for (const auto& v : voices_)
            if (v.getState() == VoiceState::playing) ++playingCount;

        if (playingCount >= maxActiveVoices_) {
            // Steal a playing voice (crossfade out)
            auto* victim = findVoiceToSteal();
            if (victim) victim->noteOff(0);
        }

        // Find idle voice, or hard-cut quietest releasing voice
        auto* voice = findIdleVoice();
        if (!voice)
            voice = findReleasingVoiceToSteal();

        if (voice)
            voice->noteOn(buffer, midiNote, velocity, 0);
    }
}

void VoiceAllocator::noteOff(int midiNote) {
    for (auto& voice : voices_) {
        if (voice.getState() == VoiceState::playing &&
            voice.getCurrentNote() == midiNote) {
            voice.noteOff(0);
        }
    }
}

void VoiceAllocator::allNotesOff() {
    for (auto& voice : voices_) {
        if (voice.getState() != VoiceState::idle) {
            voice.noteOff(0);
        }
    }
}

void VoiceAllocator::renderBlock(juce::AudioBuffer<float>& output,
                                  int startSample, int numSamples) {
    for (auto& voice : voices_) {
        if (voice.getState() != VoiceState::idle) {
            voice.render(output, startSample, numSamples);
        }
    }
}

void VoiceAllocator::setMode(VoiceMode mode) {
    mode_ = mode;
}

void VoiceAllocator::setStealPolicy(StealPolicy policy) {
    stealPolicy_ = policy;
}

void VoiceAllocator::setMaxActiveVoices(int count) {
    maxActiveVoices_ = std::clamp(count, 1, maxVoices_);
}

VoiceMode VoiceAllocator::getMode() const {
    return mode_;
}

int VoiceAllocator::getMaxVoices() const {
    return maxVoices_;
}

int VoiceAllocator::getActiveVoiceCount() const {
    int count = 0;
    for (const auto& voice : voices_) {
        if (voice.getState() != VoiceState::idle)
            ++count;
    }
    return count;
}

SamplerVoice* VoiceAllocator::findIdleVoice() {
    for (auto& voice : voices_) {
        if (voice.getState() == VoiceState::idle)
            return &voice;
    }
    return nullptr;
}

SamplerVoice* VoiceAllocator::findPlayingVoice(int midiNote) {
    for (auto& voice : voices_) {
        if (voice.getState() == VoiceState::playing &&
            voice.getCurrentNote() == midiNote)
            return &voice;
    }
    return nullptr;
}

SamplerVoice* VoiceAllocator::findVoiceToSteal() {
    SamplerVoice* victim = nullptr;

    if (stealPolicy_ == StealPolicy::oldest) {
        float maxAge = -1.0f;
        for (auto& voice : voices_) {
            if (voice.getState() == VoiceState::playing && voice.getAge() > maxAge) {
                maxAge = voice.getAge();
                victim = &voice;
            }
        }
    } else {
        float minLevel = 2.0f;
        for (auto& voice : voices_) {
            if (voice.getState() == VoiceState::playing &&
                voice.getEnvelopeLevel() < minLevel) {
                minLevel = voice.getEnvelopeLevel();
                victim = &voice;
            }
        }
    }

    return victim;
}

SamplerVoice* VoiceAllocator::findReleasingVoiceToSteal() {
    SamplerVoice* victim = nullptr;
    float minLevel = 2.0f;

    for (auto& voice : voices_) {
        if (voice.getState() == VoiceState::releasing &&
            voice.getEnvelopeLevel() < minLevel) {
            minLevel = voice.getEnvelopeLevel();
            victim = &voice;
        }
    }

    return victim;
}

} // namespace squeeze
