#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/Transport.h"

using namespace squeeze;
using Catch::Matchers::WithinAbs;

// ============================================================
// Default state
// ============================================================

TEST_CASE("Transport default state is stopped at position 0", "[Transport]")
{
    Transport t;
    REQUIRE(t.getState() == TransportState::stopped);
    REQUIRE_FALSE(t.isPlaying());
    REQUIRE(t.getPositionInSamples() == 0);
    REQUIRE(t.getTempo() == 120.0);

    auto ts = t.getTimeSignature();
    REQUIRE(ts.numerator == 4);
    REQUIRE(ts.denominator == 4);
}

// ============================================================
// State transitions
// ============================================================

TEST_CASE("Transport play sets state to playing", "[Transport]")
{
    Transport t;
    t.play();
    REQUIRE(t.getState() == TransportState::playing);
    REQUIRE(t.isPlaying());
}

TEST_CASE("Transport stop resets position to 0", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.play();
    t.advance(1000);
    REQUIRE(t.getPositionInSamples() == 1000);

    t.stop();
    REQUIRE(t.getState() == TransportState::stopped);
    REQUIRE(t.getPositionInSamples() == 0);
}

TEST_CASE("Transport pause preserves position", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.play();
    t.advance(512);
    REQUIRE(t.getPositionInSamples() == 512);

    t.pause();
    REQUIRE(t.getState() == TransportState::paused);
    REQUIRE(t.getPositionInSamples() == 512);
}

TEST_CASE("Transport play from paused resumes", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.play();
    t.advance(512);
    t.pause();
    REQUIRE(t.getPositionInSamples() == 512);

    t.play();
    t.advance(512);
    REQUIRE(t.getPositionInSamples() == 1024);
}

// ============================================================
// advance()
// ============================================================

TEST_CASE("Transport advance increases position by exactly numSamples when playing", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.play();
    t.advance(512);
    REQUIRE(t.getPositionInSamples() == 512);
    t.advance(256);
    REQUIRE(t.getPositionInSamples() == 768);
}

TEST_CASE("Transport advance is no-op when stopped", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.advance(512);
    REQUIRE(t.getPositionInSamples() == 0);
}

TEST_CASE("Transport advance is no-op when paused", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.play();
    t.advance(512);
    t.pause();
    t.advance(512);
    REQUIRE(t.getPositionInSamples() == 512);
}

TEST_CASE("Transport advance with 0 or negative numSamples is no-op", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.play();
    t.advance(100);
    REQUIRE(t.getPositionInSamples() == 100);

    t.advance(0);
    REQUIRE(t.getPositionInSamples() == 100);

    t.advance(-1);
    REQUIRE(t.getPositionInSamples() == 100);
}

// ============================================================
// Musical position derivation
// ============================================================

TEST_CASE("Transport derives positionInSeconds from samples and sample rate", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.play();
    t.advance(44100);
    REQUIRE_THAT(t.getPositionInSeconds(), WithinAbs(1.0, 1e-9));
}

TEST_CASE("Transport derives positionInBeats from seconds and tempo", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setTempo(120.0);
    t.play();

    // Advance 1 second at 120 BPM = 2 beats
    t.advance(44100);
    REQUIRE_THAT(t.getPositionInBeats(), WithinAbs(2.0, 1e-9));
}

TEST_CASE("Transport derives positionInBeats from spec example", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setTempo(120.0);
    t.play();

    t.advance(512);
    // positionInBeats = (512 / 44100) * (120 / 60) = 0.023219...
    double expected = (512.0 / 44100.0) * 2.0;
    REQUIRE_THAT(t.getPositionInBeats(), WithinAbs(expected, 1e-9));
}

TEST_CASE("Transport barCount and ppqOfLastBarStart in 4/4", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setTempo(120.0);
    t.setTimeSignature(4, 4);

    // Seek to beat 4.0 = start of bar 1 (0-based)
    t.setPositionInBeats(4.0);
    REQUIRE(t.getPositionInSamples() == 88200);
    REQUIRE(t.getBarCount() == 1);
    REQUIRE_THAT(t.getPpqOfLastBarStart(), WithinAbs(4.0, 1e-9));
}

TEST_CASE("Transport barCount in 3/4 time", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setTempo(120.0);
    t.setTimeSignature(3, 4);

    // 3/4: quarterNotesPerBar = 3 * (4/4) = 3.0
    // At beat 6.0: barCount = floor(6/3) = 2
    t.setPositionInBeats(6.0);
    REQUIRE(t.getBarCount() == 2);
    REQUIRE_THAT(t.getPpqOfLastBarStart(), WithinAbs(6.0, 1e-9));

    // At beat 7.0: barCount = floor(7/3) = 2, lastBarStart = 6.0
    t.setPositionInBeats(7.0);
    REQUIRE(t.getBarCount() == 2);
    REQUIRE_THAT(t.getPpqOfLastBarStart(), WithinAbs(6.0, 1e-9));
}

