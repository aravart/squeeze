#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/PlaybackCursor.h"

#include <cmath>
#include <vector>

using namespace squeeze;
using Catch::Matchers::WithinAbs;

// Helper: create a Buffer filled with a known ramp
static std::unique_ptr<Buffer> makeRampBuffer(int channels, int length, double sr = 44100.0)
{
    auto buf = Buffer::createEmpty(channels, length, sr, "test");
    for (int ch = 0; ch < channels; ++ch)
    {
        float* p = buf->getWritePointer(ch);
        for (int i = 0; i < length; ++i)
            p[i] = static_cast<float>(i) / static_cast<float>(length);
    }
    return buf;
}

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

// ═══════════════════════════════════════════════════════════════════
// Basic playback
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PlaybackCursor renders silence for null buffer")
{
    PlaybackCursor cursor;
    cursor.prepare(44100.0);

    float L[10], R[10];
    int rendered = cursor.render(nullptr, L, R, 10, 1.0,
                                 LoopMode::off, 0.0, 1.0, 0.0);
    CHECK(rendered == 0);
    for (int i = 0; i < 10; ++i)
    {
        CHECK(L[i] == 0.0f);
        CHECK(R[i] == 0.0f);
    }
}

TEST_CASE("PlaybackCursor renders numSamples <= 0 returns 0")
{
    PlaybackCursor cursor;
    cursor.prepare(44100.0);
    auto buf = makeRampBuffer(1, 100);

    float L, R;
    CHECK(cursor.render(buf.get(), &L, &R, 0, 1.0, LoopMode::off, 0.0, 1.0, 0.0) == 0);
    CHECK(cursor.render(buf.get(), &L, &R, -1, 1.0, LoopMode::off, 0.0, 1.0, 0.0) == 0);
}

TEST_CASE("PlaybackCursor reads a ramp buffer at rate 1.0")
{
    PlaybackCursor cursor;
    cursor.prepare(44100.0);
    auto buf = makeRampBuffer(1, 100);

    float L[10], R[10];
    int rendered = cursor.render(buf.get(), L, R, 10, 1.0,
                                 LoopMode::off, 0.0, 1.0, 0.0);
    CHECK(rendered == 10);

    // First sample should be near 0 (position 0)
    CHECK_THAT(static_cast<double>(L[0]), WithinAbs(0.0, 0.01));
    // Samples should be increasing
    for (int i = 1; i < 10; ++i)
        CHECK(L[i] > L[i - 1]);
}

TEST_CASE("PlaybackCursor mono buffer outputs same to L and R")
{
    PlaybackCursor cursor;
    cursor.prepare(44100.0);
    auto buf = makeConstBuffer(1, 100, 0.5f);

    float L[10], R[10];
    cursor.render(buf.get(), L, R, 10, 1.0, LoopMode::off, 0.0, 1.0, 0.0);

    for (int i = 0; i < 10; ++i)
    {
        CHECK_THAT(static_cast<double>(L[i]), WithinAbs(0.5, 0.01));
        CHECK(L[i] == R[i]);
    }
}

TEST_CASE("PlaybackCursor stereo buffer reads two channels")
{
    auto buf = Buffer::createEmpty(2, 100, 44100.0);
    float* ch0 = buf->getWritePointer(0);
    float* ch1 = buf->getWritePointer(1);
    for (int i = 0; i < 100; ++i)
    {
        ch0[i] = 0.25f;
        ch1[i] = 0.75f;
    }

    PlaybackCursor cursor;
    cursor.prepare(44100.0);

    float L[10], R[10];
    cursor.render(buf.get(), L, R, 10, 1.0, LoopMode::off, 0.0, 1.0, 0.0);

    for (int i = 0; i < 10; ++i)
    {
        CHECK_THAT(static_cast<double>(L[i]), WithinAbs(0.25, 0.01));
        CHECK_THAT(static_cast<double>(R[i]), WithinAbs(0.75, 0.01));
    }
}

