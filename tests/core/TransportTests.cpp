#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/Transport.h"

#include <cmath>

using namespace squeeze;
using Catch::Matchers::WithinRel;
using Catch::Matchers::WithinAbs;

// Helpers
static constexpr double kSampleRate = 44100.0;
static constexpr int kBlockSize = 512;
// beats = seconds * (tempo / 60)
// seconds = samples / sampleRate
// so beats = samples / sampleRate * tempo / 60
static double samplesToBeatsFn(int64_t samples, double sr, double bpm)
{
    if (sr <= 0.0) return 0.0;
    return (static_cast<double>(samples) / sr) * (bpm / 60.0);
}

static int64_t beatsToSamplesFn(double beats, double sr, double bpm)
{
    return static_cast<int64_t>(std::round(beats * 60.0 / bpm * sr));
}

// ═══════════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Transport default construction")
{
    Transport t;
    CHECK(t.getState() == TransportState::stopped);
    CHECK_FALSE(t.isPlaying());
    CHECK(t.getPositionInSamples() == 0);
    CHECK(t.getTempo() == 120.0);
    CHECK(t.getSampleRate() == 0.0);
    CHECK(t.getBlockSize() == 0);
    CHECK_FALSE(t.isLooping());
    CHECK(t.getLoopStartBeats() == 0.0);
    CHECK(t.getLoopEndBeats() == 0.0);
    CHECK_FALSE(t.didLoopWrap());
    CHECK(t.getBlockStartBeats() == 0.0);
    CHECK(t.getBlockEndBeats() == 0.0);
}

TEST_CASE("Transport default time signature is 4/4")
{
    Transport t;
    auto ts = t.getTimeSignature();
    CHECK(ts.numerator == 4);
    CHECK(ts.denominator == 4);
}

// ═══════════════════════════════════════════════════════════════════
// prepare()
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("prepare sets sample rate and block size")
{
    Transport t;
    t.prepare(48000.0, 256);
    CHECK(t.getSampleRate() == 48000.0);
    CHECK(t.getBlockSize() == 256);
}

// ═══════════════════════════════════════════════════════════════════
// State transitions
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("play transitions from stopped to playing")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.play();
    CHECK(t.getState() == TransportState::playing);
    CHECK(t.isPlaying());
}

TEST_CASE("stop transitions from playing to stopped and resets position")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.play();
    t.advance(kBlockSize);
    REQUIRE(t.getPositionInSamples() == kBlockSize);

    t.stop();
    CHECK(t.getState() == TransportState::stopped);
    CHECK(t.getPositionInSamples() == 0);
}

TEST_CASE("pause preserves position")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.play();
    t.advance(kBlockSize);
    int64_t posBeforePause = t.getPositionInSamples();

    t.pause();
    CHECK(t.getState() == TransportState::paused);
    CHECK(t.getPositionInSamples() == posBeforePause);
}

TEST_CASE("play from paused resumes")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.play();
    t.advance(kBlockSize);
    t.pause();
    int64_t posAtPause = t.getPositionInSamples();

    t.play();
    CHECK(t.isPlaying());
    t.advance(kBlockSize);
    CHECK(t.getPositionInSamples() == posAtPause + kBlockSize);
}

TEST_CASE("redundant play is a no-op")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.play();
    t.advance(kBlockSize);
    int64_t pos = t.getPositionInSamples();
    t.play(); // no-op
    CHECK(t.getPositionInSamples() == pos);
    CHECK(t.isPlaying());
}

TEST_CASE("redundant stop is a no-op")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.stop(); // already stopped
    CHECK(t.getState() == TransportState::stopped);
}

TEST_CASE("pause when stopped is a no-op")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.pause(); // stopped → no-op
    CHECK(t.getState() == TransportState::stopped);
}

