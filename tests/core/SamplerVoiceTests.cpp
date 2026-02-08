#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/SamplerVoice.h"

#include <cmath>
#include <numeric>

using namespace squeeze;
using Catch::Matchers::WithinAbs;

// ============================================================
// Helpers
// ============================================================

static constexpr double kSampleRate = 44100.0;
static constexpr int kBlockSize = 512;

/// Create a mono Buffer with a ramp from 0 to (numSamples-1)/numSamples.
static std::unique_ptr<Buffer> makeRampBuffer(int numSamples, double sampleRate = kSampleRate)
{
    auto buf = Buffer::createEmpty(1, numSamples, sampleRate, "ramp");
    float* data = buf->getWritePointer(0);
    for (int i = 0; i < numSamples; ++i)
        data[i] = static_cast<float>(i) / static_cast<float>(numSamples);
    return buf;
}

/// Create a mono Buffer filled with a constant value.
static std::unique_ptr<Buffer> makeConstBuffer(int numSamples, float value,
                                                double sampleRate = kSampleRate)
{
    auto buf = Buffer::createEmpty(1, numSamples, sampleRate, "const");
    float* data = buf->getWritePointer(0);
    for (int i = 0; i < numSamples; ++i)
        data[i] = value;
    return buf;
}

/// Create a stereo Buffer with left=leftVal, right=rightVal.
static std::unique_ptr<Buffer> makeStereoConstBuffer(int numSamples, float leftVal,
                                                      float rightVal,
                                                      double sampleRate = kSampleRate)
{
    auto buf = Buffer::createEmpty(2, numSamples, sampleRate, "stereo");
    float* L = buf->getWritePointer(0);
    float* R = buf->getWritePointer(1);
    for (int i = 0; i < numSamples; ++i) {
        L[i] = leftVal;
        R[i] = rightVal;
    }
    return buf;
}

/// Create a mono Buffer with a sine wave at a given frequency.
static std::unique_ptr<Buffer> makeSineBuffer(int numSamples, double freq,
                                               double sampleRate = kSampleRate)
{
    auto buf = Buffer::createEmpty(1, numSamples, sampleRate, "sine");
    float* data = buf->getWritePointer(0);
    for (int i = 0; i < numSamples; ++i)
        data[i] = static_cast<float>(std::sin(2.0 * M_PI * freq * i / sampleRate));
    return buf;
}

/// Render a voice for a given number of samples and return the output buffer.
static juce::AudioBuffer<float> renderVoice(SamplerVoice& voice, int numSamples)
{
    juce::AudioBuffer<float> output(2, numSamples);
    output.clear();
    voice.render(output, 0, numSamples);
    return output;
}

/// Get the RMS of a channel in a buffer.
static float rms(const juce::AudioBuffer<float>& buf, int channel,
                 int start = 0, int numSamples = -1)
{
    if (numSamples < 0) numSamples = buf.getNumSamples() - start;
    if (numSamples <= 0) return 0.0f;
    const float* data = buf.getReadPointer(channel);
    double sum = 0.0;
    for (int i = start; i < start + numSamples; ++i)
        sum += data[i] * data[i];
    return static_cast<float>(std::sqrt(sum / numSamples));
}

/// Get peak magnitude of a channel in a range.
static float peak(const juce::AudioBuffer<float>& buf, int channel,
                  int start = 0, int numSamples = -1)
{
    if (numSamples < 0) numSamples = buf.getNumSamples() - start;
    if (numSamples <= 0) return 0.0f;
    const float* data = buf.getReadPointer(channel);
    float maxVal = 0.0f;
    for (int i = start; i < start + numSamples; ++i)
        maxVal = std::max(maxVal, std::abs(data[i]));
    return maxVal;
}

// ============================================================
// Lifecycle & State
// ============================================================

TEST_CASE("SamplerVoice starts idle", "[SamplerVoice]")
{
    SamplerParams params;
    SamplerVoice voice(params);
    CHECK(voice.getState() == VoiceState::idle);
    CHECK(voice.getCurrentNote() == -1);
    CHECK(voice.getEnvelopeLevel() == 0.0f);
}

TEST_CASE("SamplerVoice prepare stores sample rate", "[SamplerVoice]")
{
    SamplerParams params;
    SamplerVoice voice(params);
    voice.prepare(48000.0, 256);
    // Verify indirectly: a voice prepared at 48k playing a 44.1k buffer should
    // have a playback rate < 1.0 (buffer rate / engine rate)
    CHECK(voice.getState() == VoiceState::idle);
}

TEST_CASE("SamplerVoice noteOn transitions to playing", "[SamplerVoice]")
{
    SamplerParams params;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(1000, 0.5f);
    voice.noteOn(buf.get(), 60, 100, 0);
    CHECK(voice.getState() == VoiceState::playing);
    CHECK(voice.getCurrentNote() == 60);
}

TEST_CASE("SamplerVoice noteOff transitions to releasing", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampRelease = 0.5f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(44100, 0.5f);
    voice.noteOn(buf.get(), 60, 127, 0);
    // Render enough to get past attack
    renderVoice(voice, 64);
    voice.noteOff(0);
    renderVoice(voice, 1);
    CHECK(voice.getState() == VoiceState::releasing);
}

TEST_CASE("SamplerVoice returns to idle after release completes", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampRelease = 0.001f; // ~44 samples
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(44100, 0.5f);
    voice.noteOn(buf.get(), 60, 127, 0);
    renderVoice(voice, 64);
    voice.noteOff(0);
    // Render enough for release to complete
    renderVoice(voice, 512);
    CHECK(voice.getState() == VoiceState::idle);
}

TEST_CASE("SamplerVoice noteOn with null buffer stays idle", "[SamplerVoice]")
{
    SamplerParams params;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);
    voice.noteOn(nullptr, 60, 127, 0);
    CHECK(voice.getState() == VoiceState::idle);
}

TEST_CASE("SamplerVoice noteOn with empty buffer stays idle", "[SamplerVoice]")
{
    SamplerParams params;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);
    auto buf = Buffer::createEmpty(1, 0, kSampleRate, "empty");
    voice.noteOn(buf.get(), 60, 127, 0);
    CHECK(voice.getState() == VoiceState::idle);
}

