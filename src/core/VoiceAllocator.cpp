#include "core/VoiceAllocator.h"

#include <algorithm>

namespace squeeze {

VoiceAllocator::VoiceAllocator(int maxVoices, const SamplerParams& params)
    : maxActiveVoices_(std::max(1, maxVoices))
{
    voices_.reserve(maxActiveVoices_);
    for (int i = 0; i < maxActiveVoices_; ++i)
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
        // Mono: always retrigger the first voice
        voices_[0].noteOn(buffer, midiNote, velocity, 0);
    } else {
        // Poly: find idle voice or steal
        auto* voice = findIdleVoice();
        if (!voice) {
            voice = findVoiceToSteal();
        }
        if (voice) {
            voice->noteOn(buffer, midiNote, velocity, 0);
        }
    }
}

void VoiceAllocator::noteOff(int midiNote) {
    // Only match voices in playing state
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
    maxActiveVoices_ = std::clamp(count, 1, static_cast<int>(voices_.size()));
}

VoiceMode VoiceAllocator::getMode() const {
    return mode_;
}

int VoiceAllocator::getMaxVoices() const {
    return static_cast<int>(voices_.size());
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
            if (voice.getState() != VoiceState::idle && voice.getAge() > maxAge) {
                maxAge = voice.getAge();
                victim = &voice;
            }
        }
    } else {
        // Quietest
        float minLevel = 2.0f;
        for (auto& voice : voices_) {
            if (voice.getState() != VoiceState::idle &&
                voice.getEnvelopeLevel() < minLevel) {
                minLevel = voice.getEnvelopeLevel();
                victim = &voice;
            }
        }
    }

    return victim;
}

} // namespace squeeze