TEST_CASE("pause when already paused is a no-op")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.play();
    t.advance(kBlockSize);
    t.pause();
    int64_t pos = t.getPositionInSamples();
    t.pause(); // no-op
    CHECK(t.getState() == TransportState::paused);
    CHECK(t.getPositionInSamples() == pos);
}

// ═══════════════════════════════════════════════════════════════════
// advance()
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("advance increases position by numSamples when playing")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.play();

    t.advance(kBlockSize);
    CHECK(t.getPositionInSamples() == kBlockSize);

    t.advance(kBlockSize);
    CHECK(t.getPositionInSamples() == 2 * kBlockSize);
}

TEST_CASE("advance does not change position when stopped")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.advance(kBlockSize);
    CHECK(t.getPositionInSamples() == 0);
}

TEST_CASE("advance does not change position when paused")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.play();
    t.advance(kBlockSize);
    t.pause();
    int64_t pos = t.getPositionInSamples();

    t.advance(kBlockSize);
    CHECK(t.getPositionInSamples() == pos);
}

TEST_CASE("advance with 0 samples is a no-op")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.play();
    t.advance(kBlockSize);
    int64_t pos = t.getPositionInSamples();

    t.advance(0);
    CHECK(t.getPositionInSamples() == pos);
}

TEST_CASE("advance with negative samples is a no-op")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.play();
    t.advance(kBlockSize);
    int64_t pos = t.getPositionInSamples();

    t.advance(-100);
    CHECK(t.getPositionInSamples() == pos);
}

TEST_CASE("advance resets per-block state even when not playing")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);

    // Play and advance to get some position
    t.play();
    t.advance(kBlockSize);
    // Now stop — position resets to 0
    t.stop();

    // Advance while stopped should reset block state
    t.advance(kBlockSize);
    CHECK_FALSE(t.didLoopWrap());
    CHECK(t.getBlockStartBeats() == 0.0);
    CHECK(t.getBlockEndBeats() == 0.0);
}

// ═══════════════════════════════════════════════════════════════════
// Position model
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("getPositionInSeconds derives from samples and sample rate")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setPositionInSamples(44100);
    CHECK_THAT(t.getPositionInSeconds(), WithinRel(1.0, 1e-9));
}

TEST_CASE("getPositionInBeats derives from samples, sample rate, and tempo")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);
    // At 120 BPM, 1 second = 2 beats. 44100 samples = 1 second.
    t.setPositionInSamples(44100);
    CHECK_THAT(t.getPositionInBeats(), WithinRel(2.0, 1e-9));
}

TEST_CASE("getBarCount returns 0-based complete bars")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);
    // 4/4 time: 4 quarter notes per bar
    // At beat 0: bar 0
    CHECK(t.getBarCount() == 0);

    // At beat 4.0 (start of bar 2, 0-based bar 1)
    t.setPositionInBeats(4.0);
    CHECK(t.getBarCount() == 1);

    // At beat 7.99 (still in bar 1, 0-based)
    t.setPositionInBeats(7.99);
    CHECK(t.getBarCount() == 1);

    // At beat 8.0 (start of bar 3, 0-based bar 2)
    t.setPositionInBeats(8.0);
    CHECK(t.getBarCount() == 2);
}

TEST_CASE("getPpqOfLastBarStart returns correct value")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);

    // At beat 0.0 → last bar start is 0.0
    CHECK_THAT(t.getPpqOfLastBarStart(), WithinAbs(0.0, 1e-9));

    // At beat 5.5 → in bar 1 (0-based), bar start at 4.0
    t.setPositionInBeats(5.5);
    CHECK_THAT(t.getPpqOfLastBarStart(), WithinAbs(4.0, 1e-9));
}