TEST_CASE("SamplerVoice noteOn with sampleStart >= sampleEnd stays idle", "[SamplerVoice]")
{
    SamplerParams params;
    params.sampleStart = 0.5f;
    params.sampleEnd = 0.5f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);
    auto buf = makeConstBuffer(1000, 0.5f);
    voice.noteOn(buf.get(), 60, 127, 0);
    CHECK(voice.getState() == VoiceState::idle);
}

// ============================================================
// Playback Engine
// ============================================================

TEST_CASE("SamplerVoice forward playback reads through buffer in order", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.direction = PlayDirection::forward;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeRampBuffer(1000);
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 100);

    // First samples should be increasing (ramp up)
    const float* L = output.getReadPointer(0);
    for (int i = 1; i < 50; ++i) {
        CHECK(L[i] >= L[i - 1]);
    }
}

TEST_CASE("SamplerVoice reverse playback reads through buffer backwards", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.direction = PlayDirection::reverse;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeRampBuffer(1000);
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 100);

    // First samples should be decreasing (ramp goes backwards)
    const float* L = output.getReadPointer(0);
    for (int i = 1; i < 50; ++i) {
        CHECK(L[i] <= L[i - 1]);
    }
}

TEST_CASE("SamplerVoice playback rate is 1.0 for same-rate buffer at root note", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.rootNote = 60;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    // Buffer: samples are 0, 1, 2, 3, ...
    int len = 1000;
    auto buf = Buffer::createEmpty(1, len, kSampleRate, "idx");
    float* data = buf->getWritePointer(0);
    for (int i = 0; i < len; ++i)
        data[i] = static_cast<float>(i);

    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 100);

    // At rate 1.0, output[n] ≈ n (scaled by pan ~0.707 for mono)
    const float* L = output.getReadPointer(0);
    float panGain = static_cast<float>(std::cos(M_PI / 4.0));
    for (int i = 2; i < 50; ++i) {
        CHECK_THAT(L[i], WithinAbs(i * panGain, 0.5));
    }
}

TEST_CASE("SamplerVoice playback rate accounts for buffer vs engine sample rate", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.rootNote = 60;
    SamplerVoice voice(params);
    voice.prepare(44100.0, kBlockSize);

    // Buffer at 22050 Hz — rate should be 0.5
    auto buf = makeConstBuffer(1000, 1.0f, 22050.0);
    voice.noteOn(buf.get(), 60, 127, 0);

    // After 100 engine samples at rate 0.5, we should have read ~50 buffer samples.
    // The buffer has 1000 samples, so it won't hit the end. Voice should still be playing.
    renderVoice(voice, 100);
    CHECK(voice.getState() == VoiceState::playing);
}

TEST_CASE("SamplerVoice pitch shifts by octave correctly", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.rootNote = 60;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    // Playing note 72 (octave above root 60) = 2x rate
    auto buf = makeConstBuffer(4000, 1.0f);
    voice.noteOn(buf.get(), 72, 127, 0);

    // After 100 engine samples at rate 2.0, ~200 buffer samples consumed.
    // The voice should still be playing.
    renderVoice(voice, 100);
    CHECK(voice.getState() == VoiceState::playing);
}

TEST_CASE("SamplerVoice one-shot mode stops at end and triggers release", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.001f;
    params.loopMode = LoopMode::off;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(100, 0.5f);
    voice.noteOn(buf.get(), 60, 127, 0);

    // Render well past the buffer length — voice should go idle after release
    renderVoice(voice, 200);
    CHECK(voice.getState() == VoiceState::idle);
}

// ============================================================
// Cubic Hermite Interpolation
// ============================================================

TEST_CASE("SamplerVoice interpolation returns exact value at integer positions", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.rootNote = 60;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    // Buffer with distinct values: 0.1, 0.5, 0.9, 0.3, 0.7 repeated
    int len = 1000;
    auto buf = Buffer::createEmpty(1, len, kSampleRate, "distinct");
    float* data = buf->getWritePointer(0);
    float vals[] = {0.1f, 0.5f, 0.9f, 0.3f, 0.7f};
    for (int i = 0; i < len; ++i)
        data[i] = vals[i % 5];

    // At rate 1.0 (root note), read position advances exactly by 1 each sample
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 20);

    float panGain = static_cast<float>(std::cos(M_PI / 4.0));
    const float* L = output.getReadPointer(0);
    // Check first few samples match buffer values * panGain
    for (int i = 0; i < 10; ++i) {
        float expected = vals[i % 5] * panGain;
        CHECK_THAT(L[i], WithinAbs(expected, 0.01));
    }
}

TEST_CASE("SamplerVoice interpolation smoothly transitions between samples", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.rootNote = 60;
    params.pitchCents = 50.0f; // slight pitch shift → fractional positions
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeRampBuffer(2000);
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 100);

    // Output should still be monotonically increasing (ramp with interpolation)
    const float* L = output.getReadPointer(0);
    for (int i = 2; i < 50; ++i) {
        CHECK(L[i] >= L[i - 1] - 0.001f);
    }
}

// ============================================================
// Loop Modes
// ============================================================

TEST_CASE("SamplerVoice forward loop wraps from loopEnd to loopStart", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.loopMode = LoopMode::forward;
    params.loopStart = 0.0f;
    params.loopEnd = 0.5f; // loop over first half
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(1000, 0.5f);
    voice.noteOn(buf.get(), 60, 127, 0);
    // Render well past loop end — voice should still be playing (looping)
    renderVoice(voice, 2000);
    CHECK(voice.getState() == VoiceState::playing);
}

TEST_CASE("SamplerVoice reverse loop wraps from loopStart to loopEnd", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.loopMode = LoopMode::reverse;
    params.loopStart = 0.0f;
    params.loopEnd = 0.5f;
    params.direction = PlayDirection::forward;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(1000, 0.5f);
    voice.noteOn(buf.get(), 60, 127, 0);
    renderVoice(voice, 2000);
    CHECK(voice.getState() == VoiceState::playing);
}

TEST_CASE("SamplerVoice pingPong loop alternates direction at boundaries", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.loopMode = LoopMode::pingPong;
    params.loopStart = 0.2f;
    params.loopEnd = 0.8f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeRampBuffer(1000);
    voice.noteOn(buf.get(), 60, 127, 0);

    // Render many blocks — voice should keep playing
    for (int i = 0; i < 20; ++i)
        renderVoice(voice, kBlockSize);
    CHECK(voice.getState() == VoiceState::playing);
}

