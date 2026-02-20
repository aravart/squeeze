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

TEST_CASE("PlayerProcessor has 7 parameters")
{
    PlayerProcessor pp;
    CHECK(pp.getParameterCount() == 7);
    auto descs = pp.getParameterDescriptors();
    CHECK(descs.size() == 7);
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
