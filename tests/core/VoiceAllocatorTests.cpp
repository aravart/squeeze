#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/VoiceAllocator.h"
#include "core/Buffer.h"

using namespace squeeze;

// --- Helpers ---

static std::unique_ptr<Buffer> makeTestBuffer(int numSamples = 1000,
                                               int numChannels = 1,
                                               double sampleRate = 44100.0) {
    auto buf = Buffer::createEmpty(numChannels, numSamples, sampleRate, "test");
    auto& data = buf->getAudioData();
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            data.setSample(ch, i, static_cast<float>(i) / numSamples);
    return buf;
}

static std::unique_ptr<Buffer> makeConstBuffer(float value = 1.0f,
                                                int numSamples = 10000,
                                                int numChannels = 1) {
    auto buf = Buffer::createEmpty(numChannels, numSamples, 44100.0, "const");
    auto& data = buf->getAudioData();
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            data.setSample(ch, i, value);
    return buf;
}

static float rms(const juce::AudioBuffer<float>& buf, int channel,
                 int start = 0, int numSamples = -1) {
    if (numSamples < 0) numSamples = buf.getNumSamples() - start;
    float sum = 0.0f;
    auto* p = buf.getReadPointer(channel);
    for (int i = start; i < start + numSamples; ++i)
        sum += p[i] * p[i];
    return std::sqrt(sum / numSamples);
}

static float peak(const juce::AudioBuffer<float>& buf, int channel,
                  int start = 0, int numSamples = -1) {
    if (numSamples < 0) numSamples = buf.getNumSamples() - start;
    float mx = 0.0f;
    auto* p = buf.getReadPointer(channel);
    for (int i = start; i < start + numSamples; ++i)
        mx = std::max(mx, std::abs(p[i]));
    return mx;
}

// Max sample-to-sample jump (detects discontinuities)
static float maxJump(const juce::AudioBuffer<float>& buf, int channel,
                     int start = 0, int numSamples = -1) {
    if (numSamples < 0) numSamples = buf.getNumSamples() - start;
    float mx = 0.0f;
    auto* p = buf.getReadPointer(channel);
    for (int i = start + 1; i < start + numSamples; ++i)
        mx = std::max(mx, std::abs(p[i] - p[i - 1]));
    return mx;
}

// --- Lifecycle & State ---

TEST_CASE("VoiceAllocator: default state is mono with 0 active voices") {
    SamplerParams params;
    VoiceAllocator alloc(1, params);

    REQUIRE(alloc.getMode() == VoiceMode::mono);
    REQUIRE(alloc.getMaxVoices() == 1);
    REQUIRE(alloc.getActiveVoiceCount() == 0);
}

TEST_CASE("VoiceAllocator: maxVoices < 1 clamped to 1") {
    SamplerParams params;
    VoiceAllocator alloc(0, params);
    REQUIRE(alloc.getMaxVoices() == 1);

    VoiceAllocator alloc2(-5, params);
    REQUIRE(alloc2.getMaxVoices() == 1);
}

TEST_CASE("VoiceAllocator: prepare and release do not crash") {
    SamplerParams params;
    VoiceAllocator alloc(4, params);
    alloc.prepare(44100.0, 512);
    alloc.release();
}

TEST_CASE("VoiceAllocator: getMaxVoices returns user-facing value, not pool size") {
    SamplerParams params;
    VoiceAllocator alloc(8, params);
    REQUIRE(alloc.getMaxVoices() == 8);
    REQUIRE(alloc.getActiveVoiceCount() == 0);
}

// --- Mono Mode ---

TEST_CASE("VoiceAllocator: mono noteOn triggers one voice") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 1);
}

TEST_CASE("VoiceAllocator: mono noteOn + render produces audio") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);

    juce::AudioBuffer<float> output(2, 64);
    output.clear();
    alloc.renderBlock(output, 0, 64);

    REQUIRE(rms(output, 0) > 0.0f);
}

TEST_CASE("VoiceAllocator: mono retrigger crossfades — 2 active during overlap") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.1f;  // 100ms release for visible overlap
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 1);

    // Retrigger — old voice enters release, new voice starts
    alloc.noteOn(buf.get(), 72, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 2);  // 1 playing + 1 releasing
}

TEST_CASE("VoiceAllocator: mono crossfade — old voice completes release") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.001f;  // ~44 samples
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer(44100);  // long buffer so new voice stays playing
    alloc.noteOn(buf.get(), 60, 1.0f);
    alloc.noteOn(buf.get(), 72, 1.0f);  // retrigger
    REQUIRE(alloc.getActiveVoiceCount() == 2);

    // Render enough for old voice's release to complete (~44 samples)
    juce::AudioBuffer<float> output(2, 512);
    for (int i = 0; i < 5; ++i) {
        output.clear();
        alloc.renderBlock(output, 0, 512);
    }
    REQUIRE(alloc.getActiveVoiceCount() == 1);  // only new voice remains
}