TEST_CASE("Transport barCount in 6/8 time", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setTempo(120.0);
    t.setTimeSignature(6, 8);

    // 6/8: quarterNotesPerBar = 6 * (4/8) = 3.0
    // At beat 3.0: barCount = floor(3/3) = 1
    t.setPositionInBeats(3.0);
    REQUIRE(t.getBarCount() == 1);
    REQUIRE_THAT(t.getPpqOfLastBarStart(), WithinAbs(3.0, 1e-9));
}

TEST_CASE("Transport barCount in 7/8 time", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setTempo(120.0);
    t.setTimeSignature(7, 8);

    // 7/8: quarterNotesPerBar = 7 * (4/8) = 3.5
    // At beat 7.0: barCount = floor(7/3.5) = 2
    t.setPositionInBeats(7.0);
    REQUIRE(t.getBarCount() == 2);
    REQUIRE_THAT(t.getPpqOfLastBarStart(), WithinAbs(7.0, 1e-9));
}

// ============================================================
// Tempo
// ============================================================

TEST_CASE("Transport tempo is clamped to valid range", "[Transport]")
{
    Transport t;

    t.setTempo(0.5);
    REQUIRE(t.getTempo() == 1.0);

    t.setTempo(1000.0);
    REQUIRE(t.getTempo() == 999.0);

    t.setTempo(140.0);
    REQUIRE(t.getTempo() == 140.0);
}

// ============================================================
// Time signature
// ============================================================

TEST_CASE("Transport time signature rejects invalid denominators", "[Transport]")
{
    Transport t;

    // Invalid: not power of 2
    t.setTimeSignature(4, 3);
    auto ts = t.getTimeSignature();
    REQUIRE(ts.numerator == 4);
    REQUIRE(ts.denominator == 4); // unchanged

    t.setTimeSignature(4, 5);
    ts = t.getTimeSignature();
    REQUIRE(ts.denominator == 4); // unchanged

    t.setTimeSignature(4, 0);
    ts = t.getTimeSignature();
    REQUIRE(ts.denominator == 4); // unchanged

    t.setTimeSignature(4, 64);
    ts = t.getTimeSignature();
    REQUIRE(ts.denominator == 4); // unchanged
}

TEST_CASE("Transport time signature accepts valid denominators", "[Transport]")
{
    Transport t;
    int validDenoms[] = {1, 2, 4, 8, 16, 32};

    for (int d : validDenoms) {
        t.setTimeSignature(4, d);
        REQUIRE(t.getTimeSignature().denominator == d);
    }
}

TEST_CASE("Transport time signature clamps numerator", "[Transport]")
{
    Transport t;

    t.setTimeSignature(0, 4);
    REQUIRE(t.getTimeSignature().numerator == 4); // unchanged

    t.setTimeSignature(33, 4);
    REQUIRE(t.getTimeSignature().numerator == 4); // unchanged

    t.setTimeSignature(7, 8);
    REQUIRE(t.getTimeSignature().numerator == 7);
}

// ============================================================
// Seek
// ============================================================

TEST_CASE("Transport setPositionInSamples clamps negative to 0", "[Transport]")
{
    Transport t;
    t.setPositionInSamples(-100);
    REQUIRE(t.getPositionInSamples() == 0);
}

TEST_CASE("Transport setPositionInSamples sets exact value", "[Transport]")
{
    Transport t;
    t.setPositionInSamples(12345);
    REQUIRE(t.getPositionInSamples() == 12345);
}

TEST_CASE("Transport setPositionInBeats converts via tempo and sample rate", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setTempo(120.0);

    // beats=4.0 → samples = round(4.0 * 60.0 / 120.0 * 44100) = 88200
    t.setPositionInBeats(4.0);
    REQUIRE(t.getPositionInSamples() == 88200);
}

TEST_CASE("Transport setPositionInBeats is no-op before setSampleRate", "[Transport]")
{
    Transport t;
    t.setPositionInBeats(4.0);
    REQUIRE(t.getPositionInSamples() == 0);
}

// ============================================================
// Looping
// ============================================================

TEST_CASE("Transport setLoopPoints rejects end <= start", "[Transport]")
{
    Transport t;
    t.setLoopPoints(4.0, 4.0);
    REQUIRE(t.getLoopStartBeats() == 0.0);
    REQUIRE(t.getLoopEndBeats() == 0.0);

    t.setLoopPoints(8.0, 4.0);
    REQUIRE(t.getLoopStartBeats() == 0.0);
    REQUIRE(t.getLoopEndBeats() == 0.0);
}

