#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "ffi/squeeze_ffi.h"

using Catch::Matchers::WithinAbs;

// Helper: RAII engine wrapper
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

    void flush(int samples = 512)
    {
        sq_render(engine, samples);
    }

    operator SqEngine() const { return engine; }
};


// ═══════════════════════════════════════════════════════════════════
// Schedule functions return true
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_schedule_note_on returns true")
{
    FFIEngine e;
    CHECK(sq_schedule_note_on(e, 1, 0.0, 1, 60, 0.8f));
}

TEST_CASE("sq_schedule_note_off returns true")
{
    FFIEngine e;
    CHECK(sq_schedule_note_off(e, 1, 1.0, 1, 60));
}

TEST_CASE("sq_schedule_cc returns true")
{
    FFIEngine e;
    CHECK(sq_schedule_cc(e, 1, 0.0, 1, 1, 64));
}

TEST_CASE("sq_schedule_pitch_bend returns true")
{
    FFIEngine e;
    CHECK(sq_schedule_pitch_bend(e, 1, 0.0, 1, 8192));
}

TEST_CASE("sq_schedule_param_change returns true")
{
    FFIEngine e;
    CHECK(sq_schedule_param_change(e, 1, 0.0, "gain", 0.5f));
}


// ═══════════════════════════════════════════════════════════════════
// Events survive render without crash
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Scheduled events render without crash")
{
    FFIEngine e;
    int src = sq_add_source(e, "Synth");

    sq_schedule_note_on(e, src, 0.0, 1, 60, 0.8f);
    sq_schedule_note_off(e, src, 0.5, 1, 60);
    sq_schedule_cc(e, src, 0.0, 1, 7, 100);
    sq_schedule_pitch_bend(e, src, 0.0, 1, 12000);

    // Start transport so events are dispatched
    sq_transport_play(e);
    e.flush();  // apply play command + process block with events
}


// ═══════════════════════════════════════════════════════════════════
// param change dispatch — verify via getParameter
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_schedule_param_change dispatches during render")
{
    FFIEngine e;
    int src = sq_add_source(e, "Synth");
    int gen = sq_source_generator(e, src);

    // Generator is a GainProcessor with default gain = 1.0
    float before = sq_get_param(e, gen, "gain");
    CHECK_THAT(before, WithinAbs(1.0f, 1e-6));

    // Schedule a param change at beat 0.0
    CHECK(sq_schedule_param_change(e, gen, 0.0, "gain", 0.25f));

    // Start transport and render
    sq_transport_play(e);
    e.flush();

    // Param should now be 0.25
    float after = sq_get_param(e, gen, "gain");
    CHECK_THAT(after, WithinAbs(0.25f, 1e-6));
}


// ═══════════════════════════════════════════════════════════════════
// Events cleared on stop
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Scheduled events are cleared on transport stop")
{
    FFIEngine e;
    int src = sq_add_source(e, "Synth");
    int gen = sq_source_generator(e, src);

    // Schedule a param change at beat 100.0 (far in the future)
    sq_schedule_param_change(e, gen, 100.0, "gain", 0.1f);

    // Start, render (event stays in staging — not yet at beat 100)
    sq_transport_play(e);
    e.flush();

    // Stop clears the scheduler
    sq_transport_stop(e);
    e.flush();

    // Play again and advance past beat 100 — event should be gone
    sq_transport_play(e);
    // Render many blocks to advance well past beat 100
    for (int i = 0; i < 1000; ++i)
        e.flush();

    // Gain should still be default (1.0), not 0.1
    float val = sq_get_param(e, gen, "gain");
    CHECK_THAT(val, WithinAbs(1.0f, 1e-6));
}


// ═══════════════════════════════════════════════════════════════════
// Null engine safety
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Event scheduling functions handle null engine without crash")
{
    // These should not crash — return value is unspecified for null engine
    sq_schedule_note_on(nullptr, 1, 0.0, 1, 60, 0.8f);
    sq_schedule_note_off(nullptr, 1, 1.0, 1, 60);
    sq_schedule_cc(nullptr, 1, 0.0, 1, 1, 64);
    sq_schedule_pitch_bend(nullptr, 1, 0.0, 1, 8192);
    sq_schedule_param_change(nullptr, 1, 0.0, "gain", 0.5f);
}
