#pragma once

#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>

namespace squeeze {

enum class TransportState { stopped, playing, paused };

class Transport : public juce::AudioPlayHead {
public:
    Transport();

    // Audio-thread: advance position by numSamples when playing
    void advance(int numSamples);

    // State control
    void play();
    void stop();
    void pause();

    void setTempo(double bpm);
    void setTimeSignature(int numerator, int denominator);
    void setPositionInSamples(int64_t samples);
    void setPositionInBeats(double beats);

    void setLoopPoints(double startBeats, double endBeats);
    void setLooping(bool enabled);

    void setSampleRate(double sr);

    // Queries
    TransportState getState() const;
    bool isPlaying() const;

    double getTempo() const;
    juce::AudioPlayHead::TimeSignature getTimeSignature() const;
    double getSampleRate() const;

    int64_t getPositionInSamples() const;
    double getPositionInSeconds() const;
    double getPositionInBeats() const;
    int64_t getBarCount() const;
    double getPpqOfLastBarStart() const;

    bool isLooping() const;
    double getLoopStartBeats() const;
    double getLoopEndBeats() const;

    // Loop wrap detection
    bool didLoopWrap() const;
    double getBlockStartBeats() const;
    double getBlockEndBeats() const;

    // juce::AudioPlayHead
    juce::Optional<PositionInfo> getPosition() const override;

private:
    double quarterNotesPerBar() const;
    int64_t beatsToSamples(double beats) const;

    TransportState state_ = TransportState::stopped;
    int64_t positionInSamples_ = 0;
    double sampleRate_ = 0.0;
    double tempo_ = 120.0;
    juce::AudioPlayHead::TimeSignature timeSignature_;

    bool looping_ = false;
    double loopStartBeats_ = 0.0;
    double loopEndBeats_ = 0.0;

    bool didLoopWrap_ = false;
    double blockStartBeats_ = 0.0;
    double blockEndBeats_ = 0.0;
};

} // namespace squeeze