TEST_CASE("VoiceAllocator: mono crossfade produces no pop") {
    SamplerParams params;
    params.ampAttack = 0.001f;  // 1ms attack
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.005f; // 5ms release
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    auto buf = makeConstBuffer(1.0f);

    // Play first note, render to sustain
    alloc.noteOn(buf.get(), 60, 1.0f);
    juce::AudioBuffer<float> warmup(2, 512);
    warmup.clear();
    alloc.renderBlock(warmup, 0, 512);

    // Retrigger mid-block via sub-block pattern
    juce::AudioBuffer<float> output(2, 512);
    output.clear();
    alloc.renderBlock(output, 0, 100);      // old voice at sustain
    alloc.noteOn(buf.get(), 72, 1.0f);      // crossfade retrigger
    alloc.renderBlock(output, 100, 412);    // both voices overlap

    // The transition at sample 100 should be smooth — no large jump
    // Old voice releases from ~1.0, new voice attacks from 0.0
    // Sum should transition smoothly, no single-sample jump > 0.3
    float jump = maxJump(output, 0, 99, 10);  // check around the transition
    REQUIRE(jump < 0.3f);
}

TEST_CASE("VoiceAllocator: rapid mono retrigger with all voices releasing") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 1.0f;  // long release — voices stay releasing
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();

    // First note
    alloc.noteOn(buf.get(), 60, 1.0f);
    // Retrigger — voice[0] releasing, voice[1] playing
    alloc.noteOn(buf.get(), 64, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 2);

    // Retrigger again — voice[1] releasing, no idle voice
    // Must hard-cut quietest releasing voice (voice[0])
    alloc.noteOn(buf.get(), 67, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 2);  // 1 playing + 1 releasing

    // Render to make sure it works
    juce::AudioBuffer<float> output(2, 64);
    output.clear();
    alloc.renderBlock(output, 0, 64);
    REQUIRE(rms(output, 0) > 0.0f);
}

TEST_CASE("VoiceAllocator: mono noteOff transitions to releasing") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.1f;
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 1);

    alloc.noteOff(60);
    REQUIRE(alloc.getActiveVoiceCount() == 1);  // releasing

    juce::AudioBuffer<float> output(2, 512);
    for (int i = 0; i < 20; ++i) {
        output.clear();
        alloc.renderBlock(output, 0, 512);
    }
    REQUIRE(alloc.getActiveVoiceCount() == 0);
}

TEST_CASE("VoiceAllocator: noteOff only matches playing voices, not releasing") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 1.0f;
    VoiceAllocator alloc(4, params);
    alloc.setMode(VoiceMode::mono);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);
    alloc.noteOff(60);

    // Second noteOff for same note — no-op
    alloc.noteOff(60);
    REQUIRE(alloc.getActiveVoiceCount() == 1);  // still releasing
}

TEST_CASE("VoiceAllocator: noteOff for unplayed note is a no-op") {
    SamplerParams params;
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    alloc.noteOff(60);
    REQUIRE(alloc.getActiveVoiceCount() == 0);
}

// --- allNotesOff ---

TEST_CASE("VoiceAllocator: allNotesOff releases all non-idle voices") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.001f;
    VoiceAllocator alloc(4, params);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);

    alloc.allNotesOff();

    juce::AudioBuffer<float> output(2, 512);
    for (int i = 0; i < 10; ++i) {
        output.clear();
        alloc.renderBlock(output, 0, 512);
    }
    REQUIRE(alloc.getActiveVoiceCount() == 0);
}

// --- renderBlock ---

TEST_CASE("VoiceAllocator: renderBlock with no active voices produces silence") {
    SamplerParams params;
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    juce::AudioBuffer<float> output(2, 64);
    output.clear();
    alloc.renderBlock(output, 0, 64);

    REQUIRE(peak(output, 0) == 0.0f);
    REQUIRE(peak(output, 1) == 0.0f);
}

TEST_CASE("VoiceAllocator: renderBlock is additive") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);

    juce::AudioBuffer<float> output(2, 64);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            output.setSample(ch, i, 1.0f);

    alloc.renderBlock(output, 0, 64);

    auto* p = output.getReadPointer(0);
    for (int i = 0; i < 64; ++i)
        REQUIRE(p[i] >= 1.0f);
}

TEST_CASE("VoiceAllocator: renderBlock with startSample offset") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);

    juce::AudioBuffer<float> output(2, 128);
    output.clear();
    alloc.renderBlock(output, 64, 64);

    REQUIRE(peak(output, 0, 0, 64) == 0.0f);
    REQUIRE(rms(output, 0, 64, 64) > 0.0f);
}