TEST_CASE("quarterNotesPerBar calculation for different time signatures")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);

    // 3/4 time: 3 quarter notes per bar
    t.setTimeSignature(3, 4);
    t.setPositionInBeats(3.0); // exactly 1 bar
    CHECK(t.getBarCount() == 1);

    // 6/8 time: 6 * (4/8) = 3 quarter notes per bar
    t.setTimeSignature(6, 8);
    t.setPositionInBeats(3.0); // exactly 1 bar
    CHECK(t.getBarCount() == 1);

    // 7/8 time: 7 * (4/8) = 3.5 quarter notes per bar
    t.setTimeSignature(7, 8);
    t.setPositionInBeats(3.5); // exactly 1 bar
    CHECK(t.getBarCount() == 1);
}

TEST_CASE("setPositionInSamples sets position directly")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setPositionInSamples(12345);
    CHECK(t.getPositionInSamples() == 12345);
}

TEST_CASE("setPositionInSamples clamps negative to 0")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setPositionInSamples(-100);
    CHECK(t.getPositionInSamples() == 0);
}

TEST_CASE("setPositionInBeats converts to samples")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);

    // beats = 4.0 → samples = round(4.0 * 60.0 / 120.0 * 44100) = 88200
    t.setPositionInBeats(4.0);
    CHECK(t.getPositionInSamples() == 88200);
}

TEST_CASE("before prepare, derived positions return 0")
{
    Transport t;
    t.setPositionInSamples(44100);
    CHECK(t.getPositionInSeconds() == 0.0);
    CHECK(t.getPositionInBeats() == 0.0);
}

// ═══════════════════════════════════════════════════════════════════
// Tempo
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("setTempo changes tempo")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(140.0);
    CHECK(t.getTempo() == 140.0);
}

TEST_CASE("setTempo clamps to [1, 999]")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);

    t.setTempo(0.5);
    CHECK(t.getTempo() == 1.0);

    t.setTempo(1000.0);
    CHECK(t.getTempo() == 999.0);

    t.setTempo(-10.0);
    CHECK(t.getTempo() == 1.0);
}

TEST_CASE("tempo change preserves sample position, shifts musical position")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);
    t.setPositionInSamples(44100); // 1 second = 2 beats at 120

    double beatsAt120 = t.getPositionInBeats();
    CHECK_THAT(beatsAt120, WithinRel(2.0, 1e-9));

    t.setTempo(60.0); // now 1 second = 1 beat
    CHECK(t.getPositionInSamples() == 44100); // unchanged
    CHECK_THAT(t.getPositionInBeats(), WithinRel(1.0, 1e-9));
}

// ═══════════════════════════════════════════════════════════════════
// Time signature
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("setTimeSignature with valid values")
{
    Transport t;
    t.setTimeSignature(3, 4);
    auto ts = t.getTimeSignature();
    CHECK(ts.numerator == 3);
    CHECK(ts.denominator == 4);
}

TEST_CASE("setTimeSignature clamps numerator to [1, 32]")
{
    Transport t;

    t.setTimeSignature(0, 4);
    // Invalid → no change from default 4/4
    CHECK(t.getTimeSignature().numerator == 4);

    t.setTimeSignature(33, 4);
    CHECK(t.getTimeSignature().numerator == 4);
}

TEST_CASE("setTimeSignature rejects non-power-of-2 denominator")
{
    Transport t;

    t.setTimeSignature(4, 3); // 3 is not power of 2
    // Should remain at default 4/4
    CHECK(t.getTimeSignature().denominator == 4);

    t.setTimeSignature(4, 5);
    CHECK(t.getTimeSignature().denominator == 4);
}

TEST_CASE("setTimeSignature accepts all valid denominators")
{
    Transport t;
    for (int d : {1, 2, 4, 8, 16, 32})
    {
        t.setTimeSignature(4, d);
        CHECK(t.getTimeSignature().denominator == d);
    }
}

// ═══════════════════════════════════════════════════════════════════
// Loop points
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("setLoopPoints stores beat-domain values")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setLoopPoints(4.0, 16.0);
    CHECK_THAT(t.getLoopStartBeats(), WithinAbs(4.0, 1e-9));
    CHECK_THAT(t.getLoopEndBeats(), WithinAbs(16.0, 1e-9));
}

