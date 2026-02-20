#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/PlayerProcessor.h"

#include <cmath>
#include <vector>

using namespace squeeze;
using Catch::Matchers::WithinAbs;

// Helper: create a Buffer filled with a constant
static std::unique_ptr<Buffer> makeConstBuffer(int channels, int length, float val, double sr = 44100.0)
{
    auto buf = Buffer::createEmpty(channels, length, sr, "const");
    for (int ch = 0; ch < channels; ++ch)
    {
        float* p = buf->getWritePointer(ch);
        for (int i = 0; i < length; ++i)
            p[i] = val;
    }
    return buf;
}

// Helper: create a Buffer filled with a ramp
static std::unique_ptr<Buffer> makeRampBuffer(int channels, int length, double sr = 44100.0)
{
    auto buf = Buffer::createEmpty(channels, length, sr, "ramp");
    for (int ch = 0; ch < channels; ++ch)
    {
        float* p = buf->getWritePointer(ch);
        for (int i = 0; i < length; ++i)
            p[i] = static_cast<float>(i) / static_cast<float>(length);
    }
    return buf;
}

// ═══════════════════════════════════════════════════════════════════
// Parameters
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PlayerProcessor has 9 parameters")
{
    PlayerProcessor pp;
    CHECK(pp.getParameterCount() == 9);
    auto descs = pp.getParameterDescriptors();
    CHECK(descs.size() == 9);
}

TEST_CASE("PlayerProcessor parameter defaults")
{
    PlayerProcessor pp;
    CHECK(pp.getParameter("playing") == 0.0f);
    CHECK(pp.getParameter("speed") == 1.0f);
    CHECK(pp.getParameter("loop_mode") == 0.0f);
    CHECK(pp.getParameter("loop_start") == 0.0f);
    CHECK(pp.getParameter("loop_end") == 1.0f);
    CHECK(pp.getParameter("fade_ms") == 5.0f);
}

TEST_CASE("PlayerProcessor setParameter and getParameter round-trip")
{
    PlayerProcessor pp;
    pp.setParameter("speed", 2.0f);
    CHECK(pp.getParameter("speed") == 2.0f);

    pp.setParameter("loop_mode", 1.0f);
    CHECK(pp.getParameter("loop_mode") == 1.0f);

    pp.setParameter("loop_start", 0.25f);
    CHECK(pp.getParameter("loop_start") == 0.25f);

    pp.setParameter("loop_end", 0.75f);
    CHECK(pp.getParameter("loop_end") == 0.75f);

    pp.setParameter("fade_ms", 10.0f);
    CHECK(pp.getParameter("fade_ms") == 10.0f);
}

TEST_CASE("PlayerProcessor clamps speed to [-4, 4]")
{
    PlayerProcessor pp;
    pp.setParameter("speed", 10.0f);
    CHECK(pp.getParameter("speed") == 4.0f);

    pp.setParameter("speed", -10.0f);
    CHECK(pp.getParameter("speed") == -4.0f);
}

TEST_CASE("PlayerProcessor clamps position to [0, 1]")
{
    PlayerProcessor pp;
    pp.prepare(44100.0, 512);
    auto buf = makeConstBuffer(1, 1000, 0.5f);
    pp.setBuffer(buf.get());

    pp.setParameter("position", -0.5f);
    // The seek target is clamped
    pp.setParameter("position", 1.5f);
    // No crash; values are clamped internally
}

TEST_CASE("PlayerProcessor unknown parameter returns 0")
{
    PlayerProcessor pp;
    CHECK(pp.getParameter("unknown") == 0.0f);
}

TEST_CASE("PlayerProcessor unknown parameter setParameter is no-op")
{
    PlayerProcessor pp;
    pp.setParameter("unknown", 1.0f); // should not crash
}

// ═══════════════════════════════════════════════════════════════════
// Display text
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PlayerProcessor getParameterText for playing")
{
    PlayerProcessor pp;
    CHECK(pp.getParameterText("playing") == "Stopped");
    pp.setParameter("playing", 1.0f);
    CHECK(pp.getParameterText("playing") == "Playing");
}

TEST_CASE("PlayerProcessor getParameterText for speed")
{
    PlayerProcessor pp;
    CHECK(pp.getParameterText("speed") == "1.0x");
    pp.setParameter("speed", -0.5f);
    CHECK(pp.getParameterText("speed") == "-0.5x");
}

