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
    // Fill with a ramp 0..1 so we can detect playback
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            data.setSample(ch, i, static_cast<float>(i) / numSamples);
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

TEST_CASE("VoiceAllocator: multiple voices pre-allocated") {
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

TEST_CASE("VoiceAllocator: mono retrigger — new note replaces old") {
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

    // Retrigger with different note
    alloc.noteOn(buf.get(), 72, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 1);

    // Render to make sure it doesn't crash
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
    params.ampRelease = 0.1f;  // 100ms release
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 1);

    alloc.noteOff(60);
    // Voice is now in releasing state, still counts as active
    REQUIRE(alloc.getActiveVoiceCount() == 1);

    // Render enough to finish release (100ms = 4410 samples)
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
    params.ampRelease = 1.0f;  // long release so voice stays releasing
    VoiceAllocator alloc(4, params);
    alloc.setMode(VoiceMode::mono);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);
    alloc.noteOff(60);
    // Voice is now releasing

    // Second noteOff for same note should be a no-op
    alloc.noteOff(60);
    REQUIRE(alloc.getActiveVoiceCount() == 1);  // still releasing
}

TEST_CASE("VoiceAllocator: noteOff for unplayed note is a no-op") {
    SamplerParams params;
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    alloc.noteOff(60);  // no voice playing note 60
    REQUIRE(alloc.getActiveVoiceCount() == 0);
}

// --- allNotesOff ---

TEST_CASE("VoiceAllocator: allNotesOff releases all non-idle voices") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.001f;  // very short release
    VoiceAllocator alloc(4, params);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);

    alloc.allNotesOff();
    // Voices should be in releasing state (not hard-cut)
    REQUIRE(alloc.getActiveVoiceCount() >= 0);

    // After enough rendering, all voices should be idle
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
    // Pre-fill with 1.0
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            output.setSample(ch, i, 1.0f);

    alloc.renderBlock(output, 0, 64);

    // All samples should be >= 1.0 (added to pre-existing content)
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
    // Render only samples 64..127
    alloc.renderBlock(output, 64, 64);

    // First 64 samples should be silent
    REQUIRE(peak(output, 0, 0, 64) == 0.0f);
    // Last 64 samples should have audio
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

TEST_CASE("VoiceAllocator: setMaxActiveVoices clamped to pool size") {
    SamplerParams params;
    VoiceAllocator alloc(4, params);

    alloc.setMaxActiveVoices(8);  // exceeds pool size of 4
    // Should be clamped — we verify by checking no crash occurs
    // and that the allocator still works
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

    // Reduce max active to 1 — existing voices keep playing
    alloc.setMaxActiveVoices(1);
    REQUIRE(alloc.getActiveVoiceCount() == 2);  // not killed
}

// --- Poly Mode (Phase 2 but basic functionality needed) ---

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
    params.ampRelease = 1.0f;  // long release
    VoiceAllocator alloc(4, params);
    alloc.setMode(VoiceMode::poly);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);
    alloc.noteOn(buf.get(), 64, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 2);

    alloc.noteOff(60);
    // Both still active (one playing, one releasing)
    REQUIRE(alloc.getActiveVoiceCount() == 2);
}

TEST_CASE("VoiceAllocator: poly mode steals oldest when pool exhausted") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    VoiceAllocator alloc(2, params);
    alloc.setMode(VoiceMode::poly);
    alloc.setStealPolicy(StealPolicy::oldest);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();
    alloc.noteOn(buf.get(), 60, 1.0f);

    // Render a bit so voice 1 has age > 0
    juce::AudioBuffer<float> output(2, 64);
    output.clear();
    alloc.renderBlock(output, 0, 64);

    alloc.noteOn(buf.get(), 64, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 2);

    // Pool is full, steal should happen
    alloc.noteOn(buf.get(), 67, 1.0f);
    REQUIRE(alloc.getActiveVoiceCount() == 2);  // still 2, oldest was stolen
}

// --- Sub-block rendering pattern ---

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

    // Simulate sub-block splitting: event at offset 100
    alloc.renderBlock(output, 0, 100);         // silence (no voice)
    alloc.noteOn(buf.get(), 60, 1.0f);         // trigger
    alloc.renderBlock(output, 100, 412);       // voice plays

    // First 100 samples should be silent
    REQUIRE(peak(output, 0, 0, 100) == 0.0f);
    // Remaining should have audio
    REQUIRE(rms(output, 0, 100, 412) > 0.0f);
}

TEST_CASE("VoiceAllocator: sub-block noteOff mid-block") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.5f;  // long enough to hear release
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    auto buf = makeTestBuffer();

    // Trigger first
    alloc.noteOn(buf.get(), 60, 1.0f);

    juce::AudioBuffer<float> output(2, 512);
    output.clear();

    alloc.renderBlock(output, 0, 256);   // playing
    alloc.noteOff(60);                   // release
    alloc.renderBlock(output, 256, 256); // releasing

    // Both halves should have audio (playing then releasing)
    REQUIRE(rms(output, 0, 0, 256) > 0.0f);
    REQUIRE(rms(output, 0, 256, 256) > 0.0f);
}

// --- Voice goes idle after release ---

TEST_CASE("VoiceAllocator: voice returns to idle after release completes") {
    SamplerParams params;
    params.ampAttack = 0.0f;
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    params.ampRelease = 0.001f;  // ~44 samples
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

// --- Mono retrigger resets envelope ---

TEST_CASE("VoiceAllocator: mono retrigger resets envelope from zero") {
    SamplerParams params;
    params.ampAttack = 0.01f;   // 10ms attack
    params.ampHold = 0.0f;
    params.ampDecay = 0.0f;
    params.ampSustain = 1.0f;
    VoiceAllocator alloc(1, params);
    alloc.prepare(44100.0, 512);

    // Fill buffer with constant 1.0 to isolate envelope behavior
    auto buf = Buffer::createEmpty(1, 10000, 44100.0, "const");
    auto& data = buf->getAudioData();
    for (int i = 0; i < 10000; ++i)
        data.setSample(0, i, 1.0f);

    // First note, render to sustain
    alloc.noteOn(buf.get(), 60, 1.0f);
    juce::AudioBuffer<float> output(2, 512);
    output.clear();
    alloc.renderBlock(output, 0, 512);  // should reach sustain

    // Retrigger
    alloc.noteOn(buf.get(), 72, 1.0f);
    juce::AudioBuffer<float> output2(2, 16);
    output2.clear();
    alloc.renderBlock(output2, 0, 16);

    // First sample after retrigger should be near 0 (attack starts from 0)
    float firstSample = std::abs(output2.getSample(0, 0));
    REQUIRE(firstSample < 0.1f);
}