TEST_CASE("Transport setLoopPoints accepts valid range", "[Transport]")
{
    Transport t;
    t.setLoopPoints(0.0, 16.0);
    REQUIRE(t.getLoopStartBeats() == 0.0);
    REQUIRE(t.getLoopEndBeats() == 16.0);
}

TEST_CASE("Transport setLooping with both points 0 stays disabled", "[Transport]")
{
    Transport t;
    t.setLooping(true);
    REQUIRE_FALSE(t.isLooping());
}

TEST_CASE("Transport setLooping enables after valid loop points set", "[Transport]")
{
    Transport t;
    t.setLoopPoints(0.0, 16.0);
    t.setLooping(true);
    REQUIRE(t.isLooping());
}

TEST_CASE("Transport advance wraps at loop boundary", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setTempo(120.0);

    t.setLoopPoints(0.0, 16.0);
    t.setLooping(true);
    t.setPositionInBeats(15.9);
    t.play();

    // Advance 1 second = 2 beats at 120 BPM
    // Would reach 15.9 + 2.0 = 17.9, wraps to 1.9
    t.advance(44100);
    REQUIRE_THAT(t.getPositionInBeats(), WithinAbs(1.9, 0.01));
}

TEST_CASE("Transport loop wrap: position stays within [loopStart, loopEnd)", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setTempo(120.0);

    t.setLoopPoints(4.0, 8.0);
    t.setLooping(true);
    t.setPositionInBeats(7.5);
    t.play();

    // Advance 1 second = 2 beats → would reach 9.5
    // loopLen = 4 beats, offset from start = 9.5 - 4.0 = 5.5, 5.5 % 4 = 1.5
    // position = 4.0 + 1.5 = 5.5
    t.advance(44100);
    double pos = t.getPositionInBeats();
    REQUIRE(pos >= 4.0 - 0.01);
    REQUIRE(pos < 8.0 + 0.01);
    REQUIRE_THAT(pos, WithinAbs(5.5, 0.01));
}

TEST_CASE("Transport didLoopWrap returns true after wrap", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setTempo(120.0);

    t.setLoopPoints(0.0, 4.0);
    t.setLooping(true);
    t.setPositionInBeats(3.5);
    t.play();

    REQUIRE_FALSE(t.didLoopWrap());
    t.advance(44100); // +2 beats, 3.5+2=5.5 → wraps
    REQUIRE(t.didLoopWrap());
}

TEST_CASE("Transport didLoopWrap resets to false on next advance", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setTempo(120.0);

    t.setLoopPoints(0.0, 16.0);
    t.setLooping(true);
    t.setPositionInBeats(15.5);
    t.play();

    t.advance(44100); // wraps
    REQUIRE(t.didLoopWrap());

    t.advance(512); // no wrap
    REQUIRE_FALSE(t.didLoopWrap());
}

TEST_CASE("Transport didLoopWrap is false when no wrap occurs", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setTempo(120.0);

    t.setLoopPoints(0.0, 16.0);
    t.setLooping(true);
    t.play();

    t.advance(512);
    REQUIRE_FALSE(t.didLoopWrap());
}

// ============================================================
// Block beat range
// ============================================================