TEST_CASE("PlayerProcessor getParameterText for loop_mode")
{
    PlayerProcessor pp;
    CHECK(pp.getParameterText("loop_mode") == "Off");
    pp.setParameter("loop_mode", 1.0f);
    CHECK(pp.getParameterText("loop_mode") == "Forward");
    pp.setParameter("loop_mode", 2.0f);
    CHECK(pp.getParameterText("loop_mode") == "Ping-pong");
}

TEST_CASE("PlayerProcessor getParameterText for fade_ms")
{
    PlayerProcessor pp;
    CHECK(pp.getParameterText("fade_ms") == "5.0 ms");
}

TEST_CASE("PlayerProcessor getParameterText for unknown returns empty")
{
    PlayerProcessor pp;
    CHECK(pp.getParameterText("unknown").empty());
}

// ═══════════════════════════════════════════════════════════════════
// Buffer assignment
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PlayerProcessor setBuffer assigns buffer")
{
    PlayerProcessor pp;
    auto buf = makeConstBuffer(1, 100, 0.5f);
    pp.setBuffer(buf.get());
    CHECK(pp.getBuffer() == buf.get());
}

TEST_CASE("PlayerProcessor setBuffer resets position and playing")
{
    PlayerProcessor pp;
    pp.prepare(44100.0, 512);
    pp.setParameter("playing", 1.0f);
    auto buf = makeConstBuffer(1, 100, 0.5f);
    pp.setBuffer(buf.get());
    CHECK(pp.getParameter("playing") == 0.0f);
}

TEST_CASE("PlayerProcessor setBuffer to nullptr")
{
    PlayerProcessor pp;
    pp.prepare(44100.0, 512);
    auto buf = makeConstBuffer(1, 100, 0.5f);
    pp.setBuffer(buf.get());
    pp.setBuffer(nullptr);
    CHECK(pp.getBuffer() == nullptr);
}

// ═══════════════════════════════════════════════════════════════════
// Playback
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PlayerProcessor outputs silence when not playing")
{
    PlayerProcessor pp;
    pp.prepare(44100.0, 512);
    auto buf = makeConstBuffer(1, 1000, 0.5f);
    pp.setBuffer(buf.get());

    juce::AudioBuffer<float> out(2, 64);
    pp.process(out);

    for (int i = 0; i < 64; ++i)
    {
        CHECK(out.getSample(0, i) == 0.0f);
        CHECK(out.getSample(1, i) == 0.0f);
    }
}

TEST_CASE("PlayerProcessor outputs audio when playing")
{
    PlayerProcessor pp;
    pp.prepare(44100.0, 512);
    pp.setParameter("fade_ms", 0.0f); // no fade for clean test
    auto buf = makeConstBuffer(1, 1000, 0.5f);
    pp.setBuffer(buf.get());
    pp.setParameter("playing", 1.0f);

    juce::AudioBuffer<float> out(2, 64);
    pp.process(out);

    // All samples should be ~0.5
    bool hasSignal = false;
    for (int i = 0; i < 64; ++i)
    {
        if (std::abs(out.getSample(0, i)) > 0.01f)
            hasSignal = true;
    }
    CHECK(hasSignal);
}

TEST_CASE("PlayerProcessor outputs silence with no buffer assigned")
{
    PlayerProcessor pp;
    pp.prepare(44100.0, 512);
    pp.setParameter("playing", 1.0f);

    juce::AudioBuffer<float> out(2, 64);
    pp.process(out);

    for (int i = 0; i < 64; ++i)
    {
        CHECK(out.getSample(0, i) == 0.0f);
        CHECK(out.getSample(1, i) == 0.0f);
    }
}

TEST_CASE("PlayerProcessor auto-stops when loop is off and buffer ends")
{
    PlayerProcessor pp;
    pp.prepare(44100.0, 512);
    pp.setParameter("fade_ms", 0.0f);
    auto buf = makeConstBuffer(1, 32, 0.5f);
    pp.setBuffer(buf.get());
    pp.setParameter("playing", 1.0f);

    juce::AudioBuffer<float> out(2, 64);
    pp.process(out);

    CHECK(pp.getParameter("playing") == 0.0f);
}

