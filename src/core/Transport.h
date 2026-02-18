#pragma once

#include "core/Logger.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdint>

namespace squeeze {

enum class TransportState { stopped, playing, paused };

class Transport : public juce::AudioPlayHead {
public:
    Transport();

    // Audio-thread method
    void advance(int numSamples);

    // State control (audio thread, via Engine's handleCommand)
    void play();
    void stop();
    void pause();

    void setTempo(double bpm);
    void setTimeSignature(int numerator, int denominator);
    void setPositionInSamples(int64_t samples);
    void setPositionInBeats(double beats);

    void setLoopPoints(double startBeats, double endBeats);
    void setLooping(bool enabled);

    void prepare(double sampleRate, int blockSize);

    // Queries (audio thread only)
    TransportState getState() const;
    bool isPlaying() const;

    double getTempo() const;
    juce::AudioPlayHead::TimeSignature getTimeSignature() const;
    double getSampleRate() const;
    int getBlockSize() const;

    int64_t getPositionInSamples() const;
    double getPositionInSeconds() const;
    double getPositionInBeats() const;
    int64_t getBarCount() const;
    double getPpqOfLastBarStart() const;

    bool isLooping() const;
    double getLoopStartBeats() const;
    double getLoopEndBeats() const;

    // Block range (read by Engine for EventScheduler and ClockDispatch)
    bool didLoopWrap() const;
    double getBlockStartBeats() const;
    double getBlockEndBeats() const;

    // juce::AudioPlayHead
    juce::Optional<PositionInfo> getPosition() const override;

private:
    TransportState state_ = TransportState::stopped;
    int64_t positionInSamples_ = 0;
    double tempo_ = 120.0;
    juce::AudioPlayHead::TimeSignature timeSignature_{4, 4};
    double sampleRate_ = 0.0;
    int blockSize_ = 0;

    // Looping
    bool looping_ = false;
    double loopStartBeats_ = 0.0;
    double loopEndBeats_ = 0.0;
    int64_t loopStartSamples_ = 0;
    int64_t loopEndSamples_ = 0;

    // Per-block state
    bool didLoopWrap_ = false;
    double blockStartBeats_ = 0.0;
    double blockEndBeats_ = 0.0;

    // Helpers
    int64_t beatsToSamples(double beats) const;
    double quarterNotesPerBar() const;
    void recomputeLoopSamples();
    void snapPositionToLoop();
};

} // namespace squeeze
