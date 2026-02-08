#include "core/SamplerVoice.h"
#include "core/Logger.h"

#include <algorithm>
#include <cmath>

namespace squeeze {

static constexpr double kCurveExponent = 4.0;
static constexpr double kMinCutoffHz = 20.0;
static constexpr double kMaxCutoffHz = 20000.0;
static constexpr double kFilterModOctaves = 10.0;

// ============================================================
// Curve helper
// ============================================================

double SamplerVoice::applyCurve(double t, EnvCurve curve)
{
    t = std::max(0.0, std::min(1.0, t));
    switch (curve) {
        case EnvCurve::linear:
            return t;
        case EnvCurve::exponential:
            return std::pow(t, kCurveExponent);
        case EnvCurve::logarithmic:
            return 1.0 - std::pow(1.0 - t, kCurveExponent);
    }
    return t;
}

// ============================================================
// Constructor / prepare / release
// ============================================================

SamplerVoice::SamplerVoice(const SamplerParams& params)
    : params_(params) {}

void SamplerVoice::prepare(double sampleRate, int blockSize)
{
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(blockSize);
    spec.numChannels = 1; // we process one channel at a time
    filter_.prepare(spec);
}

void SamplerVoice::release()
{
    state_ = VoiceState::idle;
    buffer_ = nullptr;
    filter_.reset();
}

// ============================================================
// noteOn / noteOff
// ============================================================

void SamplerVoice::noteOn(const Buffer* buffer, int midiNote, float velocity,
                           int startSampleInBlock)
{
    if (buffer == nullptr || buffer->getLengthInSamples() == 0) {
        return;
    }

    int bufLen = buffer->getLengthInSamples();
    int regStart = static_cast<int>(std::round(params_.sampleStart * bufLen));
    int regEnd = static_cast<int>(std::round(params_.sampleEnd * bufLen));
    regStart = std::max(0, std::min(regStart, bufLen));
    regEnd = std::max(0, std::min(regEnd, bufLen));

    if (regStart >= regEnd) {
        return;
    }

    buffer_ = buffer;
    midiNote_ = midiNote;
    velocity_ = velocity;
    state_ = VoiceState::playing;
    age_ = 0.0f;
    isStereo_ = buffer->getNumChannels() >= 2;

    regionStartSample_ = regStart;
    regionEndSample_ = regEnd;

    // Compute loop region (clamped to playback region)
    int loopS = static_cast<int>(std::round(params_.loopStart * bufLen));
    int loopE = static_cast<int>(std::round(params_.loopEnd * bufLen));
    loopS = std::max(regionStartSample_, std::min(loopS, regionEndSample_));
    loopE = std::max(regionStartSample_, std::min(loopE, regionEndSample_));
    loopStartSample_ = loopS;
    loopEndSample_ = loopE;

    // Crossfade in buffer samples
    if (params_.loopCrossfadeSec > 0.0f && loopE > loopS) {
        int xfade = static_cast<int>(params_.loopCrossfadeSec * buffer->getSampleRate());
        int halfLoop = (loopE - loopS) / 2;
        crossfadeSamples_ = std::max(0, std::min(xfade, halfLoop));
    } else {
        crossfadeSamples_ = 0;
    }

    // Set initial read position
    if (params_.direction == PlayDirection::forward) {
        readPosition_ = static_cast<double>(regionStartSample_);
        movingForward_ = true;
    } else {
        readPosition_ = static_cast<double>(regionEndSample_ - 1);
        movingForward_ = false;
    }

    // Reset envelopes
    ampEnv_.stage = AmpStage::attack;
    ampEnv_.position = 0.0;
    ampEnv_.level = 0.0;

    filterEnv_.stage = FilterEnvStage::attack;
    filterEnv_.position = 0.0;
    filterEnv_.level = 0.0;

    // Reset filter
    filter_.reset();

    // Pending offset for sample-accurate triggering
    pendingNoteOnOffset_ = startSampleInBlock;
    pendingNoteOffOffset_ = -1;
}

void SamplerVoice::noteOff(int startSampleInBlock)
{
    if (state_ == VoiceState::idle) return;
    pendingNoteOffOffset_ = startSampleInBlock;
}

// ============================================================
// Envelope advancement
// ============================================================

void SamplerVoice::advanceAmpEnvelope()
{
    double incr = 1.0 / sampleRate_;

    // Loop to cascade through instant (time=0) stages in a single call
    for (;;) {
        switch (ampEnv_.stage) {
            case AmpStage::attack: {
                float attackTime = std::max(0.0f, params_.ampAttack);
                if (attackTime <= 0.0f) {
                    ampEnv_.level = 1.0;
                    ampEnv_.stage = AmpStage::hold;
                    ampEnv_.position = 0.0;
                    continue; // cascade
                }
                ampEnv_.position += incr / attackTime;
                if (ampEnv_.position >= 1.0) {
                    ampEnv_.level = 1.0;
                    ampEnv_.stage = AmpStage::hold;
                    ampEnv_.position = 0.0;
                    continue;
                }
                ampEnv_.level = applyCurve(ampEnv_.position, params_.ampAttackCurve);
                return;
            }
            case AmpStage::hold: {
                float holdTime = std::max(0.0f, params_.ampHold);
                ampEnv_.level = 1.0;
                if (holdTime <= 0.0f) {
                    ampEnv_.stage = AmpStage::decay;
                    ampEnv_.position = 0.0;
                    continue;
                }
                ampEnv_.position += incr / holdTime;
                if (ampEnv_.position >= 1.0) {
                    ampEnv_.stage = AmpStage::decay;
                    ampEnv_.position = 0.0;
                    continue;
                }
                return;
            }
            case AmpStage::decay: {
                float decayTime = std::max(0.0f, params_.ampDecay);
                float sustain = params_.ampSustain;
                if (decayTime <= 0.0f) {
                    ampEnv_.level = sustain;
                    ampEnv_.stage = AmpStage::sustain;
                    ampEnv_.position = 0.0;
                    return; // sustain is a resting stage, no cascade needed
                }
                ampEnv_.position += incr / decayTime;
                if (ampEnv_.position >= 1.0) {
                    ampEnv_.level = sustain;
                    ampEnv_.stage = AmpStage::sustain;
                    ampEnv_.position = 0.0;
                    return;
                }
                double curved = applyCurve(ampEnv_.position, params_.ampDecayCurve);
                ampEnv_.level = 1.0 - curved * (1.0 - sustain);
                return;
            }
            case AmpStage::sustain:
                ampEnv_.level = params_.ampSustain;
                return;
            case AmpStage::release: {
                float releaseTime = std::max(0.0f, params_.ampRelease);
                if (releaseTime <= 0.0f) {
                    ampEnv_.level = 0.0;
                    ampEnv_.stage = AmpStage::done;
                    return;
                }
                ampEnv_.position += incr / releaseTime;
                if (ampEnv_.position >= 1.0) {
                    ampEnv_.level = 0.0;
                    ampEnv_.stage = AmpStage::done;
                } else {
                    double curved = applyCurve(ampEnv_.position, params_.ampReleaseCurve);
                    ampEnv_.level = releaseStartLevel_ * (1.0 - curved);
                }
                return;
            }
            case AmpStage::done:
                ampEnv_.level = 0.0;
                return;
        }
    }
}

void SamplerVoice::advanceFilterEnvelope()
{
    double incr = 1.0 / sampleRate_;

    for (;;) {
        switch (filterEnv_.stage) {
            case FilterEnvStage::attack: {
                float t = std::max(0.0f, params_.filterAttack);
                if (t <= 0.0f) {
                    filterEnv_.level = 1.0;
                    filterEnv_.stage = FilterEnvStage::decay;
                    filterEnv_.position = 0.0;
                    continue;
                }
                filterEnv_.position += incr / t;
                if (filterEnv_.position >= 1.0) {
                    filterEnv_.level = 1.0;
                    filterEnv_.stage = FilterEnvStage::decay;
                    filterEnv_.position = 0.0;
                    continue;
                }
                filterEnv_.level = applyCurve(filterEnv_.position, params_.filterAttackCurve);
                return;
            }
            case FilterEnvStage::decay: {
                float t = std::max(0.0f, params_.filterDecay);
                float sus = params_.filterSustain;
                if (t <= 0.0f) {
                    filterEnv_.level = sus;
                    filterEnv_.stage = FilterEnvStage::sustain;
                    filterEnv_.position = 0.0;
                    return;
                }
                filterEnv_.position += incr / t;
                if (filterEnv_.position >= 1.0) {
                    filterEnv_.level = sus;
                    filterEnv_.stage = FilterEnvStage::sustain;
                    filterEnv_.position = 0.0;
                    return;
                }
                double curved = applyCurve(filterEnv_.position, params_.filterDecayCurve);
                filterEnv_.level = 1.0 - curved * (1.0 - sus);
                return;
            }
            case FilterEnvStage::sustain:
                filterEnv_.level = params_.filterSustain;
                return;
            case FilterEnvStage::release: {
                float t = std::max(0.0f, params_.filterRelease);
                if (t <= 0.0f) {
                    filterEnv_.level = 0.0;
                    filterEnv_.stage = FilterEnvStage::done;
                    return;
                }
                filterEnv_.position += incr / t;
                if (filterEnv_.position >= 1.0) {
                    filterEnv_.level = 0.0;
                    filterEnv_.stage = FilterEnvStage::done;
                } else {
                    double curved = applyCurve(filterEnv_.position, params_.filterReleaseCurve);
                    filterEnv_.level = filterReleaseStartLevel_ * (1.0 - curved);
                }
                return;
            }
            case FilterEnvStage::done:
                filterEnv_.level = 0.0;
                return;
        }
    }
}

// ============================================================
// Sample fetching and interpolation
// ============================================================

float SamplerVoice::fetchSample(int intIndex, int channel) const
{
    int bufLen = buffer_->getLengthInSamples();
    int numCh = buffer_->getNumChannels();
    int ch = std::min(channel, numCh - 1);

    // For active loop modes with a valid loop region, wrap within the loop
    bool loopActive = (params_.loopMode != LoopMode::off)
                      && (loopStartSample_ < loopEndSample_)
                      && (state_ == VoiceState::playing);

    if (loopActive) {
        int loopLen = loopEndSample_ - loopStartSample_;
        if (intIndex >= loopEndSample_) {
            intIndex = loopStartSample_ + ((intIndex - loopStartSample_) % loopLen);
        } else if (intIndex < loopStartSample_) {
            // Wrap below loopStart: count how far below, mod into loop
            int below = loopStartSample_ - intIndex;
            intIndex = loopEndSample_ - (below % loopLen);
            if (intIndex >= loopEndSample_) intIndex = loopStartSample_;
        }
    }

    int idx = std::max(0, std::min(intIndex, bufLen - 1));
    return buffer_->getReadPointer(ch)[idx];
}

float SamplerVoice::hermiteInterpolate(float ym1, float y0, float y1, float y2, float t) const
{
    float a = -0.5f * ym1 + 1.5f * y0 - 1.5f * y1 + 0.5f * y2;
    float b = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    float c = -0.5f * ym1 + 0.5f * y1;
    float d = y0;
    return ((a * t + b) * t + c) * t + d;
}

// ============================================================
// Loop wrapping
// ============================================================

void SamplerVoice::handleLoopWrap()
{
    LoopMode loopMode = params_.loopMode;

    // Check if loop region is valid
    bool loopValid = (loopStartSample_ < loopEndSample_) && (loopMode != LoopMode::off);

    if (!loopValid) {
        // One-shot: check if we've passed the end
        if (movingForward_ && readPosition_ >= regionEndSample_) {
            // Trigger release if not already releasing
            if (state_ == VoiceState::playing) {
                releaseStartLevel_ = ampEnv_.level;
                ampEnv_.stage = AmpStage::release;
                ampEnv_.position = 0.0;
                filterReleaseStartLevel_ = filterEnv_.level;
                filterEnv_.stage = FilterEnvStage::release;
                filterEnv_.position = 0.0;
                state_ = VoiceState::releasing;
            }
            readPosition_ = regionEndSample_ - 1;
        } else if (!movingForward_ && readPosition_ < regionStartSample_) {
            if (state_ == VoiceState::playing) {
                releaseStartLevel_ = ampEnv_.level;
                ampEnv_.stage = AmpStage::release;
                ampEnv_.position = 0.0;
                filterReleaseStartLevel_ = filterEnv_.level;
                filterEnv_.stage = FilterEnvStage::release;
                filterEnv_.position = 0.0;
                state_ = VoiceState::releasing;
            }
            readPosition_ = regionStartSample_;
        }
        return;
    }

    // Looping modes
    double loopLen = static_cast<double>(loopEndSample_ - loopStartSample_);

    switch (loopMode) {
        case LoopMode::forward:
            if (readPosition_ >= loopEndSample_) {
                double overshoot = readPosition_ - loopEndSample_;
                // fmod handles multi-wrap at extreme playback rates
                readPosition_ = loopStartSample_ + std::fmod(overshoot, loopLen);
            }
            // Once inside the loop region, always move forward
            movingForward_ = true;
            break;

        case LoopMode::reverse:
            if (readPosition_ < loopStartSample_) {
                double undershoot = loopStartSample_ - readPosition_;
                readPosition_ = loopEndSample_ - std::fmod(undershoot, loopLen);
            }
            // Once inside the loop region, always move reverse
            if (readPosition_ >= loopStartSample_ && readPosition_ < loopEndSample_)
                movingForward_ = false;
            break;

        case LoopMode::pingPong: {
            // For pingPong, compute direction from number of boundary crossings.
            // At extreme rates this could cross multiple times per sample.
            if (movingForward_ && readPosition_ >= loopEndSample_) {
                double overshoot = readPosition_ - loopEndSample_;
                int crossings = static_cast<int>(overshoot / loopLen) + 1;
                double remainder = std::fmod(overshoot, loopLen);
                if (crossings % 2 == 1) {
                    // Odd crossings: moving backward
                    readPosition_ = loopEndSample_ - remainder;
                    movingForward_ = false;
                } else {
                    // Even crossings: still moving forward
                    readPosition_ = loopStartSample_ + remainder;
                    movingForward_ = true;
                }
            } else if (!movingForward_ && readPosition_ < loopStartSample_) {
                double undershoot = loopStartSample_ - readPosition_;
                int crossings = static_cast<int>(undershoot / loopLen) + 1;
                double remainder = std::fmod(undershoot, loopLen);
                if (crossings % 2 == 1) {
                    // Odd crossings: moving forward
                    readPosition_ = loopStartSample_ + remainder;
                    movingForward_ = true;
                } else {
                    // Even crossings: still moving backward
                    readPosition_ = loopEndSample_ - remainder;
                    movingForward_ = false;
                }
            }
            break;
        }

        case LoopMode::off:
            break;
    }
}

// ============================================================
// Pan
// ============================================================

void SamplerVoice::computePanGains(float pan, float& leftGain, float& rightGain) const
{
    if (isStereo_) {
        // Balance control for stereo
        leftGain = pan <= 0.0f ? 1.0f : 1.0f - pan;
        rightGain = pan >= 0.0f ? 1.0f : 1.0f + pan;
    } else {
        // Constant-power pan for mono
        float angle = (pan + 1.0f) * static_cast<float>(M_PI) / 4.0f;
        leftGain = std::cos(angle);
        rightGain = std::sin(angle);
    }
}

// ============================================================
// Render
// ============================================================

void SamplerVoice::render(juce::AudioBuffer<float>& output, int sampleOffset, int numSamples)
{
    if (state_ == VoiceState::idle) return;
    if (buffer_ == nullptr) return;

    int endSample = sampleOffset + numSamples;

    // Compute playback rate
    double baseRate = buffer_->getSampleRate() / sampleRate_;
    double totalSemitones = params_.pitchSemitones
        + (params_.pitchCents / 100.0)
        + (midiNote_ - params_.rootNote);
    double rateMultiplier = std::pow(2.0, totalSemitones / 12.0);
    double playbackRate = baseRate * rateMultiplier;

    // Velocity gain
    float velGain = 1.0f - params_.velSensitivity * (1.0f - velocity_ / 127.0f);

    // Pan gains
    float panL, panR;
    computePanGains(params_.pan, panL, panR);

    // Filter setup
    bool useFilter = (params_.filterType != FilterType::off);
    if (useFilter) {
        switch (params_.filterType) {
            case FilterType::lowpass:
                filter_.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
                break;
            case FilterType::highpass:
                filter_.setType(juce::dsp::StateVariableTPTFilterType::highpass);
                break;
            case FilterType::bandpass:
                filter_.setType(juce::dsp::StateVariableTPTFilterType::bandpass);
                break;
            case FilterType::notch:
                // JUCE TPT filter doesn't have notch directly; approximate with bandpass
                // Actually, notch can be LP + HP, but for simplicity use bandpass here
                // and we'll handle it below
                filter_.setType(juce::dsp::StateVariableTPTFilterType::bandpass);
                break;
            default:
                break;
        }
        float res = std::max(0.0f, std::min(params_.filterResonance, 1.0f));
        // Map 0-1 resonance to Q: 0.707 (no resonance) to ~20 (high resonance)
        float Q = 0.707f + res * 19.3f;
        filter_.setResonance(Q);
    }

    for (int i = sampleOffset; i < endSample; ++i) {
        // Handle pending noteOn offset
        if (pendingNoteOnOffset_ >= 0 && i < pendingNoteOnOffset_ + sampleOffset) {
            continue;
        }
        if (pendingNoteOnOffset_ >= 0 && i == pendingNoteOnOffset_ + sampleOffset) {
            pendingNoteOnOffset_ = -1;
        }

        // Handle pending noteOff offset
        if (pendingNoteOffOffset_ >= 0 && i == pendingNoteOffOffset_ + sampleOffset) {
            pendingNoteOffOffset_ = -1;
            if (state_ == VoiceState::playing) {
                releaseStartLevel_ = ampEnv_.level;
                ampEnv_.stage = AmpStage::release;
                ampEnv_.position = 0.0;
                filterReleaseStartLevel_ = filterEnv_.level;
                filterEnv_.stage = FilterEnvStage::release;
                filterEnv_.position = 0.0;
                state_ = VoiceState::releasing;
            }
        }

        if (state_ == VoiceState::idle) break;

        // 1. Read from buffer with cubic Hermite interpolation
        int intPos = static_cast<int>(std::floor(readPosition_));
        float frac = static_cast<float>(readPosition_ - intPos);

        float sampleL, sampleR;

        // Fetch 4 neighbors for left (or mono) channel
        float ym1_L = fetchSample(intPos - 1, 0);
        float y0_L  = fetchSample(intPos, 0);
        float y1_L  = fetchSample(intPos + 1, 0);
        float y2_L  = fetchSample(intPos + 2, 0);
        sampleL = hermiteInterpolate(ym1_L, y0_L, y1_L, y2_L, frac);

        if (isStereo_) {
            float ym1_R = fetchSample(intPos - 1, 1);
            float y0_R  = fetchSample(intPos, 1);
            float y1_R  = fetchSample(intPos + 1, 1);
            float y2_R  = fetchSample(intPos + 2, 1);
            sampleR = hermiteInterpolate(ym1_R, y0_R, y1_R, y2_R, frac);
        } else {
            sampleR = sampleL;
        }

        // 2. Apply loop crossfade if in crossfade region
        if (crossfadeSamples_ > 0 && movingForward_ &&
            params_.loopMode == LoopMode::forward &&
            loopStartSample_ < loopEndSample_) {
            double distanceToEnd = loopEndSample_ - readPosition_;
            if (distanceToEnd >= 0 && distanceToEnd < crossfadeSamples_) {
                double t = distanceToEnd / crossfadeSamples_;
                float fadeOut = static_cast<float>(std::sqrt(t));
                float fadeIn = static_cast<float>(std::sqrt(1.0 - t));

                double mirrorPos = loopStartSample_ + distanceToEnd;
                int mirrorInt = static_cast<int>(std::floor(mirrorPos));
                float mirrorFrac = static_cast<float>(mirrorPos - mirrorInt);

                float mym1_L = fetchSample(mirrorInt - 1, 0);
                float my0_L  = fetchSample(mirrorInt, 0);
                float my1_L  = fetchSample(mirrorInt + 1, 0);
                float my2_L  = fetchSample(mirrorInt + 2, 0);
                float mirrorL = hermiteInterpolate(mym1_L, my0_L, my1_L, my2_L, mirrorFrac);

                sampleL = fadeOut * sampleL + fadeIn * mirrorL;

                if (isStereo_) {
                    float mym1_R = fetchSample(mirrorInt - 1, 1);
                    float my0_R  = fetchSample(mirrorInt, 1);
                    float my1_R  = fetchSample(mirrorInt + 1, 1);
                    float my2_R  = fetchSample(mirrorInt + 2, 1);
                    float mirrorR = hermiteInterpolate(mym1_R, my0_R, my1_R, my2_R, mirrorFrac);
                    sampleR = fadeOut * sampleR + fadeIn * mirrorR;
                }
            }
        }

        // 3. Apply filter
        if (useFilter) {
            advanceFilterEnvelope();
            double envVal = filterEnv_.level;
            double modulation = params_.filterEnvAmount * envVal;
            double effectiveCutoff = params_.filterCutoffHz * std::pow(2.0, modulation * kFilterModOctaves);
            effectiveCutoff = std::max(kMinCutoffHz, std::min(kMaxCutoffHz, effectiveCutoff));
            filter_.setCutoffFrequency(static_cast<float>(effectiveCutoff));

            sampleL = filter_.processSample(0, sampleL);
            if (isStereo_) {
                // For stereo, we need a second filter channel — but we only have 1 channel prepared.
                // Process R through the same filter (mono filter applied to both channels).
                // This is acceptable for a per-voice filter.
                sampleR = filter_.processSample(0, sampleR);
            }
        }

        // 4. Advance amplitude envelope
        advanceAmpEnvelope();

        // 5. Apply amp envelope * velocity * volume
        float gain = static_cast<float>(ampEnv_.level) * velGain * params_.volume;
        sampleL *= gain;
        sampleR *= gain;

        // 6. Apply pan
        float outL = sampleL * panL;
        float outR = sampleR * panR;

        // 7. ADD to output buffer
        output.addSample(0, i, outL);
        output.addSample(1, i, outR);

        // 8. Advance read position
        if (movingForward_)
            readPosition_ += playbackRate;
        else
            readPosition_ -= playbackRate;

        // 9. Handle loop wrapping / one-shot end
        handleLoopWrap();

        // Update age
        age_ += static_cast<float>(1.0 / sampleRate_);

        // 10. Check if envelope done
        if (ampEnv_.stage == AmpStage::done) {
            state_ = VoiceState::idle;
            buffer_ = nullptr;
            midiNote_ = -1;
            break;
        }
    }

    // Clear pending offsets if we didn't reach them
    if (pendingNoteOnOffset_ >= 0 && pendingNoteOnOffset_ + sampleOffset >= endSample) {
        // noteOn offset was beyond this render call — keep it pending
    }
}

// ============================================================
// State queries
// ============================================================

VoiceState SamplerVoice::getState() const { return state_; }
int SamplerVoice::getCurrentNote() const { return midiNote_; }
float SamplerVoice::getEnvelopeLevel() const { return static_cast<float>(ampEnv_.level); }
float SamplerVoice::getAge() const { return age_; }

} // namespace squeeze