TEST_CASE("setLoopPoints rejects end <= start")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);

    t.setLoopPoints(8.0, 4.0); // end < start
    CHECK(t.getLoopStartBeats() == 0.0);
    CHECK(t.getLoopEndBeats() == 0.0);

    t.setLoopPoints(4.0, 4.0); // end == start
    CHECK(t.getLoopStartBeats() == 0.0);
    CHECK(t.getLoopEndBeats() == 0.0);
}

TEST_CASE("setLooping enables looping with valid loop points")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setLoopPoints(0.0, 16.0);
    t.setLooping(true);
    CHECK(t.isLooping());
}

TEST_CASE("setLooping true with both loop points 0 stays disabled")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setLooping(true);
    CHECK_FALSE(t.isLooping());
}

TEST_CASE("setLooping false disables looping")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setLoopPoints(0.0, 16.0);
    t.setLooping(true);
    REQUIRE(t.isLooping());

    t.setLooping(false);
    CHECK_FALSE(t.isLooping());
}

// ═══════════════════════════════════════════════════════════════════
// Loop: minimum length enforcement
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("loop shorter than blockSize is auto-disabled")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize); // blockSize = 512

    // At 120 BPM, 44100 sr: 1 beat = 22050 samples. 512 samples ≈ 0.02322 beats.
    // A loop of 0.01 beats = ~221 samples < 512 → should disable
    t.setLoopPoints(0.0, 0.01);
    t.setLooping(true);
    CHECK_FALSE(t.isLooping());
}

TEST_CASE("tempo change shrinking active loop below minimum disables looping")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);

    // At 120 BPM: 1 beat = 22050 samples. Loop of 1 beat = 22050 >> 512. Fine.
    t.setLoopPoints(0.0, 1.0);
    t.setLooping(true);
    REQUIRE(t.isLooping());

    // At very high tempo: 1 beat becomes much shorter.
    // At 999 BPM: 1 beat = 44100 * 60 / 999 ≈ 2648 samples. Still > 512.
    // Need a shorter loop to trigger this. Use 0.05 beats.
    t.setLoopPoints(0.0, 0.05);
    t.setLooping(true);
    // 0.05 beats at 120 BPM = 0.05 * 22050 = 1102.5 → 1102 samples > 512. OK.
    REQUIRE(t.isLooping());

    // Switch to 999 BPM: 0.05 beats = 0.05 * 60/999 * 44100 ≈ 132 samples < 512
    t.setTempo(999.0);
    CHECK_FALSE(t.isLooping());
}

TEST_CASE("loop points preserved after auto-disable, re-enable possible")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);

    t.setLoopPoints(0.0, 0.05);
    t.setLooping(true);
    REQUIRE(t.isLooping());

    // Disable via tempo
    t.setTempo(999.0);
    REQUIRE_FALSE(t.isLooping());

    // Beat-domain points preserved
    CHECK_THAT(t.getLoopStartBeats(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(t.getLoopEndBeats(), WithinAbs(0.05, 1e-9));

    // Restore tempo → loop should be long enough now
    t.setTempo(120.0);
    t.setLooping(true);
    CHECK(t.isLooping());
}

TEST_CASE("before prepare blockSize is 0 so no minimum enforced")
{
    Transport t;
    // blockSize_ is 0 before prepare
    t.setTempo(120.0);
    t.setLoopPoints(0.0, 0.001);
    t.setLooping(true);
    CHECK(t.isLooping()); // no minimum enforced
}

// ═══════════════════════════════════════════════════════════════════
// Loop wrapping in advance()
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("advance wraps position at loop end")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);

    // Loop 0–16 beats (4 bars at 4/4)
    t.setLoopPoints(0.0, 16.0);
    t.setLooping(true);

    // Seek near loop end
    t.setPositionInBeats(15.9);
    t.play();

    // Advance 1 second = 2 beats at 120 BPM = 44100 samples
    t.advance(44100);

    // 15.9 + 2.0 = 17.9 → wraps: (17.9 - 0) % 16 = 1.9
    CHECK_THAT(t.getPositionInBeats(), WithinAbs(1.9, 0.01));
    CHECK(t.didLoopWrap());
}

