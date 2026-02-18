#include "core/Transport.h"

#include <algorithm>

namespace squeeze {

// ═══════════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════════

Transport::Transport()
{
    SQ_DEBUG("Transport: created (stopped, 120 BPM, 4/4)");
}

// ═══════════════════════════════════════════════════════════════════
// prepare
// ═══════════════════════════════════════════════════════════════════

void Transport::prepare(double sampleRate, int blockSize)
{
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;
    recomputeLoopSamples();
    SQ_INFO("Transport: prepare sr=%.0f bs=%d", sampleRate_, blockSize_);
}

// ═══════════════════════════════════════════════════════════════════
// State control
// ═══════════════════════════════════════════════════════════════════

void Transport::play()
{
    if (state_ == TransportState::playing) return;
    SQ_DEBUG_RT("Transport: play (from %s)",
                state_ == TransportState::stopped ? "stopped" : "paused");
    state_ = TransportState::playing;
}

void Transport::stop()
{
    if (state_ == TransportState::stopped) return;
    SQ_DEBUG_RT("Transport: stop");
    state_ = TransportState::stopped;
    positionInSamples_ = 0;
}

void Transport::pause()
{
    if (state_ != TransportState::playing) return;
    SQ_DEBUG_RT("Transport: pause at sample %lld", (long long)positionInSamples_);
    state_ = TransportState::paused;
}

// ═══════════════════════════════════════════════════════════════════
// Tempo and time signature
// ═══════════════════════════════════════════════════════════════════

void Transport::setTempo(double bpm)
{
    tempo_ = std::clamp(bpm, 1.0, 999.0);
    recomputeLoopSamples();
    SQ_DEBUG_RT("Transport: setTempo %.2f", tempo_);
}

void Transport::setTimeSignature(int numerator, int denominator)
{
    if (numerator < 1 || numerator > 32) return;

    // Denominator must be power of 2 in {1, 2, 4, 8, 16, 32}
    bool validDenom = false;
    for (int d : {1, 2, 4, 8, 16, 32})
    {
        if (denominator == d)
        {
            validDenom = true;
            break;
        }
    }
    if (!validDenom) return;

    timeSignature_ = {numerator, denominator};
    SQ_DEBUG_RT("Transport: setTimeSignature %d/%d", numerator, denominator);
}

// ═══════════════════════════════════════════════════════════════════
// Position
// ═══════════════════════════════════════════════════════════════════

void Transport::setPositionInSamples(int64_t samples)
{
    positionInSamples_ = std::max(samples, int64_t(0));
    SQ_DEBUG_RT("Transport: setPositionInSamples %lld", (long long)positionInSamples_);
}

void Transport::setPositionInBeats(double beats)
{
    positionInSamples_ = beatsToSamples(beats);
    SQ_DEBUG_RT("Transport: setPositionInBeats %.4f -> %lld samples",
                beats, (long long)positionInSamples_);
}

// ═══════════════════════════════════════════════════════════════════
// Looping
// ═══════════════════════════════════════════════════════════════════

void Transport::setLoopPoints(double startBeats, double endBeats)
{
    if (endBeats <= startBeats)
    {
        SQ_DEBUG_RT("Transport: setLoopPoints rejected (end %.4f <= start %.4f)",
                    endBeats, startBeats);
        return;
    }

    loopStartBeats_ = startBeats;
    loopEndBeats_ = endBeats;
    recomputeLoopSamples();
    SQ_DEBUG_RT("Transport: setLoopPoints %.4f - %.4f (samples %lld - %lld)",
                loopStartBeats_, loopEndBeats_,
                (long long)loopStartSamples_, (long long)loopEndSamples_);
}

void Transport::setLooping(bool enabled)
{
    if (enabled)
    {
        if (loopStartBeats_ == 0.0 && loopEndBeats_ == 0.0)
        {
            SQ_DEBUG_RT("Transport: setLooping(true) ignored — no valid loop points");
            return;
        }

        // Check minimum loop length
        if (blockSize_ > 0 && loopEndSamples_ - loopStartSamples_ < blockSize_)
        {
            SQ_WARN_RT("Transport: loop too short (%lld samples, block size %d), not enabling",
                       (long long)(loopEndSamples_ - loopStartSamples_), blockSize_);
            return;
        }

        looping_ = true;
        snapPositionToLoop();
        SQ_DEBUG_RT("Transport: looping enabled");
    }
    else
    {
        looping_ = false;
        SQ_DEBUG_RT("Transport: looping disabled");
    }
}

// ═══════════════════════════════════════════════════════════════════
// advance (audio thread)
// ═══════════════════════════════════════════════════════════════════

void Transport::advance(int numSamples)
{
    didLoopWrap_ = false;
    blockStartBeats_ = getPositionInBeats();
    blockEndBeats_ = blockStartBeats_;

    if (state_ != TransportState::playing || numSamples <= 0)
        return;

    positionInSamples_ += numSamples;

    if (looping_ && loopEndSamples_ > loopStartSamples_
        && positionInSamples_ >= loopEndSamples_)
    {
        int64_t loopLen = loopEndSamples_ - loopStartSamples_;
        positionInSamples_ = loopStartSamples_
            + ((positionInSamples_ - loopStartSamples_) % loopLen);
        didLoopWrap_ = true;
    }

    blockEndBeats_ = getPositionInBeats();
}

// ═══════════════════════════════════════════════════════════════════
// Queries
// ═══════════════════════════════════════════════════════════════════

TransportState Transport::getState() const { return state_; }
bool Transport::isPlaying() const { return state_ == TransportState::playing; }
double Transport::getTempo() const { return tempo_; }

juce::AudioPlayHead::TimeSignature Transport::getTimeSignature() const
{
    return timeSignature_;
}

double Transport::getSampleRate() const { return sampleRate_; }
int Transport::getBlockSize() const { return blockSize_; }
int64_t Transport::getPositionInSamples() const { return positionInSamples_; }

double Transport::getPositionInSeconds() const
{
    if (sampleRate_ <= 0.0) return 0.0;
    return static_cast<double>(positionInSamples_) / sampleRate_;
}

double Transport::getPositionInBeats() const
{
    if (sampleRate_ <= 0.0) return 0.0;
    return (static_cast<double>(positionInSamples_) / sampleRate_) * (tempo_ / 60.0);
}

int64_t Transport::getBarCount() const
{
    double qnPerBar = quarterNotesPerBar();
    if (qnPerBar <= 0.0) return 0;
    return static_cast<int64_t>(std::floor(getPositionInBeats() / qnPerBar));
}

double Transport::getPpqOfLastBarStart() const
{
    double qnPerBar = quarterNotesPerBar();
    if (qnPerBar <= 0.0) return 0.0;
    return static_cast<double>(getBarCount()) * qnPerBar;
}

bool Transport::isLooping() const { return looping_; }
double Transport::getLoopStartBeats() const { return loopStartBeats_; }
double Transport::getLoopEndBeats() const { return loopEndBeats_; }

bool Transport::didLoopWrap() const { return didLoopWrap_; }
double Transport::getBlockStartBeats() const { return blockStartBeats_; }
double Transport::getBlockEndBeats() const { return blockEndBeats_; }

// ═══════════════════════════════════════════════════════════════════
// AudioPlayHead
// ═══════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════

int64_t Transport::beatsToSamples(double beats) const
{
    if (sampleRate_ <= 0.0 || tempo_ <= 0.0) return 0;
    return static_cast<int64_t>(std::round(beats * 60.0 / tempo_ * sampleRate_));
}

double Transport::quarterNotesPerBar() const
{
    return static_cast<double>(timeSignature_.numerator)
         * (4.0 / static_cast<double>(timeSignature_.denominator));
}

void Transport::recomputeLoopSamples()
{
    loopStartSamples_ = beatsToSamples(loopStartBeats_);
    loopEndSamples_ = beatsToSamples(loopEndBeats_);

    if (looping_ && blockSize_ > 0
        && loopEndSamples_ - loopStartSamples_ < blockSize_)
    {
        looping_ = false;
        SQ_WARN_RT("Transport: loop too short (%lld samples, block size %d), disabling",
                   (long long)(loopEndSamples_ - loopStartSamples_), blockSize_);
    }

    snapPositionToLoop();
}

void Transport::snapPositionToLoop()
{
    if (!looping_) return;

    int64_t loopLen = loopEndSamples_ - loopStartSamples_;
    if (loopLen <= 0) return;

    if (positionInSamples_ >= loopEndSamples_)
    {
        positionInSamples_ = loopStartSamples_
            + ((positionInSamples_ - loopStartSamples_) % loopLen);
    }
    else if (positionInSamples_ < loopStartSamples_)
    {
        positionInSamples_ = loopStartSamples_;
    }
}

} // namespace squeeze