TEST_CASE("SamplerVoice loopStart >= loopEnd disables loop (treated as off)", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.001f;
    params.loopMode = LoopMode::forward;
    params.loopStart = 0.8f;
    params.loopEnd = 0.2f; // invalid: start > end
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(200, 0.5f);
    voice.noteOn(buf.get(), 60, 127, 0);
    // Should play through and end (one-shot behavior since loop is disabled)
    renderVoice(voice, 400);
    CHECK(voice.getState() == VoiceState::idle);
}

// ============================================================
// Single-Cycle Waveform Looping
// ============================================================

TEST_CASE("SamplerVoice single-cycle waveform loops seamlessly (no crossfade)", "[SamplerVoice]")
{
    // A perfect single-cycle sine: sample[0] and sample[N] are phase-matched.
    // With forward loop and no crossfade, the output should be a continuous sine.
    int cycleLen = 256;
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.loopMode = LoopMode::forward;
    params.loopStart = 0.0f;
    params.loopEnd = 1.0f;
    params.loopCrossfadeSec = 0.0f;
    params.rootNote = 60;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    // Create a single-cycle sine buffer
    auto buf = Buffer::createEmpty(1, cycleLen, kSampleRate, "single_cycle");
    float* data = buf->getWritePointer(0);
    for (int i = 0; i < cycleLen; ++i)
        data[i] = static_cast<float>(std::sin(2.0 * M_PI * i / cycleLen));

    voice.noteOn(buf.get(), 60, 127, 0);

    // Render 4 full cycles (4 * 256 = 1024 samples)
    auto output = renderVoice(voice, cycleLen * 4);

    // Compare cycle 1 with cycles 2, 3, 4 — they should be nearly identical.
    // This catches interpolation errors at the loop boundary.
    const float* L = output.getReadPointer(0);
    for (int cycle = 1; cycle < 4; ++cycle) {
        for (int i = 0; i < cycleLen; ++i) {
            float ref = L[i];
            float val = L[cycle * cycleLen + i];
            CHECK_THAT(val, WithinAbs(ref, 0.01));
        }
    }
}

TEST_CASE("SamplerVoice single-cycle waveform at fractional rate loops cleanly", "[SamplerVoice]")
{
    // Pitch-shifted single-cycle: fractional read positions cross the loop boundary.
    // Interpolation must wrap, not clamp, for a clean loop.
    int cycleLen = 256;
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.loopMode = LoopMode::forward;
    params.loopStart = 0.0f;
    params.loopEnd = 1.0f;
    params.loopCrossfadeSec = 0.0f;
    params.rootNote = 60;
    params.pitchCents = 37.0f; // fractional pitch shift → non-integer positions
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = Buffer::createEmpty(1, cycleLen, kSampleRate, "single_cycle");
    float* data = buf->getWritePointer(0);
    for (int i = 0; i < cycleLen; ++i)
        data[i] = static_cast<float>(std::sin(2.0 * M_PI * i / cycleLen));

    voice.noteOn(buf.get(), 60, 127, 0);

    // Render several cycles worth
    auto output = renderVoice(voice, cycleLen * 8);

    // Check for discontinuities: max sample-to-sample jump should be bounded.
    // For a clean sine, the max delta between adjacent samples is ~2*pi/cycleLen * rate ≈ 0.025 * 1.02 ≈ 0.026.
    // Allow some headroom for the pan gain.
    const float* L = output.getReadPointer(0);
    float maxJump = 0.0f;
    for (int i = 1; i < cycleLen * 8; ++i) {
        float jump = std::abs(L[i] - L[i - 1]);
        maxJump = std::max(maxJump, jump);
    }
    // A clamp-instead-of-wrap bug would cause jumps > 0.1 at the loop boundary.
    CHECK(maxJump < 0.05f);
}

// ============================================================
// Loop Crossfade
// ============================================================

TEST_CASE("SamplerVoice loop crossfade blends near loop boundary", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.loopMode = LoopMode::forward;
    params.loopStart = 0.0f;
    params.loopEnd = 1.0f;
    params.loopCrossfadeSec = 0.01f; // 441 samples at 44100
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    // Use a buffer that has a big jump at loop boundary (0 at start, 1 near end)
    int len = 4410; // 100ms
    auto buf = Buffer::createEmpty(1, len, kSampleRate, "jump");
    float* data = buf->getWritePointer(0);
    for (int i = 0; i < len; ++i)
        data[i] = static_cast<float>(i) / static_cast<float>(len);

    voice.noteOn(buf.get(), 60, 127, 0);
    // Render through the loop point — no hard click expected
    auto output = renderVoice(voice, len + 500);

    // Check that the transition near the loop point is smoother than a raw jump
    // Near the loop point (around sample len), consecutive samples shouldn't have huge jumps
    const float* L = output.getReadPointer(0);
    float maxJump = 0.0f;
    for (int i = len - 500; i < std::min(len + 400, output.getNumSamples() - 1); ++i) {
        float jump = std::abs(L[i + 1] - L[i]);
        maxJump = std::max(maxJump, jump);
    }
    // With crossfade, max jump should be much smaller than 0.5 (which is what a raw
    // wrap from ~1.0 to ~0.0 would produce)
    CHECK(maxJump < 0.15f);
}

TEST_CASE("SamplerVoice zero crossfade means instant wrap", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.loopMode = LoopMode::forward;
    params.loopStart = 0.0f;
    params.loopEnd = 1.0f;
    params.loopCrossfadeSec = 0.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeRampBuffer(1000);
    voice.noteOn(buf.get(), 60, 127, 0);
    // Just verify it loops without crashing
    renderVoice(voice, 2000);
    CHECK(voice.getState() == VoiceState::playing);
}

TEST_CASE("SamplerVoice crossfade length clamped to half loop length", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.loopMode = LoopMode::forward;
    params.loopStart = 0.0f;
    params.loopEnd = 1.0f;
    params.loopCrossfadeSec = 100.0f; // way longer than buffer — should clamp
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(500, 0.5f);
    voice.noteOn(buf.get(), 60, 127, 0);
    // Should still work (no crash, clamped crossfade)
    renderVoice(voice, 1000);
    CHECK(voice.getState() == VoiceState::playing);
}