TEST_CASE("advance without loop crossing does not set didLoopWrap")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);
    t.setLoopPoints(0.0, 16.0);
    t.setLooping(true);

    t.setPositionInBeats(1.0);
    t.play();
    t.advance(kBlockSize); // small advance, won't reach loop end
    CHECK_FALSE(t.didLoopWrap());
}

TEST_CASE("loop wrapping uses integer sample arithmetic")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);

    // Loop 0–4 beats. At 120 BPM, 44100 sr: 4 beats = 88200 samples.
    t.setLoopPoints(0.0, 4.0);
    t.setLooping(true);

    t.setPositionInSamples(88200 - 100); // 100 samples before loop end
    t.play();
    t.advance(kBlockSize); // 512 samples: 88100 + 512 = 88612 → wraps

    int64_t expected = (88612 - 0) % 88200; // = 412
    CHECK(t.getPositionInSamples() == expected);
    CHECK(t.didLoopWrap());
}

TEST_CASE("advance with looping disabled does not wrap")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);
    t.setLoopPoints(0.0, 4.0);
    // looping is off

    int64_t loopEndSamples = beatsToSamplesFn(4.0, kSampleRate, 120.0);
    t.setPositionInSamples(loopEndSamples - 100);
    t.play();
    t.advance(kBlockSize);

    CHECK(t.getPositionInSamples() == loopEndSamples - 100 + kBlockSize);
    CHECK_FALSE(t.didLoopWrap());
}

// ═══════════════════════════════════════════════════════════════════
// Position snapping
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("setLooping true snaps position into loop region")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);
    t.setLoopPoints(4.0, 8.0);

    // Position past loop end
    t.setPositionInBeats(10.0);
    t.setLooping(true);

    // Should snap into [4, 8)
    double pos = t.getPositionInBeats();
    CHECK(pos >= 4.0 - 0.01);
    CHECK(pos < 8.0 + 0.01);
}

TEST_CASE("setLooping true snaps position before loop start")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);
    t.setLoopPoints(4.0, 8.0);

    // Position before loop start
    t.setPositionInBeats(1.0);
    t.setLooping(true);

    // Should snap to loop start
    int64_t loopStartSamples = beatsToSamplesFn(4.0, kSampleRate, 120.0);
    CHECK(t.getPositionInSamples() == loopStartSamples);
}

TEST_CASE("position snapping does not set didLoopWrap")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);
    t.setLoopPoints(4.0, 8.0);

    t.setPositionInBeats(10.0);
    t.setLooping(true);
    CHECK_FALSE(t.didLoopWrap());
}

TEST_CASE("setLoopPoints snaps position into new loop region")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);
    t.setLoopPoints(0.0, 16.0);
    t.setLooping(true);

    // Position in [0, 16) — fine
    t.setPositionInBeats(12.0);

    // Shrink loop to [0, 8) — position 12 is outside
    t.setLoopPoints(0.0, 8.0);

    // Should snap into [0, 8)
    double pos = t.getPositionInBeats();
    CHECK(pos >= 0.0 - 0.01);
    CHECK(pos < 8.0 + 0.01);
}

// ═══════════════════════════════════════════════════════════════════
// Block range tracking
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("blockStartBeats and blockEndBeats track advance range")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);
    t.play();

    t.advance(kBlockSize);
    double startBeats = t.getBlockStartBeats();
    double endBeats = t.getBlockEndBeats();

    CHECK_THAT(startBeats, WithinAbs(0.0, 1e-9));
    CHECK(endBeats > startBeats);
    CHECK_THAT(endBeats,
               WithinRel(samplesToBeatsFn(kBlockSize, kSampleRate, 120.0), 1e-9));
}

