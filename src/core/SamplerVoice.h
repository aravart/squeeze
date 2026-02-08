#pragma once

#include "core/Buffer.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace squeeze {

enum class LoopMode { off, forward, reverse, pingPong };
enum class PlayDirection { forward, reverse };
enum class FilterType { off, lowpass, highpass, bandpass, notch };
enum class EnvCurve { linear, exponential, logarithmic };
enum class VoiceState { idle, playing, releasing };

struct SamplerParams {
    // Playback region (normalized to buffer length, 0.0–1.0)
    float sampleStart = 0.0f;
    float sampleEnd = 1.0f;
    int rootNote = 60;

    // Loop
    float loopStart = 0.0f;
    float loopEnd = 1.0f;
    LoopMode loopMode = LoopMode::off;
    float loopCrossfadeSec = 0.0f;

    // Pitch & direction
    PlayDirection direction = PlayDirection::forward;
    int pitchSemitones = 0;
    float pitchCents = 0.0f;

    // Amplitude
    float volume = 1.0f;
    float pan = 0.0f;
    float velSensitivity = 1.0f;

    // Amp envelope (times in seconds, sustain is level)
    float ampAttack = 0.001f;
    float ampHold = 0.0f;
    float ampDecay = 0.1f;
    float ampSustain = 1.0f;
    float ampRelease = 0.01f;
    EnvCurve ampAttackCurve = EnvCurve::linear;
    EnvCurve ampDecayCurve = EnvCurve::exponential;
    EnvCurve ampReleaseCurve = EnvCurve::exponential;

    // Filter
    FilterType filterType = FilterType::off;
    float filterCutoffHz = 20000.0f;
    float filterResonance = 0.0f;
    float filterEnvAmount = 0.0f;

    // Filter envelope (times in seconds, sustain is level)
    float filterAttack = 0.001f;
    float filterDecay = 0.1f;
    float filterSustain = 1.0f;
    float filterRelease = 0.01f;
    EnvCurve filterAttackCurve = EnvCurve::linear;
    EnvCurve filterDecayCurve = EnvCurve::exponential;
    EnvCurve filterReleaseCurve = EnvCurve::exponential;
};

class SamplerVoice {
public:
    explicit SamplerVoice(const SamplerParams& params);

    void prepare(double sampleRate, int blockSize);
    void release();

    void noteOn(const Buffer* buffer, int midiNote, float velocity,
                int startSampleInBlock);
    void noteOff(int startSampleInBlock);

    /// Render audio into output buffer (additive — adds to existing content).
    void render(juce::AudioBuffer<float>& output, int sampleOffset, int numSamples);

    VoiceState getState() const;
    int getCurrentNote() const;
    float getEnvelopeLevel() const;
    float getAge() const;

private:
    enum class AmpStage { attack, hold, decay, sustain, release, done };
    enum class FilterEnvStage { attack, decay, sustain, release, done };

    struct AHDSRState {
        AmpStage stage = AmpStage::done;
        double position = 0.0;   // 0-1 normalized within current stage
        double level = 0.0;
    };

    struct ADSRState {
        FilterEnvStage stage = FilterEnvStage::done;
        double position = 0.0;
        double level = 0.0;
    };

    static double applyCurve(double t, EnvCurve curve);
    void advanceAmpEnvelope();
    void advanceFilterEnvelope();
    float fetchSample(int intIndex, int channel) const;
    float hermiteInterpolate(float ym1, float y0, float y1, float y2, float t) const;
    void handleLoopWrap();
    void computePanGains(float pan, float& leftGain, float& rightGain) const;

    const SamplerParams& params_;
    VoiceState state_ = VoiceState::idle;
    const Buffer* buffer_ = nullptr;
    int midiNote_ = -1;
    float velocity_ = 0.0f;
    double readPosition_ = 0.0;
    double sampleRate_ = 44100.0;
    int blockSize_ = 512;
    float age_ = 0.0f;

    // Derived from buffer + params at noteOn
    int regionStartSample_ = 0;
    int regionEndSample_ = 0;
    int loopStartSample_ = 0;
    int loopEndSample_ = 0;
    int crossfadeSamples_ = 0;
    bool isStereo_ = false;

    // Direction tracking for pingPong
    bool movingForward_ = true;

    // Pending trigger/release offsets within current render block
    int pendingNoteOnOffset_ = -1;
    int pendingNoteOffOffset_ = -1;

    // Envelopes
    AHDSRState ampEnv_;
    ADSRState filterEnv_;
    double releaseStartLevel_ = 0.0;       // amp level when release started
    double filterReleaseStartLevel_ = 0.0;  // filter env level when release started

    // Filter
    juce::dsp::StateVariableTPTFilter<float> filter_;
};

} // namespace squeeze