TEST_CASE("Transport blockStartBeats and blockEndBeats track advance", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setTempo(120.0);
    t.play();

    t.advance(44100); // 0→2 beats
    REQUIRE_THAT(t.getBlockStartBeats(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(t.getBlockEndBeats(), WithinAbs(2.0, 1e-9));

    t.advance(44100); // 2→4 beats
    REQUIRE_THAT(t.getBlockStartBeats(), WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(t.getBlockEndBeats(), WithinAbs(4.0, 1e-9));
}

TEST_CASE("Transport on loop wrap blockEndBeats < blockStartBeats", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setTempo(120.0);

    t.setLoopPoints(0.0, 4.0);
    t.setLooping(true);
    t.setPositionInBeats(3.5);
    t.play();

    t.advance(44100); // start=3.5, +2 beats → wraps, end ~1.5
    REQUIRE(t.getBlockStartBeats() > t.getBlockEndBeats());
}

// ============================================================
// AudioPlayHead
// ============================================================

TEST_CASE("Transport getPosition populates all fields", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setTempo(140.0);
    t.setTimeSignature(3, 4);
    t.play();
    t.advance(44100); // 1 second

    auto pos = t.getPosition();
    REQUIRE(pos.hasValue());

    REQUIRE(*pos->getTimeInSamples() == 44100);
    REQUIRE_THAT(*pos->getTimeInSeconds(), WithinAbs(1.0, 1e-9));

    // beats = 1.0 * (140/60) = 2.333...
    double expectedBeats = 140.0 / 60.0;
    REQUIRE_THAT(*pos->getPpqPosition(), WithinAbs(expectedBeats, 1e-6));

    REQUIRE(*pos->getBpm() == 140.0);
    auto ts2 = pos->getTimeSignature();
    REQUIRE(ts2->numerator == 3);
    REQUIRE(ts2->denominator == 4);
    REQUIRE(pos->getIsPlaying());
    REQUIRE_FALSE(pos->getIsRecording());
}

TEST_CASE("Transport getPosition reports loop info when looping", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setLoopPoints(0.0, 16.0);
    t.setLooping(true);

    auto pos = t.getPosition();
    REQUIRE(pos.hasValue());
    REQUIRE(pos->getIsLooping());
    auto lp = pos->getLoopPoints();
    REQUIRE(lp->ppqStart == 0.0);
    REQUIRE(lp->ppqEnd == 16.0);
}

TEST_CASE("Transport getPosition does not report loop points when not looping", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);

    auto pos = t.getPosition();
    REQUIRE(pos.hasValue());
    REQUIRE_FALSE(pos->getIsLooping());
}

TEST_CASE("Transport getPosition before setSampleRate has timeInSamples set", "[Transport]")
{
    Transport t;
    t.setPositionInSamples(1000);

    auto pos = t.getPosition();
    REQUIRE(pos.hasValue());
    REQUIRE(*pos->getTimeInSamples() == 1000);
    // Musical fields depend on SR=0, so beats/seconds = 0
    REQUIRE_THAT(*pos->getTimeInSeconds(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(*pos->getPpqPosition(), WithinAbs(0.0, 1e-9));
}

// ============================================================
// Edge cases
// ============================================================

TEST_CASE("Transport derived positions return 0 before setSampleRate", "[Transport]")
{
    Transport t;
    REQUIRE_THAT(t.getPositionInSeconds(), WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(t.getPositionInBeats(), WithinAbs(0.0, 1e-9));
    REQUIRE(t.getBarCount() == 0);
    REQUIRE_THAT(t.getPpqOfLastBarStart(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Transport getSampleRate returns what was set", "[Transport]")
{
    Transport t;
    REQUIRE(t.getSampleRate() == 0.0);
    t.setSampleRate(48000.0);
    REQUIRE(t.getSampleRate() == 48000.0);
}

TEST_CASE("Transport looping disabled does not wrap", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setTempo(120.0);

    t.setLoopPoints(0.0, 4.0);
    // looping not enabled
    t.setPositionInBeats(3.5);
    t.play();
    t.advance(44100); // +2 beats → 5.5 beats, no wrap

    REQUIRE_THAT(t.getPositionInBeats(), WithinAbs(5.5, 0.01));
    REQUIRE_FALSE(t.didLoopWrap());
}

TEST_CASE("Transport multiple loop wraps in one large advance", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setTempo(120.0);

    // Loop 4 beats long (0-4)
    t.setLoopPoints(0.0, 4.0);
    t.setLooping(true);
    t.play();

    // Advance 10 seconds = 20 beats. 20 % 4 = 0 beats → position = 0.0
    t.advance(44100 * 10);
    double pos = t.getPositionInBeats();
    REQUIRE(pos >= 0.0 - 0.01);
    REQUIRE(pos < 4.0 + 0.01);
    REQUIRE(t.didLoopWrap());
}

TEST_CASE("Transport setLooping false disables looping", "[Transport]")
{
    Transport t;
    t.setLoopPoints(0.0, 16.0);
    t.setLooping(true);
    REQUIRE(t.isLooping());

    t.setLooping(false);
    REQUIRE_FALSE(t.isLooping());
}

TEST_CASE("Transport barCount at position 0 is 0", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    REQUIRE(t.getBarCount() == 0);
    REQUIRE_THAT(t.getPpqOfLastBarStart(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Transport position survives tempo change", "[Transport]")
{
    Transport t;
    t.setSampleRate(44100.0);
    t.setTempo(120.0);
    t.play();
    t.advance(44100); // 1 second = 44100 samples

    // Position is in samples (ground truth), beats change with tempo
    REQUIRE(t.getPositionInSamples() == 44100);
    REQUIRE_THAT(t.getPositionInBeats(), WithinAbs(2.0, 1e-9));

    t.setTempo(60.0);
    // Same sample position, different beat position
    REQUIRE(t.getPositionInSamples() == 44100);
    REQUIRE_THAT(t.getPositionInBeats(), WithinAbs(1.0, 1e-9));
}