TEST_CASE("block range on loop wrap: blockEndBeats < blockStartBeats")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);
    t.setLoopPoints(0.0, 16.0);
    t.setLooping(true);

    t.setPositionInBeats(15.9);
    t.play();
    t.advance(44100); // 2 beats, wraps

    CHECK_THAT(t.getBlockStartBeats(), WithinAbs(15.9, 0.01));
    CHECK(t.getBlockEndBeats() < t.getBlockStartBeats()); // wrapped
    CHECK(t.didLoopWrap());
}

TEST_CASE("block range resets when not playing")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);

    // Play and advance
    t.play();
    t.advance(kBlockSize);
    t.stop(); // position → 0

    // Advance while stopped
    t.advance(kBlockSize);
    CHECK_THAT(t.getBlockStartBeats(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(t.getBlockEndBeats(), WithinAbs(0.0, 1e-9));
    CHECK_FALSE(t.didLoopWrap());
}

TEST_CASE("didLoopWrap resets on every advance call")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);
    t.setLoopPoints(0.0, 4.0);
    t.setLooping(true);

    int64_t loopEndSamples = beatsToSamplesFn(4.0, kSampleRate, 120.0);
    t.setPositionInSamples(loopEndSamples - 100);
    t.play();

    t.advance(kBlockSize); // wraps
    REQUIRE(t.didLoopWrap());

    t.advance(kBlockSize); // no wrap this time
    CHECK_FALSE(t.didLoopWrap());
}

// ═══════════════════════════════════════════════════════════════════
// AudioPlayHead
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("getPosition returns valid PositionInfo")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(140.0);
    t.setTimeSignature(3, 4);

    auto pos = t.getPosition();
    REQUIRE(pos.hasValue());

    CHECK(*pos->getTimeInSamples() == 0);
    CHECK_THAT(*pos->getTimeInSeconds(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(*pos->getPpqPosition(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(*pos->getBpm(), WithinRel(140.0, 1e-9));
    CHECK(pos->getTimeSignature()->numerator == 3);
    CHECK(pos->getTimeSignature()->denominator == 4);
    CHECK_FALSE(pos->getIsPlaying());
    CHECK_FALSE(pos->getIsRecording());
}

TEST_CASE("getPosition reflects playing state")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.play();

    auto pos = t.getPosition();
    CHECK(pos->getIsPlaying());
}

TEST_CASE("getPosition reflects loop info when looping")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);
    t.setLoopPoints(4.0, 16.0);
    t.setLooping(true);

    auto pos = t.getPosition();
    REQUIRE(pos.hasValue());
    CHECK(pos->getIsLooping());
    auto lp = pos->getLoopPoints();
    REQUIRE(lp.hasValue());
    CHECK_THAT(lp->ppqStart, WithinAbs(4.0, 1e-9));
    CHECK_THAT(lp->ppqEnd, WithinAbs(16.0, 1e-9));
}

TEST_CASE("getPosition without looping does not report loop points")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);

    auto pos = t.getPosition();
    CHECK_FALSE(pos->getIsLooping());
}

TEST_CASE("getPosition ppqPositionOfLastBarStart and barCount")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);
    t.setPositionInBeats(5.5); // in bar 1 (0-based), bar start at 4.0

    auto pos = t.getPosition();
    CHECK(*pos->getBarCount() == 1);
    CHECK_THAT(*pos->getPpqPositionOfLastBarStart(), WithinAbs(4.0, 1e-9));
}