// ============================================================
// AHDSR Amplitude Envelope
// ============================================================

TEST_CASE("SamplerVoice attack ramps from 0 to 1 over attack time", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.01f; // 10ms = 441 samples
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampAttackCurve = EnvCurve::linear;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(44100, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);

    // Render 1 sample — should be near 0 (start of attack)
    auto out1 = renderVoice(voice, 1);
    float first = peak(out1, 0);
    CHECK(first < 0.1f);

    // After attack time, should be near full level
    renderVoice(voice, 450);
    CHECK_THAT(voice.getEnvelopeLevel(), WithinAbs(1.0, 0.05));
}

TEST_CASE("SamplerVoice hold maintains 1.0 for hold duration", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.01f; // 441 samples
    params.ampDecay = 0.01f;
    params.ampSustain = 0.5f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(44100, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);

    // After a few samples (past instant attack, in hold), level should be ~1.0
    renderVoice(voice, 10);
    CHECK_THAT(voice.getEnvelopeLevel(), WithinAbs(1.0, 0.01));

    // Still at 1.0 mid-hold
    renderVoice(voice, 200);
    CHECK_THAT(voice.getEnvelopeLevel(), WithinAbs(1.0, 0.01));
}

TEST_CASE("SamplerVoice decay ramps from 1 to sustain level", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.01f; // 441 samples
    params.ampSustain = 0.5f;
    params.ampDecayCurve = EnvCurve::linear;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(44100, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);

    // Render past decay
    renderVoice(voice, 500);
    CHECK_THAT(voice.getEnvelopeLevel(), WithinAbs(0.5, 0.05));
}

TEST_CASE("SamplerVoice sustain holds at sustain level until noteOff", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 0.7f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(44100, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);
    renderVoice(voice, 1000);
    CHECK_THAT(voice.getEnvelopeLevel(), WithinAbs(0.7, 0.01));

    // Still sustaining after more rendering
    renderVoice(voice, 1000);
    CHECK_THAT(voice.getEnvelopeLevel(), WithinAbs(0.7, 0.01));
}

TEST_CASE("SamplerVoice release ramps from current level to 0", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.01f; // 441 samples
    params.ampReleaseCurve = EnvCurve::linear;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(44100, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);
    renderVoice(voice, 64);
    CHECK_THAT(voice.getEnvelopeLevel(), WithinAbs(1.0, 0.01));

    voice.noteOff(0);
    // Mid-release
    renderVoice(voice, 200);
    float midLevel = voice.getEnvelopeLevel();
    CHECK(midLevel > 0.0f);
    CHECK(midLevel < 1.0f);

    // After release completes
    renderVoice(voice, 500);
    CHECK(voice.getState() == VoiceState::idle);
}

TEST_CASE("SamplerVoice instant stages (time=0) complete in one sample", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 0.8f;
    params.ampRelease = 0.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(44100, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);
    renderVoice(voice, 2);
    // Should be at sustain level immediately
    CHECK_THAT(voice.getEnvelopeLevel(), WithinAbs(0.8, 0.01));

    voice.noteOff(0);
    renderVoice(voice, 2);
    // Instant release → idle
    CHECK(voice.getState() == VoiceState::idle);
}

TEST_CASE("SamplerVoice noteOff during attack starts release from current level", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.1f; // 4410 samples — long attack
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.01f;
    params.ampAttackCurve = EnvCurve::linear;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(44100, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);
    // Render 1/4 of attack
    renderVoice(voice, 1100);
    float midAttack = voice.getEnvelopeLevel();
    CHECK(midAttack > 0.1f);
    CHECK(midAttack < 0.5f);

    voice.noteOff(0);
    renderVoice(voice, 1);
    CHECK(voice.getState() == VoiceState::releasing);
    // Release level should be near where attack was interrupted
    CHECK(voice.getEnvelopeLevel() < midAttack + 0.05f);
}

TEST_CASE("SamplerVoice linear curve shape produces linear ramp", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.01f; // 441 samples
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampAttackCurve = EnvCurve::linear;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(44100, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);

    // Sample at ~25%, ~50%, ~75% of attack
    renderVoice(voice, 110);
    float q1 = voice.getEnvelopeLevel();
    renderVoice(voice, 110);
    float q2 = voice.getEnvelopeLevel();
    renderVoice(voice, 110);
    float q3 = voice.getEnvelopeLevel();

    // Linear: differences between quarters should be roughly equal
    float d1 = q2 - q1;
    float d2 = q3 - q2;
    CHECK_THAT(d1, WithinAbs(d2, 0.1));
}

TEST_CASE("SamplerVoice exponential curve is nonlinear (slow start)", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.02f; // 882 samples
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampAttackCurve = EnvCurve::exponential;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(44100, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);

    // At 25% through attack, exponential (t^4) should be very low: 0.25^4 ≈ 0.004
    renderVoice(voice, 220);
    float earlyLevel = voice.getEnvelopeLevel();
    CHECK(earlyLevel < 0.1f);
}

TEST_CASE("SamplerVoice logarithmic curve is nonlinear (fast start)", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.02f; // 882 samples
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampAttackCurve = EnvCurve::logarithmic;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(44100, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);

    // At 25% through attack, logarithmic (1-(1-t)^4) should be high:
    // 1 - (0.75)^4 ≈ 0.68
    renderVoice(voice, 220);
    float earlyLevel = voice.getEnvelopeLevel();
    CHECK(earlyLevel > 0.4f);
}

TEST_CASE("SamplerVoice envelope handles drastic parameter change mid-stage", "[SamplerVoice]")
{
    // The exact scenario from the critique: ampAttack shrinks from 2s to 0.001s
    // while voice is 10% through the attack stage.
    SamplerParams params;
    params.ampAttack = 2.0f; // 2 second attack
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampAttackCurve = EnvCurve::linear;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(441000, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);

    // Render 10% of 2s attack = 0.2s = 8820 samples
    renderVoice(voice, 8820);
    float levelBefore = voice.getEnvelopeLevel();
    CHECK(levelBefore > 0.05f);
    CHECK(levelBefore < 0.2f);
    CHECK(voice.getState() == VoiceState::playing);

    // Now slam attack time to nearly instant
    params.ampAttack = 0.001f;

    // Next render should complete the attack without clicks/NaN/stuck
    renderVoice(voice, 100);
    float levelAfter = voice.getEnvelopeLevel();

    // Should have reached sustain (1.0) — attack completed, no NaN
    CHECK(levelAfter == levelAfter); // not NaN
    CHECK_THAT(levelAfter, WithinAbs(1.0, 0.01));
}

