#include "core/Transport.h"
#include <algorithm>
#include <cmath>

namespace squeeze {

Transport::Transport()
{
    timeSignature_.numerator = 4;
    timeSignature_.denominator = 4;
}

void Transport::advance(int numSamples)
{
    didLoopWrap_ = false;

    if (state_ != TransportState::playing || numSamples <= 0)
        return;

    blockStartBeats_ = getPositionInBeats();

    positionInSamples_ += numSamples;

    if (looping_ && sampleRate_ > 0.0)
    {
        int64_t loopStart = beatsToSamples(loopStartBeats_);
        int64_t loopEnd = beatsToSamples(loopEndBeats_);
        int64_t loopLen = loopEnd - loopStart;

        if (loopLen > 0 && positionInSamples_ >= loopEnd)
        {
            positionInSamples_ = loopStart
                + ((positionInSamples_ - loopStart) % loopLen);
            didLoopWrap_ = true;
        }
    }

    blockEndBeats_ = getPositionInBeats();
}

void Transport::play()
{
    state_ = TransportState::playing;
}

void Transport::stop()
{
    state_ = TransportState::stopped;
    positionInSamples_ = 0;
}

void Transport::pause()
{
    state_ = TransportState::paused;
}

void Transport::setTempo(double bpm)
{
    tempo_ = std::clamp(bpm, 1.0, 999.0);
}

void Transport::setTimeSignature(int numerator, int denominator)
{
    if (numerator < 1 || numerator > 32)
        return;

    // Denominator must be power of 2 in {1, 2, 4, 8, 16, 32}
    if (denominator < 1 || denominator > 32)
        return;
    if ((denominator & (denominator - 1)) != 0)
        return;

    timeSignature_.numerator = numerator;
    timeSignature_.denominator = denominator;
}

void Transport::setPositionInSamples(int64_t samples)
{
    positionInSamples_ = std::max(samples, int64_t(0));
}

void Transport::setPositionInBeats(double beats)
{
    if (sampleRate_ <= 0.0)
        return;
    positionInSamples_ = beatsToSamples(beats);
}

void Transport::setLoopPoints(double startBeats, double endBeats)
{
    if (endBeats <= startBeats)
        return;
    loopStartBeats_ = startBeats;
    loopEndBeats_ = endBeats;
}

void Transport::setLooping(bool enabled)
{
    if (enabled && loopStartBeats_ == 0.0 && loopEndBeats_ == 0.0)
        return;
    looping_ = enabled;
}

void Transport::setSampleRate(double sr)
{
    sampleRate_ = sr;
}

TransportState Transport::getState() const
{
    return state_;
}

bool Transport::isPlaying() const
{
    return state_ == TransportState::playing;
}

double Transport::getTempo() const
{
    return tempo_;
}

juce::AudioPlayHead::TimeSignature Transport::getTimeSignature() const
{
    return timeSignature_;
}

double Transport::getSampleRate() const
{
    return sampleRate_;
}

int64_t Transport::getPositionInSamples() const
{
    return positionInSamples_;
}

double Transport::getPositionInSeconds() const
{
    if (sampleRate_ <= 0.0)
        return 0.0;
    return static_cast<double>(positionInSamples_) / sampleRate_;
}

double Transport::getPositionInBeats() const
{
    if (sampleRate_ <= 0.0)
        return 0.0;
    return getPositionInSeconds() * (tempo_ / 60.0);
}

int64_t Transport::getBarCount() const
{
    double qnpb = quarterNotesPerBar();
    if (qnpb <= 0.0)
        return 0;
    return static_cast<int64_t>(std::floor(getPositionInBeats() / qnpb));
}

double Transport::getPpqOfLastBarStart() const
{
    double qnpb = quarterNotesPerBar();
    if (qnpb <= 0.0)
        return 0.0;
    return static_cast<double>(getBarCount()) * qnpb;
}

bool Transport::isLooping() const
{
    return looping_;
}

double Transport::getLoopStartBeats() const
{
    return loopStartBeats_;
}

double Transport::getLoopEndBeats() const
{
    return loopEndBeats_;
}

bool Transport::didLoopWrap() const
{
    return didLoopWrap_;
}

double Transport::getBlockStartBeats() const
{
    return blockStartBeats_;
}

double Transport::getBlockEndBeats() const
{
    return blockEndBeats_;
}

juce::Optional<juce::AudioPlayHead::PositionInfo> Transport::getPosition() const
{
    PositionInfo info;

    info.setTimeInSamples(positionInSamples_);
    info.setTimeInSeconds(getPositionInSeconds());
    info.setPpqPosition(getPositionInBeats());
    info.setPpqPositionOfLastBarStart(getPpqOfLastBarStart());
    info.setBarCount(static_cast<int>(getBarCount()));
    info.setBpm(tempo_);
    info.setTimeSignature(timeSignature_);
    info.setIsPlaying(state_ == TransportState::playing);
    info.setIsRecording(false);
    info.setIsLooping(looping_);

    if (looping_)
    {
        LoopPoints lp;
        lp.ppqStart = loopStartBeats_;
        lp.ppqEnd = loopEndBeats_;
        info.setLoopPoints(lp);
    }

    return info;
}

double Transport::quarterNotesPerBar() const
{
    return static_cast<double>(timeSignature_.numerator) * (4.0 / timeSignature_.denominator);
}

int64_t Transport::beatsToSamples(double beats) const
{
    if (sampleRate_ <= 0.0 || tempo_ <= 0.0)
        return 0;
    return static_cast<int64_t>(std::round(beats * 60.0 / tempo_ * sampleRate_));
}

} // namespace squeeze
