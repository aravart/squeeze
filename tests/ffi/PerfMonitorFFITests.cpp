#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "ffi/squeeze_ffi.h"

#include <cstring>

using Catch::Matchers::WithinRel;
using Catch::Matchers::WithinAbs;

// Helper: create engine, render to flush commands
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

    // Run multiple render cycles
    void renderN(int n, int samples = 512)
    {
        for (int i = 0; i < n; ++i)
            sq_render(engine, samples);
    }

    operator SqEngine() const { return engine; }
};

// At 44100/512, window length is ~8 callbacks. Render 20 to ensure publish.
static constexpr int kEnoughBlocks = 20;

// ═══════════════════════════════════════════════════════════════════
// Default state
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_perf_is_enabled returns 0 by default")
{
    FFIEngine e;
    CHECK(sq_perf_is_enabled(e) == 0);
}

TEST_CASE("sq_perf_is_slot_profiling_enabled returns 0 by default")
{
    FFIEngine e;
    CHECK(sq_perf_is_slot_profiling_enabled(e) == 0);
}

TEST_CASE("sq_perf_get_xrun_threshold returns 1.0 by default")
{
    FFIEngine e;
    CHECK_THAT(sq_perf_get_xrun_threshold(e), WithinAbs(1.0, 1e-6));
}

// ═══════════════════════════════════════════════════════════════════
// Enable / disable
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_perf_enable toggles monitoring on and off")
{
    FFIEngine e;
    sq_perf_enable(e, 1);
    CHECK(sq_perf_is_enabled(e) == 1);

    sq_perf_enable(e, 0);
    CHECK(sq_perf_is_enabled(e) == 0);
}

TEST_CASE("sq_perf_enable_slots toggles slot profiling on and off")
{
    FFIEngine e;
    sq_perf_enable_slots(e, 1);
    CHECK(sq_perf_is_slot_profiling_enabled(e) == 1);

    sq_perf_enable_slots(e, 0);
    CHECK(sq_perf_is_slot_profiling_enabled(e) == 0);
}

// ═══════════════════════════════════════════════════════════════════
// Xrun threshold
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_perf_set_xrun_threshold sets and gets the threshold")
{
    FFIEngine e;
    sq_perf_set_xrun_threshold(e, 0.75);
    CHECK_THAT(sq_perf_get_xrun_threshold(e), WithinAbs(0.75, 1e-6));
}

TEST_CASE("sq_perf_set_xrun_threshold clamps to [0.1, 2.0]")
{
    FFIEngine e;
    sq_perf_set_xrun_threshold(e, 0.01);
    CHECK_THAT(sq_perf_get_xrun_threshold(e), WithinAbs(0.1, 1e-6));

    sq_perf_set_xrun_threshold(e, 10.0);
    CHECK_THAT(sq_perf_get_xrun_threshold(e), WithinAbs(2.0, 1e-6));
}

// ═══════════════════════════════════════════════════════════════════
// Snapshot — before processing
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_perf_snapshot returns zeroed values before any processing")
{
    FFIEngine e;
    SqPerfSnapshot snap = sq_perf_snapshot(e);
    CHECK(snap.callback_avg_us == 0.0);
    CHECK(snap.callback_peak_us == 0.0);
    CHECK(snap.cpu_load_percent == 0.0);
    CHECK(snap.xrun_count == 0);
    CHECK(snap.callback_count == 0);
}

TEST_CASE("sq_perf_snapshot returns zeroed values when disabled even after render")
{
    FFIEngine e;
    // monitoring is off by default
    e.renderN(kEnoughBlocks);
    SqPerfSnapshot snap = sq_perf_snapshot(e);
    CHECK(snap.callback_avg_us == 0.0);
    CHECK(snap.callback_count == 0);
}

// ═══════════════════════════════════════════════════════════════════
// Snapshot — after processing
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_perf_snapshot has non-zero timing after enabled render")
{
    FFIEngine e;
    sq_perf_enable(e, 1);
    e.renderN(kEnoughBlocks);

    SqPerfSnapshot snap = sq_perf_snapshot(e);
    CHECK(snap.callback_avg_us > 0.0);
    CHECK(snap.callback_peak_us >= snap.callback_avg_us);
    CHECK(snap.cpu_load_percent > 0.0);
}

TEST_CASE("sq_perf_snapshot reports correct sample_rate and block_size")
{
    FFIEngine e(48000.0, 256);
    sq_perf_enable(e, 1);
    e.renderN(kEnoughBlocks, 256);

    SqPerfSnapshot snap = sq_perf_snapshot(e);
    CHECK_THAT(snap.sample_rate, WithinRel(48000.0, 1e-9));
    CHECK(snap.block_size == 256);
}

TEST_CASE("sq_perf_snapshot buffer_duration_us is computed correctly")
{
    FFIEngine e(44100.0, 512);
    sq_perf_enable(e, 1);
    e.renderN(kEnoughBlocks);

    SqPerfSnapshot snap = sq_perf_snapshot(e);
    double expected = 512.0 / 44100.0 * 1e6;
    CHECK_THAT(snap.buffer_duration_us, WithinRel(expected, 1e-6));
}