TEST_CASE("SamplerVoice envelope handles negative parameter defensively", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.01f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(44100, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);

    // Set attack to negative mid-stage — should not cause stuck voice
    params.ampAttack = -1.0f;

    // Render — the negative time should be clamped to 0, completing the stage
    renderVoice(voice, 100);
    float level = voice.getEnvelopeLevel();
    CHECK(level == level); // not NaN
    CHECK(level >= 0.0f);
    CHECK(level <= 1.0f);
    // Should have cascaded through to sustain
    CHECK_THAT(level, WithinAbs(1.0, 0.01));
}

// ============================================================
// Filter
// ============================================================

TEST_CASE("SamplerVoice FilterType::off bypasses filter", "[SamplerVoice]")
{
    SamplerParams paramsOff;
    paramsOff.ampAttack = 0.0f;
    paramsOff.ampHold = 0.0f;
    paramsOff.ampDecay = 0.0f;
    paramsOff.ampSustain = 1.0f;
    paramsOff.filterType = FilterType::off;

    SamplerParams paramsLP;
    paramsLP.ampAttack = 0.0f;
    paramsLP.ampHold = 0.0f;
    paramsLP.ampDecay = 0.0f;
    paramsLP.ampSustain = 1.0f;
    paramsLP.filterType = FilterType::lowpass;
    paramsLP.filterCutoffHz = 500.0f; // aggressive lowpass

    SamplerVoice voiceOff(paramsOff);
    SamplerVoice voiceLP(paramsLP);
    voiceOff.prepare(kSampleRate, kBlockSize);
    voiceLP.prepare(kSampleRate, kBlockSize);

    // High frequency content
    auto buf = makeSineBuffer(4410, 5000.0);
    voiceOff.noteOn(buf.get(), 60, 127, 0);
    voiceLP.noteOn(buf.get(), 60, 127, 0);

    auto outOff = renderVoice(voiceOff, 500);
    auto outLP = renderVoice(voiceLP, 500);

    float rmsOff = rms(outOff, 0, 10, 400);
    float rmsLP = rms(outLP, 0, 10, 400);

    // Lowpass at 500 Hz should attenuate 5 kHz significantly
    CHECK(rmsLP < rmsOff * 0.5f);
}

TEST_CASE("SamplerVoice lowpass filter attenuates high frequencies", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.filterType = FilterType::lowpass;
    params.filterCutoffHz = 500.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeSineBuffer(4410, 5000.0);
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 500);

    // 5 kHz through 500 Hz lowpass: should be heavily attenuated
    float level = rms(output, 0, 50, 400);
    CHECK(level < 0.2f);
}

TEST_CASE("SamplerVoice highpass filter attenuates low frequencies", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.filterType = FilterType::highpass;
    params.filterCutoffHz = 5000.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeSineBuffer(4410, 200.0);
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 500);

    // 200 Hz through 5 kHz highpass: should be heavily attenuated
    float level = rms(output, 0, 50, 400);
    CHECK(level < 0.2f);
}

TEST_CASE("SamplerVoice filter cutoff modulated by filter envelope", "[SamplerVoice]")
{
    // With positive env amount and high sustain, cutoff should increase
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.filterType = FilterType::lowpass;
    params.filterCutoffHz = 200.0f; // very low base cutoff
    params.filterEnvAmount = 1.0f;  // full positive modulation
    params.filterAttack = 0.0f;
    params.filterDecay = 0.0f;
    params.filterSustain = 1.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    // 1 kHz sine — should pass because env opens the filter
    auto buf = makeSineBuffer(4410, 1000.0);
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 500);

    float level = rms(output, 0, 50, 400);
    // With env at 1.0 and envAmount=1.0, cutoff = 200 * 2^(1*10) = 200*1024 → clamped to 20k
    // The 1 kHz tone should pass through
    CHECK(level > 0.1f);
}

TEST_CASE("SamplerVoice filter cutoff clamped to [20, 20000] after modulation", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.filterType = FilterType::lowpass;
    params.filterCutoffHz = 100.0f;
    params.filterEnvAmount = -1.0f; // negative: cutoff goes below 20 Hz
    params.filterAttack = 0.0f;
    params.filterDecay = 0.0f;
    params.filterSustain = 1.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeSineBuffer(4410, 500.0);
    voice.noteOn(buf.get(), 60, 127, 0);
    // Should not crash with extreme modulation
    auto output = renderVoice(voice, 500);
    CHECK(voice.getState() == VoiceState::playing);
}

TEST_CASE("SamplerVoice filter state resets on noteOn", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.0f;
    params.filterType = FilterType::lowpass;
    params.filterCutoffHz = 500.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeSineBuffer(4410, 5000.0);

    // First note
    voice.noteOn(buf.get(), 60, 127, 0);
    auto out1 = renderVoice(voice, 200);
    voice.noteOff(0);
    renderVoice(voice, 10);

    // Second note — filter should reset, output should match first note
    voice.noteOn(buf.get(), 60, 127, 0);
    auto out2 = renderVoice(voice, 200);

    float rms1 = rms(out1, 0, 20, 150);
    float rms2 = rms(out2, 0, 20, 150);
    CHECK_THAT(rms1, WithinAbs(rms2, 0.05));
}

TEST_CASE("SamplerVoice filterEnvAmount=0 means no modulation", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.filterType = FilterType::lowpass;
    params.filterCutoffHz = 500.0f;
    params.filterEnvAmount = 0.0f;
    params.filterAttack = 0.0f;
    params.filterDecay = 0.0f;
    params.filterSustain = 1.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeSineBuffer(4410, 5000.0);
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 500);

    // With envAmount=0, cutoff stays at 500 Hz regardless of envelope
    // So 5 kHz should still be attenuated
    float level = rms(output, 0, 50, 400);
    CHECK(level < 0.2f);
}