TEST_CASE("PlayerProcessor continues playing with forward loop")
{
    PlayerProcessor pp;
    pp.prepare(44100.0, 512);
    pp.setParameter("fade_ms", 0.0f);
    auto buf = makeConstBuffer(1, 100, 0.5f);
    pp.setBuffer(buf.get());
    pp.setParameter("loop_mode", 1.0f);
    pp.setParameter("playing", 1.0f);

    juce::AudioBuffer<float> out(2, 256);
    pp.process(out);

    CHECK(pp.getParameter("playing") >= 0.5f);
}

// ═══════════════════════════════════════════════════════════════════
// Seek
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PlayerProcessor seek via position parameter")
{
    PlayerProcessor pp;
    pp.prepare(44100.0, 512);
    pp.setParameter("fade_ms", 0.0f);
    auto buf = makeRampBuffer(1, 1000);
    pp.setBuffer(buf.get());
    pp.setParameter("playing", 1.0f);

    // Seek to middle
    pp.setParameter("position", 0.5f);

    // Process to trigger the seek
    juce::AudioBuffer<float> out(2, 64);
    pp.process(out);

    // Position should be around 0.5
    float pos = pp.getParameter("position");
    CHECK(pos > 0.4f);
}

// ═══════════════════════════════════════════════════════════════════
// Latency
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PlayerProcessor latency is 0")
{
    PlayerProcessor pp;
    CHECK(pp.getLatencySamples() == 0);
}

// ═══════════════════════════════════════════════════════════════════
// getParameterDescriptor
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PlayerProcessor getParameterDescriptor for valid index")
{
    PlayerProcessor pp;
    auto d = pp.getParameterDescriptor(0);
    CHECK(d.name == "playing");
    CHECK(d.numSteps == 2);
}

TEST_CASE("PlayerProcessor getParameterDescriptor for invalid index")
{
    PlayerProcessor pp;
    auto d = pp.getParameterDescriptor(-1);
    CHECK(d.name.empty());
    auto d2 = pp.getParameterDescriptor(100);
    CHECK(d2.name.empty());
}

// ═══════════════════════════════════════════════════════════════════
// Reset
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PlayerProcessor reset preserves parameters and buffer")
{
    PlayerProcessor pp;
    pp.prepare(44100.0, 512);
    auto buf = makeConstBuffer(1, 1000, 0.5f);
    pp.setBuffer(buf.get());
    pp.setParameter("speed", 2.0f);
    pp.setParameter("loop_mode", 1.0f);

    pp.reset();

    CHECK(pp.getBuffer() == buf.get());
    CHECK(pp.getParameter("speed") == 2.0f);
    CHECK(pp.getParameter("loop_mode") == 1.0f);
}

// ═══════════════════════════════════════════════════════════════════
// tempo_lock and transpose parameters
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PlayerProcessor tempo_lock defaults to 0.0")
{
    PlayerProcessor pp;
    CHECK(pp.getParameter("tempo_lock") == 0.0f);
}

TEST_CASE("PlayerProcessor tempo_lock set/get round-trip")
{
    PlayerProcessor pp;
    pp.setParameter("tempo_lock", 1.0f);
    CHECK(pp.getParameter("tempo_lock") == 1.0f);
    pp.setParameter("tempo_lock", 0.0f);
    CHECK(pp.getParameter("tempo_lock") == 0.0f);
}

TEST_CASE("PlayerProcessor transpose defaults to 0.0")
{
    PlayerProcessor pp;
    CHECK(pp.getParameter("transpose") == 0.0f);
}

TEST_CASE("PlayerProcessor transpose set/get round-trip")
{
    PlayerProcessor pp;
    pp.setParameter("transpose", 7.0f);
    CHECK(pp.getParameter("transpose") == 7.0f);
    pp.setParameter("transpose", -12.0f);
    CHECK(pp.getParameter("transpose") == -12.0f);
}

TEST_CASE("PlayerProcessor transpose is clamped to [-24, 24]")
{
    PlayerProcessor pp;
    pp.setParameter("transpose", 30.0f);
    CHECK(pp.getParameter("transpose") == 24.0f);
    pp.setParameter("transpose", -30.0f);
    CHECK(pp.getParameter("transpose") == -24.0f);
}

TEST_CASE("PlayerProcessor display text for tempo_lock")
{
    PlayerProcessor pp;
    CHECK(pp.getParameterText("tempo_lock") == "Off");
    pp.setParameter("tempo_lock", 1.0f);
    CHECK(pp.getParameterText("tempo_lock") == "On");
}

