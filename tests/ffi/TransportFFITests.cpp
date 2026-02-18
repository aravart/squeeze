#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "ffi/squeeze_ffi.h"

#include <cmath>

using Catch::Matchers::WithinRel;
using Catch::Matchers::WithinAbs;

// Helper: create engine, run render to flush commands
struct FFIEngine {
    SqEngine engine;

    FFIEngine(double sr = 44100.0, int bs = 512)
    {
        engine = sq_engine_create(sr, bs, nullptr);
    }

    ~FFIEngine()
    {
        sq_engine_destroy(engine);
    }

    // Flush pending commands through a render cycle
    void flush(int samples = 512)
    {
        sq_render(engine, samples);
    }

    operator SqEngine() const { return engine; }
};

// ═══════════════════════════════════════════════════════════════════
// Default state
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_transport_is_playing returns false initially")
{
    FFIEngine e;
    CHECK_FALSE(sq_transport_is_playing(e));
}

TEST_CASE("sq_transport_tempo returns 120 by default")
{
    FFIEngine e;
    CHECK_THAT(sq_transport_tempo(e), WithinRel(120.0, 1e-9));
}

TEST_CASE("sq_transport_position returns 0 initially")
{
    FFIEngine e;
    CHECK_THAT(sq_transport_position(e), WithinAbs(0.0, 1e-9));
}

TEST_CASE("sq_transport_is_looping returns false initially")
{
    FFIEngine e;
    CHECK_FALSE(sq_transport_is_looping(e));
}

// ═══════════════════════════════════════════════════════════════════
// Play / Stop / Pause
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_transport_play then is_playing returns true after flush")
{
    FFIEngine e;
    sq_transport_play(e);
    e.flush();
    CHECK(sq_transport_is_playing(e));
}

TEST_CASE("sq_transport_stop resets playing and position")
{
    FFIEngine e;
    sq_transport_play(e);
    e.flush(512);
    REQUIRE(sq_transport_is_playing(e));

    // Position should have advanced
    double posAfterPlay = sq_transport_position(e);

    sq_transport_stop(e);
    e.flush();
    CHECK_FALSE(sq_transport_is_playing(e));
    CHECK_THAT(sq_transport_position(e), WithinAbs(0.0, 1e-9));
}

TEST_CASE("sq_transport_pause preserves position")
{
    FFIEngine e;
    sq_transport_play(e);
    e.flush(512);
    double posBeforePause = sq_transport_position(e);

    sq_transport_pause(e);
    e.flush();
    CHECK_FALSE(sq_transport_is_playing(e));

    // Advance another block — position should not change
    e.flush(512);
    CHECK_THAT(sq_transport_position(e), WithinAbs(posBeforePause, 0.01));
}

TEST_CASE("sq_transport_play after pause resumes advancing")
{
    FFIEngine e;
    sq_transport_play(e);
    e.flush(512);
    double posAfterFirstBlock = sq_transport_position(e);

    sq_transport_pause(e);
    e.flush();

    sq_transport_play(e);
    e.flush(512);
    double posAfterResume = sq_transport_position(e);
    CHECK(posAfterResume > posAfterFirstBlock);
}

// ═══════════════════════════════════════════════════════════════════
// Tempo
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_transport_set_tempo updates tempo")
{
    FFIEngine e;
    sq_transport_set_tempo(e, 140.0);
    CHECK_THAT(sq_transport_tempo(e), WithinRel(140.0, 1e-9));
}

TEST_CASE("sq_transport_set_tempo clamps to valid range")
{
    FFIEngine e;

    sq_transport_set_tempo(e, 0.5);
    CHECK_THAT(sq_transport_tempo(e), WithinRel(1.0, 1e-9));

    sq_transport_set_tempo(e, 2000.0);
    CHECK_THAT(sq_transport_tempo(e), WithinRel(999.0, 1e-9));
}