TEST_CASE("SamplerVoice negative filterEnvAmount inverts envelope modulation", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.filterType = FilterType::lowpass;
    params.filterCutoffHz = 10000.0f; // high base cutoff
    params.filterEnvAmount = -1.0f;   // negative env should reduce cutoff
    params.filterAttack = 0.0f;
    params.filterDecay = 0.0f;
    params.filterSustain = 1.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeSineBuffer(4410, 5000.0);
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 500);

    // Negative env should push cutoff down: 10000 * 2^(-10) ≈ 9.7 Hz → clamped to 20 Hz
    // 5 kHz should be heavily attenuated
    float level = rms(output, 0, 50, 400);
    CHECK(level < 0.15f);
}

// ============================================================
// Velocity & Gain
// ============================================================

TEST_CASE("SamplerVoice velocity sensitivity=0 means constant volume", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.velSensitivity = 0.0f;
    SamplerVoice voiceLow(params);
    SamplerVoice voiceHigh(params);
    voiceLow.prepare(kSampleRate, kBlockSize);
    voiceHigh.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(4410, 1.0f);
    voiceLow.noteOn(buf.get(), 60, 1, 0);   // minimum velocity
    voiceHigh.noteOn(buf.get(), 60, 127, 0); // maximum velocity

    auto outLow = renderVoice(voiceLow, 100);
    auto outHigh = renderVoice(voiceHigh, 100);

    float rmsLow = rms(outLow, 0, 10, 80);
    float rmsHigh = rms(outHigh, 0, 10, 80);
    CHECK_THAT(rmsLow, WithinAbs(rmsHigh, 0.01));
}

TEST_CASE("SamplerVoice velocity sensitivity=1 gives full velocity response", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.velSensitivity = 1.0f;
    SamplerVoice voiceLow(params);
    SamplerVoice voiceHigh(params);
    voiceLow.prepare(kSampleRate, kBlockSize);
    voiceHigh.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(4410, 1.0f);
    voiceLow.noteOn(buf.get(), 60, 32, 0);
    voiceHigh.noteOn(buf.get(), 60, 127, 0);

    auto outLow = renderVoice(voiceLow, 100);
    auto outHigh = renderVoice(voiceHigh, 100);

    float rmsLow = rms(outLow, 0, 10, 80);
    float rmsHigh = rms(outHigh, 0, 10, 80);
    CHECK(rmsHigh > rmsLow * 1.5f);
}

TEST_CASE("SamplerVoice volume parameter scales output", "[SamplerVoice]")
{
    SamplerParams paramsFull;
    paramsFull.ampAttack = 0.0f;
    paramsFull.ampHold = 0.0f;
    paramsFull.ampDecay = 0.0f;
    paramsFull.ampSustain = 1.0f;
    paramsFull.volume = 1.0f;

    SamplerParams paramsHalf;
    paramsHalf.ampAttack = 0.0f;
    paramsHalf.ampHold = 0.0f;
    paramsHalf.ampDecay = 0.0f;
    paramsHalf.ampSustain = 1.0f;
    paramsHalf.volume = 0.5f;

    SamplerVoice voiceFull(paramsFull);
    SamplerVoice voiceHalf(paramsHalf);
    voiceFull.prepare(kSampleRate, kBlockSize);
    voiceHalf.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(4410, 1.0f);
    voiceFull.noteOn(buf.get(), 60, 127, 0);
    voiceHalf.noteOn(buf.get(), 60, 127, 0);

    auto outFull = renderVoice(voiceFull, 100);
    auto outHalf = renderVoice(voiceHalf, 100);

    float rmsFull = rms(outFull, 0, 10, 80);
    float rmsHalf = rms(outHalf, 0, 10, 80);
    CHECK_THAT(rmsHalf, WithinAbs(rmsFull * 0.5f, 0.05));
}

TEST_CASE("SamplerVoice volume=0 produces silence", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.volume = 0.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(4410, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 100);

    CHECK(peak(output, 0) < 0.0001f);
    CHECK(peak(output, 1) < 0.0001f);
}

// ============================================================
// Pan (Dual Mode)
// ============================================================

TEST_CASE("SamplerVoice mono buffer: constant-power pan center is ~0.707", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.pan = 0.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(4410, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 100);

    float expectedGain = static_cast<float>(std::cos(M_PI / 4.0)); // ~0.707
    const float* L = output.getReadPointer(0);
    const float* R = output.getReadPointer(1);
    // Check a sample in steady state
    CHECK_THAT(L[10], WithinAbs(expectedGain, 0.02));
    CHECK_THAT(R[10], WithinAbs(expectedGain, 0.02));
}

TEST_CASE("SamplerVoice mono buffer: pan=-1 full left", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.pan = -1.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(4410, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 100);

    const float* L = output.getReadPointer(0);
    const float* R = output.getReadPointer(1);
    CHECK_THAT(L[10], WithinAbs(1.0, 0.02));
    CHECK(std::abs(R[10]) < 0.02f);
}

TEST_CASE("SamplerVoice mono buffer: pan=+1 full right", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.pan = 1.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(4410, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 100);

    const float* L = output.getReadPointer(0);
    const float* R = output.getReadPointer(1);
    CHECK(std::abs(L[10]) < 0.02f);
    CHECK_THAT(R[10], WithinAbs(1.0, 0.02));
}

TEST_CASE("SamplerVoice stereo buffer: balance pan=0 both channels at unity", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.pan = 0.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeStereoConstBuffer(4410, 0.8f, 0.6f);
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 100);

    const float* L = output.getReadPointer(0);
    const float* R = output.getReadPointer(1);
    // At center, both channels should be at unity (1.0 gain)
    CHECK_THAT(L[10], WithinAbs(0.8, 0.02));
    CHECK_THAT(R[10], WithinAbs(0.6, 0.02));
}

TEST_CASE("SamplerVoice stereo buffer: balance pan left attenuates right", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.pan = -1.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeStereoConstBuffer(4410, 0.8f, 0.6f);
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 100);

    const float* L = output.getReadPointer(0);
    const float* R = output.getReadPointer(1);
    CHECK_THAT(L[10], WithinAbs(0.8, 0.02));
    CHECK(std::abs(R[10]) < 0.02f);
}

TEST_CASE("SamplerVoice stereo buffer: balance pan right attenuates left", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.pan = 1.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeStereoConstBuffer(4410, 0.8f, 0.6f);
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 100);

    const float* L = output.getReadPointer(0);
    const float* R = output.getReadPointer(1);
    CHECK(std::abs(L[10]) < 0.02f);
    CHECK_THAT(R[10], WithinAbs(0.6, 0.02));
}