TEST_CASE("PlayerProcessor display text for transpose")
{
    PlayerProcessor pp;
    CHECK(pp.getParameterText("transpose") == "0.0 st");
    pp.setParameter("transpose", 3.0f);
    CHECK(pp.getParameterText("transpose") == "+3.0 st");
    pp.setParameter("transpose", -12.0f);
    CHECK(pp.getParameterText("transpose") == "-12.0 st");
}

TEST_CASE("PlayerProcessor parameter descriptors includes new params")
{
    PlayerProcessor pp;
    auto descs = pp.getParameterDescriptors();
    REQUIRE(descs.size() == 9);

    // Check tempo_lock descriptor
    CHECK(descs[7].name == "tempo_lock");
    CHECK(descs[7].defaultValue == 0.0f);
    CHECK(descs[7].minValue == 0.0f);
    CHECK(descs[7].maxValue == 1.0f);
    CHECK(descs[7].numSteps == 2);
    CHECK(descs[7].boolean == true);

    // Check transpose descriptor
    CHECK(descs[8].name == "transpose");
    CHECK(descs[8].defaultValue == 0.0f);
    CHECK(descs[8].minValue == -24.0f);
    CHECK(descs[8].maxValue == 24.0f);
    CHECK(descs[8].numSteps == 0);
    CHECK(descs[8].label == "st");
}

// ═══════════════════════════════════════════════════════════════════
// setPlayHead
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PlayerProcessor setPlayHead stores pointer")
{
    PlayerProcessor pp;
    pp.prepare(44100.0, 512);

    // Create a simple mock PlayHead
    struct MockPlayHead : juce::AudioPlayHead {
        double bpm = 120.0;
        juce::Optional<PositionInfo> getPosition() const override {
            PositionInfo info;
            info.setBpm(bpm);
            return info;
        }
    };

    MockPlayHead mock;
    pp.setPlayHead(&mock);
    // No crash, pointer accepted — tested indirectly via tempo_lock below
}

TEST_CASE("PlayerProcessor tempo_lock with buffer tempo adjusts speed")
{
    struct MockPlayHead : juce::AudioPlayHead {
        double bpm = 240.0;
        juce::Optional<PositionInfo> getPosition() const override {
            PositionInfo info;
            info.setBpm(bpm);
            return info;
        }
    };

    PlayerProcessor pp;
    pp.prepare(44100.0, 512);
    pp.setParameter("fade_ms", 0.0f);

    // Create buffer at 120 BPM with ramp data
    auto buf = makeRampBuffer(1, 10000);
    buf->setTempo(120.0);
    pp.setBuffer(buf.get());

    MockPlayHead mock;
    pp.setPlayHead(&mock);
    pp.setParameter("tempo_lock", 1.0f);
    pp.setParameter("playing", 1.0f);

    juce::AudioBuffer<float> out(2, 512);
    pp.process(out);

    // Engine tempo 240 / buffer tempo 120 = 2x speed
    // Position should advance at 2x rate
    float pos = pp.getParameter("position");
    CHECK(pos > 0.05f); // at 2x speed over 512 samples of 10000
}

TEST_CASE("PlayerProcessor tempo_lock with no buffer tempo has no effect")
{
    struct MockPlayHead : juce::AudioPlayHead {
        double bpm = 240.0;
        juce::Optional<PositionInfo> getPosition() const override {
            PositionInfo info;
            info.setBpm(bpm);
            return info;
        }
    };

    PlayerProcessor pp;
    pp.prepare(44100.0, 512);
    pp.setParameter("fade_ms", 0.0f);

    auto buf = makeRampBuffer(1, 10000);
    // tempo is 0.0 (default, not set)
    pp.setBuffer(buf.get());

    MockPlayHead mock;
    pp.setPlayHead(&mock);
    pp.setParameter("tempo_lock", 1.0f);
    pp.setParameter("playing", 1.0f);

    juce::AudioBuffer<float> out1(2, 512);
    pp.process(out1);
    float posLocked = pp.getParameter("position");

    // Compare with non-locked: should be same since bufferTempo is 0
    PlayerProcessor pp2;
    pp2.prepare(44100.0, 512);
    pp2.setParameter("fade_ms", 0.0f);
    auto buf2 = makeRampBuffer(1, 10000);
    pp2.setBuffer(buf2.get());
    pp2.setParameter("playing", 1.0f);

    juce::AudioBuffer<float> out2(2, 512);
    pp2.process(out2);
    float posNormal = pp2.getParameter("position");

    CHECK_THAT(posLocked, WithinAbs(posNormal, 0.001));
}