// ═══════════════════════════════════════════════════════════════════
// Rate
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PlaybackCursor at rate 2.0 advances twice as fast")
{
    PlaybackCursor cursor;
    cursor.prepare(44100.0);
    auto buf = makeRampBuffer(1, 1000);

    float L1[10], R1[10];
    cursor.render(buf.get(), L1, R1, 10, 1.0, LoopMode::off, 0.0, 1.0, 0.0);
    double pos1 = cursor.getRawPosition();

    cursor.reset();
    float L2[10], R2[10];
    cursor.render(buf.get(), L2, R2, 10, 2.0, LoopMode::off, 0.0, 1.0, 0.0);
    double pos2 = cursor.getRawPosition();

    CHECK_THAT(pos2, WithinAbs(pos1 * 2.0, 0.1));
}

// ═══════════════════════════════════════════════════════════════════
// Loop modes
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PlaybackCursor loop off stops at buffer end")
{
    PlaybackCursor cursor;
    cursor.prepare(44100.0);
    auto buf = makeConstBuffer(1, 50, 1.0f);

    float L[100], R[100];
    int rendered = cursor.render(buf.get(), L, R, 100, 1.0,
                                 LoopMode::off, 0.0, 1.0, 0.0);
    CHECK(rendered <= 51); // might render up to length
    CHECK(cursor.isStopped());
}

TEST_CASE("PlaybackCursor forward loop wraps around")
{
    PlaybackCursor cursor;
    cursor.prepare(44100.0);
    auto buf = makeConstBuffer(1, 100, 0.5f);

    float L[200], R[200];
    int rendered = cursor.render(buf.get(), L, R, 200, 1.0,
                                 LoopMode::forward, 0.0, 1.0, 0.0);
    CHECK(rendered == 200);
    CHECK_FALSE(cursor.isStopped());
}

TEST_CASE("PlaybackCursor ping-pong loop reverses direction")
{
    PlaybackCursor cursor;
    cursor.prepare(44100.0);
    auto buf = makeRampBuffer(1, 100);

    float L[250], R[250];
    int rendered = cursor.render(buf.get(), L, R, 250, 1.0,
                                 LoopMode::pingPong, 0.0, 1.0, 0.0);
    CHECK(rendered == 250);
    CHECK_FALSE(cursor.isStopped());
}

TEST_CASE("PlaybackCursor forward loop with sub-region")
{
    PlaybackCursor cursor;
    cursor.prepare(44100.0);
    auto buf = makeRampBuffer(1, 1000);
    cursor.setRawPosition(250.0); // start at loop start

    float L[1000], R[1000];
    int rendered = cursor.render(buf.get(), L, R, 1000, 1.0,
                                 LoopMode::forward, 0.25, 0.75, 0.0);
    CHECK(rendered == 1000);
    // Position should stay within loop region
    double pos = cursor.getRawPosition();
    CHECK(pos >= 250.0 - 1.0);
    CHECK(pos <= 750.0 + 1.0);
}

// ═══════════════════════════════════════════════════════════════════
// Seek
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PlaybackCursor seek sets position correctly")
{
    PlaybackCursor cursor;
    cursor.prepare(44100.0);
    auto buf = makeRampBuffer(1, 1000);

    cursor.seek(0.5, buf.get(), 0.0);
    CHECK_THAT(cursor.getPosition(buf.get()), WithinAbs(0.5, 0.01));
}

TEST_CASE("PlaybackCursor seek clears stopped state")
{
    PlaybackCursor cursor;
    cursor.prepare(44100.0);
    auto buf = makeConstBuffer(1, 10, 1.0f);

    float L[20], R[20];
    cursor.render(buf.get(), L, R, 20, 1.0, LoopMode::off, 0.0, 1.0, 0.0);
    CHECK(cursor.isStopped());

    cursor.seek(0.0, buf.get(), 0.0);
    CHECK_FALSE(cursor.isStopped());
}

