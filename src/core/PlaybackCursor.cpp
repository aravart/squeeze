#include "core/PlaybackCursor.h"

#include <algorithm>
#include <cstring>

namespace squeeze {

PlaybackCursor::PlaybackCursor() = default;
PlaybackCursor::~PlaybackCursor() = default;

PlaybackCursor::PlaybackCursor(PlaybackCursor&& other) noexcept
    : position_(other.position_),
      engineSampleRate_(other.engineSampleRate_),
      stopped_(other.stopped_),
      direction_(other.direction_),
      crossfading_(other.crossfading_),
      crossfadePosition_(other.crossfadePosition_),
      crossfadeRemaining_(other.crossfadeRemaining_),
      crossfadeLength_(other.crossfadeLength_)
{
    other.position_ = 0.0;
    other.stopped_ = false;
    other.direction_ = 1;
    other.crossfading_ = false;
}

PlaybackCursor& PlaybackCursor::operator=(PlaybackCursor&& other) noexcept
{
    if (this != &other)
    {
        position_ = other.position_;
        engineSampleRate_ = other.engineSampleRate_;
        stopped_ = other.stopped_;
        direction_ = other.direction_;
        crossfading_ = other.crossfading_;
        crossfadePosition_ = other.crossfadePosition_;
        crossfadeRemaining_ = other.crossfadeRemaining_;
        crossfadeLength_ = other.crossfadeLength_;
        other.position_ = 0.0;
        other.stopped_ = false;
        other.direction_ = 1;
        other.crossfading_ = false;
    }
    return *this;
}

void PlaybackCursor::prepare(double engineSampleRate)
{
    engineSampleRate_ = engineSampleRate;
}

void PlaybackCursor::reset()
{
    position_ = 0.0;
    stopped_ = false;
    direction_ = 1;
    crossfading_ = false;
    crossfadeRemaining_ = 0.0;
    crossfadeLength_ = 0.0;
}

float PlaybackCursor::interpolate(const float* data, int length, double pos) const
{
    int i = static_cast<int>(std::floor(pos));
    double t = pos - std::floor(pos);

    auto clamp = [&](int idx) -> int {
        return std::max(0, std::min(idx, length - 1));
    };

    float s0 = data[clamp(i - 1)];
    float s1 = data[clamp(i)];
    float s2 = data[clamp(i + 1)];
    float s3 = data[clamp(i + 2)];

    float ft = static_cast<float>(t);
    float a0 = -0.5f * s0 + 1.5f * s1 - 1.5f * s2 + 0.5f * s3;
    float a1 =        s0 - 2.5f * s1 + 2.0f * s2 - 0.5f * s3;
    float a2 = -0.5f * s0             + 0.5f * s2;
    float a3 =                    s1;

    return ((a0 * ft + a1) * ft + a2) * ft + a3;
}

int PlaybackCursor::render(const Buffer* buffer, float* destL, float* destR,
                           int numSamples, double rate,
                           LoopMode loopMode, double loopStart, double loopEnd,
                           double fadeSamples)
{
    if (!buffer || numSamples <= 0)
    {
        if (destL && numSamples > 0)
            std::memset(destL, 0, static_cast<size_t>(numSamples) * sizeof(float));
        if (destR && numSamples > 0)
            std::memset(destR, 0, static_cast<size_t>(numSamples) * sizeof(float));
        return 0;
    }

    if (stopped_)
    {
        std::memset(destL, 0, static_cast<size_t>(numSamples) * sizeof(float));
        std::memset(destR, 0, static_cast<size_t>(numSamples) * sizeof(float));
        return 0;
    }

    int bufLen = buffer->getLengthInSamples();
    int numCh = buffer->getNumChannels();
    const float* ch0 = buffer->getReadPointer(0);
    const float* ch1 = numCh > 1 ? buffer->getReadPointer(1) : ch0;

    double sampleRateRatio = buffer->getSampleRate() / engineSampleRate_;

    // Resolve loop boundaries (normalized → samples)
    double loopStartSample = loopStart * bufLen;
    double loopEndSample = loopEnd * bufLen;
    if (loopStartSample >= loopEndSample)
    {
        loopStartSample = 0.0;
        loopEndSample = static_cast<double>(bufLen);
    }

    if (fadeSamples < 0.0)
        fadeSamples = 0.0;

    int rendered = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        // Read current sample with interpolation
        float sampleL, sampleR;

        if (crossfading_ && crossfadeRemaining_ > 0.0)
        {
            // Read from both old (crossfade) position and new position
            float oldL = interpolate(ch0, bufLen, crossfadePosition_);
            float oldR = interpolate(ch1, bufLen, crossfadePosition_);
            float newL = interpolate(ch0, bufLen, position_);
            float newR = interpolate(ch1, bufLen, position_);

            double progress = 1.0 - (crossfadeRemaining_ / crossfadeLength_);
            float fadeIn = static_cast<float>(std::sqrt(progress));
            float fadeOut = static_cast<float>(std::sqrt(1.0 - progress));

            sampleL = oldL * fadeOut + newL * fadeIn;
            sampleR = oldR * fadeOut + newR * fadeIn;

            crossfadePosition_ += rate * static_cast<double>(direction_) * sampleRateRatio;
            crossfadeRemaining_ -= 1.0;
            if (crossfadeRemaining_ <= 0.0)
                crossfading_ = false;
        }
        else
        {
            sampleL = interpolate(ch0, bufLen, position_);
            sampleR = interpolate(ch1, bufLen, position_);
        }

        destL[i] = sampleL;
        destR[i] = sampleR;
        ++rendered;

        // Advance position
        double increment = rate * static_cast<double>(direction_) * sampleRateRatio;
        position_ += increment;

        // Handle loop / end-of-buffer
        if (loopMode == LoopMode::off)
        {
            if (position_ >= static_cast<double>(bufLen) || position_ < 0.0)
            {
                stopped_ = true;
                // Fill rest with silence
                for (int j = i + 1; j < numSamples; ++j)
                {
                    destL[j] = 0.0f;
                    destR[j] = 0.0f;
                }
                break;
            }
        }
        else if (loopMode == LoopMode::forward)
        {
            if (position_ >= loopEndSample)
            {
                double overshoot = position_ - loopEndSample;
                double loopLen = loopEndSample - loopStartSample;
                if (loopLen > 0.0)
                {
                    if (fadeSamples > 0.0)
                    {
                        crossfading_ = true;
                        crossfadePosition_ = position_;
                        crossfadeLength_ = fadeSamples;
                        crossfadeRemaining_ = fadeSamples;
                    }
                    position_ = loopStartSample + std::fmod(overshoot, loopLen);
                }
            }
            else if (position_ < loopStartSample)
            {
                double loopLen = loopEndSample - loopStartSample;
                if (loopLen > 0.0)
                {
                    if (fadeSamples > 0.0)
                    {
                        crossfading_ = true;
                        crossfadePosition_ = position_;
                        crossfadeLength_ = fadeSamples;
                        crossfadeRemaining_ = fadeSamples;
                    }
                    double undershoot = loopStartSample - position_;
                    position_ = loopEndSample - std::fmod(undershoot, loopLen);
                }
            }
        }
        else if (loopMode == LoopMode::pingPong)
        {
            if (position_ >= loopEndSample)
            {
                position_ = loopEndSample - (position_ - loopEndSample);
                direction_ = -1;
            }
            else if (position_ < loopStartSample)
            {
                position_ = loopStartSample + (loopStartSample - position_);
                direction_ = 1;
            }
        }
    }

    return rendered;
}

void PlaybackCursor::seek(double normalizedPosition, const Buffer* buffer, double fadeSamples)
{
    if (!buffer) return;

    double newPos = normalizedPosition * buffer->getLengthInSamples();
    newPos = std::max(0.0, std::min(newPos, static_cast<double>(buffer->getLengthInSamples() - 1)));

    if (fadeSamples > 0.0 && !stopped_)
    {
        crossfading_ = true;
        crossfadePosition_ = position_;
        crossfadeLength_ = fadeSamples;
        crossfadeRemaining_ = fadeSamples;
    }

    position_ = newPos;
    stopped_ = false;
}

double PlaybackCursor::getPosition(const Buffer* buffer) const
{
    if (!buffer || buffer->getLengthInSamples() == 0)
        return 0.0;
    double norm = position_ / static_cast<double>(buffer->getLengthInSamples());
    return std::max(0.0, std::min(1.0, norm));
}

double PlaybackCursor::getRawPosition() const
{
    return position_;
}

void PlaybackCursor::setRawPosition(double samplePosition)
{
    position_ = samplePosition;
}

bool PlaybackCursor::isStopped() const
{
    return stopped_;
}

} // namespace squeeze
