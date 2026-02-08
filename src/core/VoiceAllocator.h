#pragma once

#include "core/SamplerVoice.h"

#include <vector>

namespace squeeze {

enum class VoiceMode { mono, poly, legato };
enum class StealPolicy { oldest, quietest };

class VoiceAllocator {
public:
    VoiceAllocator(int maxVoices, const SamplerParams& params);

    void prepare(double sampleRate, int blockSize);
    void release();

    // Note events (audio thread — called by SamplerNode between sub-block segments)
    void noteOn(const Buffer* buffer, int midiNote, float velocity);
    void noteOff(int midiNote);
    void allNotesOff();

    /// Render all active voices into output (additive).
    void renderBlock(juce::AudioBuffer<float>& output, int startSample,
                     int numSamples);

    // Configuration (control thread)
    void setMode(VoiceMode mode);
    void setStealPolicy(StealPolicy policy);
    void setMaxActiveVoices(int count);

    // State queries
    VoiceMode getMode() const;
    int getMaxVoices() const;
    int getActiveVoiceCount() const;

private:
    SamplerVoice* findIdleVoice();
    SamplerVoice* findPlayingVoice(int midiNote);
    SamplerVoice* findVoiceToSteal();

    std::vector<SamplerVoice> voices_;
    VoiceMode mode_ = VoiceMode::mono;
    StealPolicy stealPolicy_ = StealPolicy::oldest;
    int maxActiveVoices_;
};

} // namespace squeeze