// ═══════════════════════════════════════════════════════════════════
// Tempo change recomputes loop sample boundaries
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("tempo change recomputes cached loop samples")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);
    t.setLoopPoints(0.0, 4.0);
    t.setLooping(true);

    // At 120 BPM, loop end = 88200 samples
    t.setPositionInSamples(88200 - 100);
    t.play();
    t.advance(kBlockSize); // wraps at 88200
    REQUIRE(t.didLoopWrap());

    // Change tempo to 60 BPM → loop end = 4.0 * 60/60 * 44100 = 176400
    t.stop();
    t.setTempo(60.0);
    t.setPositionInSamples(88200 - 100);
    t.play();
    t.advance(kBlockSize); // should NOT wrap now (88100 + 512 = 88612 < 176400)
    CHECK_FALSE(t.didLoopWrap());
}

TEST_CASE("prepare recomputes cached loop samples")
{
    Transport t;
    t.setTempo(120.0);
    t.setLoopPoints(0.0, 4.0);
    t.setLooping(true);

    // prepare with different sample rate
    t.prepare(48000.0, kBlockSize);
    // loop end at 48000: 4.0 * 60/120 * 48000 = 96000
    int64_t expected = beatsToSamplesFn(4.0, 48000.0, 120.0);

    t.setPositionInSamples(expected - 100);
    t.play();
    t.advance(kBlockSize);
    CHECK(t.didLoopWrap());
}

// ═══════════════════════════════════════════════════════════════════
// Edge cases
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("position at exactly loop end wraps to loop start")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);
    t.setLoopPoints(0.0, 4.0);
    t.setLooping(true);

    int64_t loopEnd = beatsToSamplesFn(4.0, kSampleRate, 120.0);
    // Place position so that after advance it lands exactly on loop end
    t.setPositionInSamples(loopEnd - kBlockSize);
    t.play();
    t.advance(kBlockSize); // position becomes exactly loopEnd → wraps
    CHECK(t.getPositionInSamples() == 0); // loopStart + (loopEnd - 0) % loopLen = 0
    CHECK(t.didLoopWrap());
}

TEST_CASE("non-zero loop start: loop region 4 to 8 beats")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);
    t.setLoopPoints(4.0, 8.0);
    t.setLooping(true);

    int64_t loopStart = beatsToSamplesFn(4.0, kSampleRate, 120.0);
    int64_t loopEnd = beatsToSamplesFn(8.0, kSampleRate, 120.0);

    // Position near end of loop
    t.setPositionInSamples(loopEnd - 100);
    t.play();
    t.advance(kBlockSize); // 88100 + 512 = wraps

    // Wrapped position should be in [loopStart, loopEnd)
    CHECK(t.getPositionInSamples() >= loopStart);
    CHECK(t.getPositionInSamples() < loopEnd);
    CHECK(t.didLoopWrap());
}

TEST_CASE("large advance wrapping multiple times still lands in loop region")
{
    Transport t;
    t.prepare(kSampleRate, kBlockSize);
    t.setTempo(120.0);
    t.setLoopPoints(0.0, 4.0); // 88200 samples
    t.setLooping(true);

    t.setPositionInSamples(0);
    t.play();

    // Advance by many loop lengths: 10 * 88200 = 882000 samples
    t.advance(882000);

    // Should wrap back into [0, 88200)
    CHECK(t.getPositionInSamples() >= 0);
    CHECK(t.getPositionInSamples() < 88200);
    CHECK(t.didLoopWrap());
}

TEST_CASE("advance with 1 sample increments near loop boundary")
{
    Transport t;
    t.prepare(kSampleRate, 1); // blockSize 1 for this test
    t.setTempo(120.0);
    t.setLoopPoints(0.0, 4.0);
    t.setLooping(true);

    int64_t loopEnd = beatsToSamplesFn(4.0, kSampleRate, 120.0);
    t.setPositionInSamples(loopEnd - 1);
    t.play();

    t.advance(1); // exactly reaches loop end
    CHECK(t.getPositionInSamples() == 0);
    CHECK(t.didLoopWrap());
}