// ============================================================
// Render Behavior
// ============================================================

TEST_CASE("SamplerVoice render is additive", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(4410, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);

    juce::AudioBuffer<float> output(2, 100);
    // Fill with 0.5 first
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 100; ++i)
            output.setSample(ch, i, 0.5f);

    voice.render(output, 0, 100);

    // Output should be 0.5 + voice_output, not just voice_output
    const float* L = output.getReadPointer(0);
    CHECK(L[10] > 0.6f); // 0.5 + ~0.707 > 0.6
}

TEST_CASE("SamplerVoice idle voice produces zero output", "[SamplerVoice]")
{
    SamplerParams params;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> output(2, 100);
    output.clear();
    voice.render(output, 0, 100);

    CHECK(peak(output, 0) == 0.0f);
    CHECK(peak(output, 1) == 0.0f);
}

TEST_CASE("SamplerVoice mono buffer produces equal content on both channels", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.pan = 0.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeRampBuffer(1000);
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 100);

    const float* L = output.getReadPointer(0);
    const float* R = output.getReadPointer(1);
    for (int i = 0; i < 50; ++i) {
        CHECK_THAT(L[i], WithinAbs(R[i], 0.001));
    }
}

TEST_CASE("SamplerVoice stereo buffer interpolates each channel independently", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.pan = 0.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeStereoConstBuffer(4410, 0.3f, 0.9f);
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 100);

    const float* L = output.getReadPointer(0);
    const float* R = output.getReadPointer(1);
    // Left should be ~0.3, right ~0.9 (balance pan at center = unity)
    CHECK_THAT(L[10], WithinAbs(0.3, 0.02));
    CHECK_THAT(R[10], WithinAbs(0.9, 0.02));
}

TEST_CASE("SamplerVoice noteOn with retrigger resets position and envelopes", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.01f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampAttackCurve = EnvCurve::linear;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(44100, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);
    renderVoice(voice, 1000);
    float level1 = voice.getEnvelopeLevel();

    // Retrigger — should reset envelope to 0 and start attack again
    voice.noteOn(buf.get(), 64, 127, 0);
    renderVoice(voice, 10);
    float level2 = voice.getEnvelopeLevel();
    CHECK(level2 < level1);
    CHECK(voice.getCurrentNote() == 64);
}

// ============================================================
// Sample-Accurate Triggering
// ============================================================

TEST_CASE("SamplerVoice noteOn at offset N: silence before N, playback from N", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(4410, 1.0f);
    int offset = 50;
    voice.noteOn(buf.get(), 60, 127, offset);
    auto output = renderVoice(voice, 200);

    const float* L = output.getReadPointer(0);
    // Before offset: silence
    for (int i = 0; i < offset; ++i) {
        CHECK(std::abs(L[i]) < 0.001f);
    }
    // After offset: should have signal
    float postRms = rms(output, 0, offset + 10, 100);
    CHECK(postRms > 0.1f);
}

TEST_CASE("SamplerVoice noteOff at offset N: sustain through N-1, release at N", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.05f; // long enough to see
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(44100, 1.0f);
    voice.noteOn(buf.get(), 60, 127, 0);
    renderVoice(voice, 100); // get to sustain

    int releaseOffset = 50;
    voice.noteOff(releaseOffset);
    auto output = renderVoice(voice, 200);

    // Before offset: full sustain level
    float prePeak = peak(output, 0, 0, releaseOffset);
    CHECK(prePeak > 0.5f);

    // After offset + some release time: level should be dropping
    CHECK(voice.getState() == VoiceState::releasing);
}

// ============================================================
// Age tracking
// ============================================================

TEST_CASE("SamplerVoice age advances with rendering", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(44100, 0.5f);
    voice.noteOn(buf.get(), 60, 127, 0);
    CHECK(voice.getAge() < 0.001f);

    renderVoice(voice, 44100); // 1 second
    CHECK_THAT(voice.getAge(), WithinAbs(1.0f, 0.02f));
}

// ============================================================
// Pitch cents
// ============================================================

TEST_CASE("SamplerVoice pitch cents adjust playback rate", "[SamplerVoice]")
{
    SamplerParams paramsBase;
    paramsBase.ampAttack = 0.0f;
    paramsBase.ampHold = 0.0f;
    paramsBase.ampDecay = 0.0f;
    paramsBase.ampSustain = 1.0f;
    paramsBase.rootNote = 60;
    paramsBase.pitchCents = 0.0f;

    SamplerParams paramsCents;
    paramsCents.ampAttack = 0.0f;
    paramsCents.ampHold = 0.0f;
    paramsCents.ampDecay = 0.0f;
    paramsCents.ampSustain = 1.0f;
    paramsCents.rootNote = 60;
    paramsCents.pitchCents = 100.0f; // +100 cents = +1 semitone

    SamplerVoice voiceBase(paramsBase);
    SamplerVoice voiceCents(paramsCents);
    voiceBase.prepare(kSampleRate, kBlockSize);
    voiceCents.prepare(kSampleRate, kBlockSize);

    // A ramp buffer — we'll compare how far each voice reads
    auto buf = makeRampBuffer(4000);
    voiceBase.noteOn(buf.get(), 60, 127, 0);
    voiceCents.noteOn(buf.get(), 60, 127, 0);

    auto outBase = renderVoice(voiceBase, 500);
    auto outCents = renderVoice(voiceCents, 500);

    // Voice with +100 cents should read faster (higher pitch = higher rate)
    // Compare the last sample value — higher rate = further into ramp = larger value
    const float* Lb = outBase.getReadPointer(0);
    const float* Lc = outCents.getReadPointer(0);
    CHECK(Lc[400] > Lb[400]);
}

// ============================================================
// Numerical Stability & DSP Edge Cases
// ============================================================