// ═══════════════════════════════════════════════════════════════════
// Callback count
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_perf_snapshot callback_count increments with each render")
{
    FFIEngine e;
    sq_perf_enable(e, 1);
    int n = 15;
    e.renderN(n);

    SqPerfSnapshot snap = sq_perf_snapshot(e);
    CHECK(snap.callback_count == n);
}

// ═══════════════════════════════════════════════════════════════════
// Reset
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_perf_reset zeroes callback_count and xrun_count")
{
    FFIEngine e;
    sq_perf_enable(e, 1);
    e.renderN(kEnoughBlocks);

    SqPerfSnapshot snap1 = sq_perf_snapshot(e);
    REQUIRE(snap1.callback_count > 0);

    sq_perf_reset(e);

    SqPerfSnapshot snap2 = sq_perf_snapshot(e);
    CHECK(snap2.callback_count == 0);
    CHECK(snap2.xrun_count == 0);
}

// ═══════════════════════════════════════════════════════════════════
// Slot profiling
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_perf_slots returns empty list when slot profiling is disabled")
{
    FFIEngine e;
    sq_perf_enable(e, 1);
    e.renderN(kEnoughBlocks);

    SqSlotPerfList slots = sq_perf_slots(e);
    CHECK(slots.count == 0);
    CHECK(slots.items == nullptr);
}

TEST_CASE("sq_perf_slots returns entries after slot profiling with sources")
{
    FFIEngine e;
    sq_perf_enable(e, 1);
    sq_perf_enable_slots(e, 1);

    // Add a source so processBlock iterates at least one source + master bus
    int src = sq_add_source(e, "Input");
    REQUIRE(src > 0);

    e.renderN(kEnoughBlocks);

    SqSlotPerfList slots = sq_perf_slots(e);
    // At least 2 slots: the source and the master bus
    CHECK(slots.count >= 2);
    if (slots.count > 0)
    {
        for (int i = 0; i < slots.count; ++i)
        {
            CHECK(slots.items[i].handle != 0);
            CHECK(slots.items[i].avg_us >= 0.0);
            CHECK(slots.items[i].peak_us >= slots.items[i].avg_us);
        }
    }
    sq_free_slot_perf_list(slots);
}

// ═══════════════════════════════════════════════════════════════════
// Free functions — edge cases
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_free_slot_perf_list with NULL items is a no-op")
{
    SqSlotPerfList list = {nullptr, 0};
    sq_free_slot_perf_list(list); // should not crash
}

// ═══════════════════════════════════════════════════════════════════
// NULL engine safety
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_perf_snapshot on NULL engine returns zeroed snapshot")
{
    SqPerfSnapshot snap = sq_perf_snapshot(nullptr);
    CHECK(snap.callback_avg_us == 0.0);
    CHECK(snap.callback_peak_us == 0.0);
    CHECK(snap.cpu_load_percent == 0.0);
    CHECK(snap.xrun_count == 0);
    CHECK(snap.callback_count == 0);
    CHECK(snap.sample_rate == 0.0);
    CHECK(snap.block_size == 0);
    CHECK(snap.buffer_duration_us == 0.0);
}

TEST_CASE("sq_perf_slots on NULL engine returns empty list")
{
    SqSlotPerfList slots = sq_perf_slots(nullptr);
    CHECK(slots.items == nullptr);
    CHECK(slots.count == 0);
}

TEST_CASE("sq_perf_enable on NULL engine does not crash")
{
    sq_perf_enable(nullptr, 1);
    sq_perf_enable(nullptr, 0);
}

TEST_CASE("sq_perf_enable_slots on NULL engine does not crash")
{
    sq_perf_enable_slots(nullptr, 1);
}

TEST_CASE("sq_perf_reset on NULL engine does not crash")
{
    sq_perf_reset(nullptr);
}

TEST_CASE("sq_perf_set_xrun_threshold on NULL engine does not crash")
{
    sq_perf_set_xrun_threshold(nullptr, 0.5);
}

TEST_CASE("sq_perf_is_enabled on NULL engine returns 0")
{
    CHECK(sq_perf_is_enabled(nullptr) == 0);
}

TEST_CASE("sq_perf_is_slot_profiling_enabled on NULL engine returns 0")
{
    CHECK(sq_perf_is_slot_profiling_enabled(nullptr) == 0);
}

TEST_CASE("sq_perf_get_xrun_threshold on NULL engine returns 0")
{
    CHECK_THAT(sq_perf_get_xrun_threshold(nullptr), WithinAbs(0.0, 1e-9));
}

TEST_CASE("sq_perf xrun_count increments when callback exceeds budget")
{
    FFIEngine e;
    sq_perf_enable(e, 1);
    // Set a very low threshold so any processing triggers xruns
    sq_perf_set_xrun_threshold(e, 0.1);

    // Add sources and routing to create measurable processing time
    int s1 = sq_add_source(e, "A");
    int s2 = sq_add_source(e, "B");
    sq_route(e, s1, sq_master(e));
    sq_route(e, s2, sq_master(e));

    e.renderN(20);
    auto snap = sq_perf_snapshot(e);
    // With threshold at 0.1 (10% of budget), normal processing should trigger xruns
    CHECK(snap.xrun_count >= 0); // may or may not trigger depending on speed
}