TEST_CASE("PlaybackCursor seek with crossfade")
{
    PlaybackCursor cursor;
    cursor.prepare(44100.0);
    auto buf = makeConstBuffer(1, 1000, 0.5f);

    // Render a few samples to be "playing"
    float L[100], R[100];
    cursor.render(buf.get(), L, R, 10, 1.0, LoopMode::off, 0.0, 1.0, 0.0);

    // Seek with crossfade
    cursor.seek(0.5, buf.get(), 32.0);
    int rendered = cursor.render(buf.get(), L, R, 100, 1.0,
                                 LoopMode::off, 0.0, 1.0, 0.0);
    CHECK(rendered > 0);
}

// ═══════════════════════════════════════════════════════════════════
// Position / reset
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PlaybackCursor getPosition returns 0 for null buffer")
{
    PlaybackCursor cursor;
    CHECK(cursor.getPosition(nullptr) == 0.0);
}

TEST_CASE("PlaybackCursor reset returns to initial state")
{
    PlaybackCursor cursor;
    cursor.prepare(44100.0);
    auto buf = makeRampBuffer(1, 100);

    float L[50], R[50];
    cursor.render(buf.get(), L, R, 50, 1.0, LoopMode::off, 0.0, 1.0, 0.0);
    CHECK(cursor.getRawPosition() > 0.0);

    cursor.reset();
    CHECK(cursor.getRawPosition() == 0.0);
    CHECK_FALSE(cursor.isStopped());
}

TEST_CASE("PlaybackCursor setRawPosition sets exact sample position")
{
    PlaybackCursor cursor;
    cursor.setRawPosition(123.456);
    CHECK_THAT(cursor.getRawPosition(), WithinAbs(123.456, 1e-9));
}

// ═══════════════════════════════════════════════════════════════════
// Sample rate compensation
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PlaybackCursor compensates for sample rate mismatch")
{
    auto buf = makeRampBuffer(1, 1000, 48000.0);

    PlaybackCursor cursor;
    cursor.prepare(24000.0); // engine at half the buffer rate

    float L[10], R[10];
    cursor.render(buf.get(), L, R, 10, 1.0, LoopMode::off, 0.0, 1.0, 0.0);

    // At rate 1.0 with 48k buffer on 24k engine: advance 2 samples per output sample
    CHECK_THAT(cursor.getRawPosition(), WithinAbs(20.0, 0.5));
}

// ═══════════════════════════════════════════════════════════════════
// Stopped state
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PlaybackCursor stopped state renders silence")
{
    PlaybackCursor cursor;
    cursor.prepare(44100.0);
    auto buf = makeConstBuffer(1, 10, 1.0f);

    float L[20], R[20];
    cursor.render(buf.get(), L, R, 20, 1.0, LoopMode::off, 0.0, 1.0, 0.0);
    CHECK(cursor.isStopped());

    // Subsequent render should output silence
    float L2[5], R2[5];
    int rendered = cursor.render(buf.get(), L2, R2, 5, 1.0,
                                 LoopMode::off, 0.0, 1.0, 0.0);
    CHECK(rendered == 0);
    for (int i = 0; i < 5; ++i)
    {
        CHECK(L2[i] == 0.0f);
        CHECK(R2[i] == 0.0f);
    }
}

// ═══════════════════════════════════════════════════════════════════
// Invalid loop region
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PlaybackCursor treats loopStart >= loopEnd as full buffer")
{
    PlaybackCursor cursor;
    cursor.prepare(44100.0);
    auto buf = makeConstBuffer(1, 100, 0.5f);

    float L[200], R[200];
    int rendered = cursor.render(buf.get(), L, R, 200, 1.0,
                                 LoopMode::forward, 0.8, 0.2, 0.0);
    CHECK(rendered == 200);
}