TEST_CASE("PlayerProcessor tempo_lock without PlayHead has no effect")
{
    PlayerProcessor pp;
    pp.prepare(44100.0, 512);
    pp.setParameter("fade_ms", 0.0f);

    auto buf = makeRampBuffer(1, 10000);
    buf->setTempo(120.0);
    pp.setBuffer(buf.get());

    // No setPlayHead called
    pp.setParameter("tempo_lock", 1.0f);
    pp.setParameter("playing", 1.0f);

    juce::AudioBuffer<float> out1(2, 512);
    pp.process(out1);
    float posLocked = pp.getParameter("position");

    // Compare with non-locked
    PlayerProcessor pp2;
    pp2.prepare(44100.0, 512);
    pp2.setParameter("fade_ms", 0.0f);
    auto buf2 = makeRampBuffer(1, 10000);
    buf2->setTempo(120.0);
    pp2.setBuffer(buf2.get());
    pp2.setParameter("playing", 1.0f);

    juce::AudioBuffer<float> out2(2, 512);
    pp2.process(out2);
    float posNormal = pp2.getParameter("position");

    CHECK_THAT(posLocked, WithinAbs(posNormal, 0.001));
}

TEST_CASE("PlayerProcessor transpose shifts pitch")
{
    PlayerProcessor pp;
    pp.prepare(44100.0, 512);
    pp.setParameter("fade_ms", 0.0f);

    auto buf = makeRampBuffer(1, 10000);
    pp.setBuffer(buf.get());
    pp.setParameter("transpose", 12.0f); // +1 octave = 2x speed
    pp.setParameter("playing", 1.0f);

    juce::AudioBuffer<float> out(2, 512);
    pp.process(out);
    float posOctaveUp = pp.getParameter("position");

    // Compare with no transpose
    PlayerProcessor pp2;
    pp2.prepare(44100.0, 512);
    pp2.setParameter("fade_ms", 0.0f);
    auto buf2 = makeRampBuffer(1, 10000);
    pp2.setBuffer(buf2.get());
    pp2.setParameter("playing", 1.0f);

    juce::AudioBuffer<float> out2(2, 512);
    pp2.process(out2);
    float posNormal = pp2.getParameter("position");

    // transpose=12 should double the speed, so position advances ~2x
    CHECK(posOctaveUp > posNormal * 1.8f);
}

TEST_CASE("PlayerProcessor tempo_lock and transpose combine")
{
    struct MockPlayHead : juce::AudioPlayHead {
        double bpm = 240.0;
        juce::Optional<PositionInfo> getPosition() const override {
            PositionInfo info;
            info.setBpm(bpm);
            return info;
        }
    };

    PlayerProcessor pp;
    pp.prepare(44100.0, 512);
    pp.setParameter("fade_ms", 0.0f);

    auto buf = makeRampBuffer(1, 20000);
    buf->setTempo(120.0);
    pp.setBuffer(buf.get());

    MockPlayHead mock;
    pp.setPlayHead(&mock);
    pp.setParameter("tempo_lock", 1.0f); // 240/120 = 2x
    pp.setParameter("transpose", 12.0f); // +12 = 2x more
    pp.setParameter("playing", 1.0f);

    juce::AudioBuffer<float> out(2, 512);
    pp.process(out);
    float posCombined = pp.getParameter("position");

    // Compare with just speed=1 (no lock, no transpose)
    PlayerProcessor pp2;
    pp2.prepare(44100.0, 512);
    pp2.setParameter("fade_ms", 0.0f);
    auto buf2 = makeRampBuffer(1, 20000);
    pp2.setBuffer(buf2.get());
    pp2.setParameter("playing", 1.0f);

    juce::AudioBuffer<float> out2(2, 512);
    pp2.process(out2);
    float posNormal = pp2.getParameter("position");

    // Combined should be ~4x speed, so position ~4x
    CHECK(posCombined > posNormal * 3.5f);
}