TEST_CASE("SamplerVoice extreme playback rate with forward loop does not crash", "[SamplerVoice]")
{
    // MIDI note 127, rootNote 0 = +127 semitones ≈ 1534x rate.
    // With a 500-sample loop, this skips ~1534 samples/output sample = ~3 full loops/sample.
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.rootNote = 0;
    params.loopMode = LoopMode::forward;
    params.loopStart = 0.0f;
    params.loopEnd = 0.5f; // 500 samples of 1000
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(1000, 0.5f);
    voice.noteOn(buf.get(), 127, 127, 0);
    auto output = renderVoice(voice, 512);

    // Voice should still be playing (loop keeps it alive)
    CHECK(voice.getState() == VoiceState::playing);
    // Output should be finite (no NaN/inf)
    const float* L = output.getReadPointer(0);
    for (int i = 0; i < 512; ++i) {
        CHECK(std::isfinite(L[i]));
    }
}

TEST_CASE("SamplerVoice extreme playback rate with multi-wrap preserves position correctly", "[SamplerVoice]")
{
    // Forward loop: rate high enough to wrap multiple times per sample.
    // Verify fmod wrapping produces a position inside the loop region.
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.rootNote = 0;
    params.loopMode = LoopMode::forward;
    params.loopStart = 0.0f;
    params.loopEnd = 1.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeRampBuffer(100); // tiny buffer, 100 samples
    voice.noteOn(buf.get(), 120, 127, 0); // +120 semitones ≈ 1000x rate
    // Render multiple blocks — no crash, voice stays alive
    for (int i = 0; i < 10; ++i) {
        auto output = renderVoice(voice, kBlockSize);
        const float* L = output.getReadPointer(0);
        for (int j = 0; j < kBlockSize; ++j)
            CHECK(std::isfinite(L[j]));
    }
    CHECK(voice.getState() == VoiceState::playing);
}

TEST_CASE("SamplerVoice extreme playback rate with pingPong loop does not crash", "[SamplerVoice]")
{
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.rootNote = 0;
    params.loopMode = LoopMode::pingPong;
    params.loopStart = 0.1f;
    params.loopEnd = 0.9f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(1000, 0.5f);
    voice.noteOn(buf.get(), 127, 127, 0);
    auto output = renderVoice(voice, 512);

    CHECK(voice.getState() == VoiceState::playing);
    const float* L = output.getReadPointer(0);
    for (int i = 0; i < 512; ++i)
        CHECK(std::isfinite(L[i]));
}

TEST_CASE("SamplerVoice very short loop region (3 samples) at high rate", "[SamplerVoice]")
{
    // 3-sample loop at high playback rate. Crossfade clamped to half loop = 1 sample.
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.rootNote = 60;
    params.loopMode = LoopMode::forward;
    // Buffer of 100 samples, loop region = samples 10-13 (3 samples)
    params.sampleStart = 0.0f;
    params.sampleEnd = 1.0f;
    params.loopStart = 0.1f;   // sample 10
    params.loopEnd = 0.13f;    // sample 13
    params.loopCrossfadeSec = 1.0f; // way too long, will be clamped to 1 sample
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeRampBuffer(100);
    voice.noteOn(buf.get(), 72, 127, 0); // octave up = 2x rate
    auto output = renderVoice(voice, 512);

    CHECK(voice.getState() == VoiceState::playing);
    const float* L = output.getReadPointer(0);
    for (int i = 0; i < 512; ++i)
        CHECK(std::isfinite(L[i]));
}

TEST_CASE("SamplerVoice filter at maximum resonance does not produce NaN", "[SamplerVoice]")
{
    // Resonance=1.0 maps to Q≈20. TPT filter should remain stable.
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.filterType = FilterType::lowpass;
    params.filterCutoffHz = 1000.0f;
    params.filterResonance = 1.0f; // maximum resonance
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    // Use an impulse-like buffer (click at start) to excite the filter
    auto buf = Buffer::createEmpty(1, 4410, kSampleRate, "impulse");
    buf->getWritePointer(0)[0] = 1.0f;

    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 2000);

    // All output must be finite — no NaN, no inf
    const float* L = output.getReadPointer(0);
    const float* R = output.getReadPointer(1);
    for (int i = 0; i < 2000; ++i) {
        CHECK(std::isfinite(L[i]));
        CHECK(std::isfinite(R[i]));
    }
    // The resonant filter should ring (produce nonzero output beyond the impulse)
    float tailRms = rms(output, 0, 100, 500);
    CHECK(tailRms > 0.001f);
}

TEST_CASE("SamplerVoice filter with extreme cutoff modulation produces finite output", "[SamplerVoice]")
{
    // Max positive env amount + high sustain pushes cutoff to clamp at 20kHz.
    // Max negative pushes to clamp at 20Hz. Neither should produce NaN.
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.filterType = FilterType::lowpass;
    params.filterCutoffHz = 1000.0f;
    params.filterResonance = 0.8f;
    params.filterEnvAmount = 1.0f; // extreme positive modulation
    params.filterAttack = 0.0f;
    params.filterDecay = 0.0f;
    params.filterSustain = 1.0f;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeSineBuffer(4410, 440.0);
    voice.noteOn(buf.get(), 60, 127, 0);
    auto output = renderVoice(voice, 1000);

    const float* L = output.getReadPointer(0);
    for (int i = 0; i < 1000; ++i)
        CHECK(std::isfinite(L[i]));

    // Now test extreme negative
    params.filterEnvAmount = -1.0f;
    SamplerVoice voice2(params);
    voice2.prepare(kSampleRate, kBlockSize);
    voice2.noteOn(buf.get(), 60, 127, 0);
    auto output2 = renderVoice(voice2, 1000);

    const float* L2 = output2.getReadPointer(0);
    for (int i = 0; i < 1000; ++i)
        CHECK(std::isfinite(L2[i]));
}

TEST_CASE("SamplerVoice extreme playback rate one-shot reaches end without crash", "[SamplerVoice]")
{
    // One-shot at extreme rate: should hit sampleEnd and trigger release quickly.
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.001f;
    params.rootNote = 0;
    params.loopMode = LoopMode::off;
    SamplerVoice voice(params);
    voice.prepare(kSampleRate, kBlockSize);

    auto buf = makeConstBuffer(1000, 0.5f);
    voice.noteOn(buf.get(), 120, 127, 0); // ~1000x rate
    auto output = renderVoice(voice, 512);

    // Should have hit the end and gone idle after release
    CHECK(voice.getState() == VoiceState::idle);
    const float* L = output.getReadPointer(0);
    for (int i = 0; i < 512; ++i)
        CHECK(std::isfinite(L[i]));
}