// ═══════════════════════════════════════════════════════════════════
// Seek
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_transport_seek_beats changes position")
{
    FFIEngine e;
    sq_transport_seek_beats(e, 4.0);
    e.flush();
    CHECK_THAT(sq_transport_position(e), WithinAbs(4.0, 0.01));
}

TEST_CASE("sq_transport_seek_samples changes position")
{
    FFIEngine e;
    // 44100 samples at 120 BPM, 44100 sr = 2 beats
    sq_transport_seek_samples(e, 44100);
    e.flush();
    CHECK_THAT(sq_transport_position(e), WithinAbs(2.0, 0.01));
}

// ═══════════════════════════════════════════════════════════════════
// Time signature
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_transport_set_time_signature does not crash")
{
    FFIEngine e;
    sq_transport_set_time_signature(e, 3, 4);
    e.flush();
    // No query for time signature through FFI — just verify no crash
}

// ═══════════════════════════════════════════════════════════════════
// Looping
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_transport_set_looping enables looping with valid loop points")
{
    FFIEngine e;
    sq_transport_set_loop_points(e, 0.0, 16.0);
    sq_transport_set_looping(e, true);
    CHECK(sq_transport_is_looping(e));
}

TEST_CASE("sq_transport_set_looping false disables looping")
{
    FFIEngine e;
    sq_transport_set_loop_points(e, 0.0, 16.0);
    sq_transport_set_looping(e, true);
    REQUIRE(sq_transport_is_looping(e));

    sq_transport_set_looping(e, false);
    CHECK_FALSE(sq_transport_is_looping(e));
}

TEST_CASE("sq_transport_set_looping true with no loop points stays disabled")
{
    FFIEngine e;
    sq_transport_set_looping(e, true);
    CHECK_FALSE(sq_transport_is_looping(e));
}

TEST_CASE("sq_transport_set_loop_points rejects end <= start")
{
    FFIEngine e;
    sq_transport_set_loop_points(e, 8.0, 4.0);
    sq_transport_set_looping(e, true);
    CHECK_FALSE(sq_transport_is_looping(e)); // no valid points set
}

// ═══════════════════════════════════════════════════════════════════
// Position advances with playback
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("position advances during playback")
{
    FFIEngine e;
    sq_transport_play(e);

    // Render multiple blocks
    e.flush(512);
    e.flush(512);
    double pos = sq_transport_position(e);
    CHECK(pos > 0.0);
}

TEST_CASE("position does not advance when stopped")
{
    FFIEngine e;
    e.flush(512);
    e.flush(512);
    CHECK_THAT(sq_transport_position(e), WithinAbs(0.0, 1e-9));
}

// ═══════════════════════════════════════════════════════════════════
// Loop wrap through render
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("looping wraps position during render")
{
    FFIEngine e;
    sq_transport_set_tempo(e, 120.0);
    sq_transport_set_loop_points(e, 0.0, 4.0);
    sq_transport_set_looping(e, true);
    sq_transport_seek_beats(e, 3.9);
    sq_transport_play(e);

    // At 120 BPM, 44100 sr, 512 samples per block:
    // 512 samples ≈ 0.0232 beats
    // Render enough blocks to cross beat 4.0
    for (int i = 0; i < 20; ++i)
        e.flush(512);

    // Position should have wrapped back into [0, 4)
    double pos = sq_transport_position(e);
    CHECK(pos >= 0.0);
    CHECK(pos < 4.0);
}

// ═══════════════════════════════════════════════════════════════════
// Null engine safety
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("transport functions with null engine do not crash")
{
    // These should be no-ops, not crashes
    sq_transport_play(nullptr);
    sq_transport_stop(nullptr);
    sq_transport_pause(nullptr);
    sq_transport_set_tempo(nullptr, 120.0);
    sq_transport_set_time_signature(nullptr, 4, 4);
    sq_transport_seek_samples(nullptr, 0);
    sq_transport_seek_beats(nullptr, 0.0);
    sq_transport_set_loop_points(nullptr, 0.0, 4.0);
    sq_transport_set_looping(nullptr, false);
}