// --- Error Conditions ---

TEST_CASE("VoiceAllocator: noteOn with null buffer is ignored") {
    SamplerParams params;
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    alloc.noteOn(nullptr, 60, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 0);
}

TEST_CASE("VoiceAllocator: noteOn with midiNote outside 0-127 is ignored") {
    SamplerParams params;
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), -1, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 0);

    alloc.noteOn(buf.get(), 128, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 0);
}

// --- Configuration ---

TEST_CASE("VoiceAllocator: setMode/getMode") {
    SamplerParams params;
    VoiceAllocator alloc(1, params);

    alloc.setMode(VoiceMode::poly);
    REQUIRE(alloc.getMode() == VoiceMode::poly);

    alloc.setMode(VoiceMode::legato);
    REQUIRE(alloc.getMode() == VoiceMode::legato);

    alloc.setMode(VoiceMode::mono);
    REQUIRE(alloc.getMode() == VoiceMode::mono);
}

TEST_CASE("VoiceAllocator: setMaxActiveVoices clamped to maxVoices") {
    SamplerParams params;
    VoiceAllocator alloc(4, params);

    alloc.setMaxActiveVoices(8);  // exceeds maxVoices of 4
    alloc.prepare(44100.0, 512);
    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 1);
}

TEST_CASE("VoiceAllocator: setMaxActiveVoices does not kill active voices") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    VoiceAllocator alloc(4, params);
    alloc.setMode(VoiceMode::poly);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);
    alloc.noteOn(buf.get(), 64, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 2);

    alloc.setMaxActiveVoices(1);
    REQUIRE(alloc.getActiveVoiceCount() == 2);  // not killed
}

// --- Poly Mode ---

TEST_CASE("VoiceAllocator: poly mode allows multiple simultaneous voices") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    VoiceAllocator alloc(4, params);
    alloc.setMode(VoiceMode::poly);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);
    alloc.noteOn(buf.get(), 64, 1.0f);
    alloc.noteOn(buf.get(), 67, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 3);
}

TEST_CASE("VoiceAllocator: poly noteOff releases only matching playing voice") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 1.0f;
    VoiceAllocator alloc(4, params);
    alloc.setMode(VoiceMode::poly);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);
    alloc.noteOn(buf.get(), 64, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 2);

    alloc.noteOff(60);
    REQUIRE(alloc.getActiveVoiceCount() == 2);  // one playing, one releasing
}

TEST_CASE("VoiceAllocator: poly steal crossfades — victim enters release") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.5f;  // long release to see overlap
    VoiceAllocator alloc(2, params);
    alloc.setMode(VoiceMode::poly);
    alloc.setStealPolicy(StealPolicy::oldest);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);

    // Render a bit so voice 1 has age
    juce::AudioBuffer<float> output(2, 64);
    output.clear();
    alloc.renderBlock(output, 0, 64);

    alloc.noteOn(buf.get(), 64, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 2);

    // Pool has 3 voices (2+1). Both playing, 1 idle.
    // Third note triggers steal: victim (oldest playing) enters release
    alloc.noteOn(buf.get(), 67, 1.0f);
    // Victim is now releasing, new note is playing
    REQUIRE(alloc.getActiveVoiceCount() == 3);  // 2 playing + 1 releasing
}

// --- Sub-block rendering ---

TEST_CASE("VoiceAllocator: sub-block rendering pattern works correctly") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.001f;
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();

    juce::AudioBuffer<float> output(2, 512);
    output.clear();

    alloc.renderBlock(output, 0, 100);
    alloc.noteOn(buf.get(), 60, 1.0f);
    alloc.renderBlock(output, 100, 412);

    REQUIRE(peak(output, 0, 0, 100) == 0.0f);
    REQUIRE(rms(output, 0, 100, 412) > 0.0f);
}

TEST_CASE("VoiceAllocator: sub-block noteOff mid-block") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.5f;
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);

    juce::AudioBuffer<float> output(2, 512);
    output.clear();

    alloc.renderBlock(output, 0, 256);
    alloc.noteOff(60);
    alloc.renderBlock(output, 256, 256);

    REQUIRE(rms(output, 0, 0, 256) > 0.0f);
    REQUIRE(rms(output, 0, 256, 256) > 0.0f);
}

// --- Voice idle after release ---

TEST_CASE("VoiceAllocator: voice returns to idle after release completes") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.001f;
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);
    alloc.noteOff(60);

    juce::AudioBuffer<float> output(2, 512);
    for (int i = 0; i < 5; ++i) {
        output.clear();
        alloc.renderBlock(output, 0, 512);
    }
    REQUIRE(alloc.getActiveVoiceCount() == 0);
}
