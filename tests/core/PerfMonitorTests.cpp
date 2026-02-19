#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/PerfMonitor.h"

#include <chrono>
#include <thread>

using namespace squeeze;
using Catch::Matchers::WithinRel;
using Catch::Matchers::WithinAbs;

static constexpr double kSampleRate = 44100.0;
static constexpr int kBlockSize = 512;
// bufferDurationUs = blockSize / sampleRate * 1e6
static constexpr double kBudgetUs = kBlockSize / kSampleRate * 1e6; // ~11609us

// Window length: sampleRate / blockSize / 10 = ~8 callbacks
static constexpr int kWindowLength = static_cast<int>(kSampleRate / kBlockSize / 10);

// Busy-wait for a given number of microseconds (more precise than sleep)
static void busyWaitUs(int us)
{
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - start)
               .count() < us)
    {
    }
}

// Run N beginBlock/endBlock cycles with no work in between
static void runBlocks(PerfMonitor& pm, int n)
{
    for (int i = 0; i < n; ++i)
    {
        pm.beginBlock();
        pm.endBlock();
    }
}

// Run N blocks with slot profiling (simulating slotCount slots)
static void runBlocksWithSlots(PerfMonitor& pm, int n, int slotCount,
                               int baseHandle = 100)
{
    for (int i = 0; i < n; ++i)
    {
        pm.beginBlock();
        for (int s = 0; s < slotCount; ++s)
        {
            pm.beginSlot(s, baseHandle + s);
            pm.endSlot(s);
        }
        pm.endBlock();
    }
}

// ═══════════════════════════════════════════════════════════════════
// Construction & defaults
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PerfMonitor is disabled by default")
{
    PerfMonitor pm;
    CHECK_FALSE(pm.isEnabled());
}

TEST_CASE("PerfMonitor slot profiling is disabled by default")
{
    PerfMonitor pm;
    CHECK_FALSE(pm.isSlotProfilingEnabled());
}

TEST_CASE("PerfMonitor default xrun threshold is 1.0")
{
    PerfMonitor pm;
    CHECK_THAT(pm.getXrunThreshold(), WithinAbs(1.0, 1e-6));
}

TEST_CASE("PerfMonitor getSnapshot before prepare returns zeroed snapshot")
{
    PerfMonitor pm;
    auto snap = pm.getSnapshot();
    CHECK(snap.callbackAvgUs == 0.0);
    CHECK(snap.callbackPeakUs == 0.0);
    CHECK(snap.cpuLoadPercent == 0.0);
    CHECK(snap.xrunCount == 0);
    CHECK(snap.callbackCount == 0);
    CHECK(snap.sampleRate == 0.0);
    CHECK(snap.blockSize == 0);
    CHECK(snap.bufferDurationUs == 0.0);
    CHECK(snap.slots.empty());
}

// ═══════════════════════════════════════════════════════════════════
// enable / disable
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("enable makes isEnabled return true")
{
    PerfMonitor pm;
    pm.enable();
    CHECK(pm.isEnabled());
}

TEST_CASE("disable makes isEnabled return false")
{
    PerfMonitor pm;
    pm.enable();
    pm.disable();
    CHECK_FALSE(pm.isEnabled());
}

TEST_CASE("enableSlotProfiling makes isSlotProfilingEnabled return true")
{
    PerfMonitor pm;
    pm.enableSlotProfiling();
    CHECK(pm.isSlotProfilingEnabled());
}

TEST_CASE("disableSlotProfiling makes isSlotProfilingEnabled return false")
{
    PerfMonitor pm;
    pm.enableSlotProfiling();
    pm.disableSlotProfiling();
    CHECK_FALSE(pm.isSlotProfilingEnabled());
}

// ═══════════════════════════════════════════════════════════════════
// prepare
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("prepare sets sampleRate and blockSize in snapshot")
{
    PerfMonitor pm;
    pm.prepare(48000.0, 256);
    pm.enable();
    runBlocks(pm, 200); // enough to publish
    auto snap = pm.getSnapshot();
    CHECK_THAT(snap.sampleRate, WithinRel(48000.0, 1e-9));
    CHECK(snap.blockSize == 256);
}

TEST_CASE("prepare computes bufferDurationUs correctly")
{
    PerfMonitor pm;
    pm.prepare(44100.0, 512);
    pm.enable();
    runBlocks(pm, 200);
    auto snap = pm.getSnapshot();
    double expected = 512.0 / 44100.0 * 1e6;
    CHECK_THAT(snap.bufferDurationUs, WithinRel(expected, 1e-6));
}

// ═══════════════════════════════════════════════════════════════════
// Disabled state — no data accumulated
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("beginBlock/endBlock while disabled does not publish data")
{
    PerfMonitor pm;
    pm.prepare(kSampleRate, kBlockSize);
    // pm is disabled (default)
    runBlocks(pm, kWindowLength + 5);
    auto snap = pm.getSnapshot();
    CHECK(snap.callbackAvgUs == 0.0);
    CHECK(snap.callbackCount == 0);
}

// ═══════════════════════════════════════════════════════════════════
// Snapshot after processing
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("snapshot has non-zero callbackAvgUs after enabled processing")
{
    PerfMonitor pm;
    pm.prepare(kSampleRate, kBlockSize);
    pm.enable();
    // Run enough blocks to trigger at least one publish
    for (int i = 0; i < kWindowLength + 5; ++i)
    {
        pm.beginBlock();
        busyWaitUs(10); // ensure measurable duration
        pm.endBlock();
    }
    auto snap = pm.getSnapshot();
    CHECK(snap.callbackAvgUs > 0.0);
}

TEST_CASE("callbackPeakUs >= callbackAvgUs")
{
    PerfMonitor pm;
    pm.prepare(kSampleRate, kBlockSize);
    pm.enable();
    for (int i = 0; i < kWindowLength + 5; ++i)
    {
        pm.beginBlock();
        busyWaitUs(10);
        pm.endBlock();
    }
    auto snap = pm.getSnapshot();
    CHECK(snap.callbackPeakUs >= snap.callbackAvgUs);
}

TEST_CASE("cpuLoadPercent is non-negative after processing")
{
    PerfMonitor pm;
    pm.prepare(kSampleRate, kBlockSize);
    pm.enable();
    for (int i = 0; i < kWindowLength + 5; ++i)
    {
        pm.beginBlock();
        busyWaitUs(10);
        pm.endBlock();
    }
    auto snap = pm.getSnapshot();
    CHECK(snap.cpuLoadPercent > 0.0);
}

TEST_CASE("cpuLoadPercent equals callbackAvgUs / bufferDurationUs * 100")
{
    PerfMonitor pm;
    pm.prepare(kSampleRate, kBlockSize);
    pm.enable();
    for (int i = 0; i < kWindowLength + 5; ++i)
    {
        pm.beginBlock();
        busyWaitUs(50);
        pm.endBlock();
    }
    auto snap = pm.getSnapshot();
    REQUIRE(snap.bufferDurationUs > 0.0);
    double expectedCpu = snap.callbackAvgUs / snap.bufferDurationUs * 100.0;
    CHECK_THAT(snap.cpuLoadPercent, WithinRel(expectedCpu, 1e-6));
}

TEST_CASE("getSnapshot returns zeroed snapshot when disabled even after prior data")
{
    PerfMonitor pm;
    pm.prepare(kSampleRate, kBlockSize);
    pm.enable();
    for (int i = 0; i < kWindowLength + 5; ++i)
    {
        pm.beginBlock();
        busyWaitUs(10);
        pm.endBlock();
    }
    // Verify data was published
    auto snap1 = pm.getSnapshot();
    REQUIRE(snap1.callbackAvgUs > 0.0);

    // Disable and check zeroed
    pm.disable();
    auto snap2 = pm.getSnapshot();
    CHECK(snap2.callbackAvgUs == 0.0);
    CHECK(snap2.callbackCount == 0);
}

// ═══════════════════════════════════════════════════════════════════
// Callback count
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("callbackCount increments with each endBlock")
{
    PerfMonitor pm;
    pm.prepare(kSampleRate, kBlockSize);
    pm.enable();
    int n = 25;
    runBlocks(pm, n);
    auto snap = pm.getSnapshot();
    CHECK(snap.callbackCount == n);
}

TEST_CASE("callbackCount is cumulative across publish windows")
{
    PerfMonitor pm;
    pm.prepare(kSampleRate, kBlockSize);
    pm.enable();
    int total = kWindowLength * 3 + 2; // span multiple windows
    runBlocks(pm, total);
    auto snap = pm.getSnapshot();
    CHECK(snap.callbackCount == total);
}

// ═══════════════════════════════════════════════════════════════════
// Xrun detection
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("no xruns with fast callbacks and large budget")
{
    PerfMonitor pm;
    pm.prepare(kSampleRate, kBlockSize); // budget ~11609us
    pm.enable();
    runBlocks(pm, kWindowLength + 5); // very fast callbacks
    auto snap = pm.getSnapshot();
    CHECK(snap.xrunCount == 0);
}

TEST_CASE("xrun detected when callback exceeds budget")
{
    // Use a tiny budget: sampleRate=1000000, blockSize=1 → budget = 1us
    PerfMonitor pm;
    pm.prepare(1000000.0, 1);
    pm.enable();

    for (int i = 0; i < 5; ++i)
    {
        pm.beginBlock();
        busyWaitUs(100); // ~100us >> 1us budget
        pm.endBlock();
    }
    auto snap = pm.getSnapshot();
    CHECK(snap.xrunCount > 0);
}

TEST_CASE("xrunCount is cumulative")
{
    PerfMonitor pm;
    pm.prepare(1000000.0, 1); // 1us budget
    pm.enable();

    for (int i = 0; i < 10; ++i)
    {
        pm.beginBlock();
        busyWaitUs(100);
        pm.endBlock();
    }
    auto snap = pm.getSnapshot();
    CHECK(snap.xrunCount == 10);
}

TEST_CASE("resetCounters zeroes xrunCount and callbackCount")
{
    PerfMonitor pm;
    pm.prepare(1000000.0, 1); // 1us budget
    pm.enable();

    for (int i = 0; i < 5; ++i)
    {
        pm.beginBlock();
        busyWaitUs(100);
        pm.endBlock();
    }
    auto snap1 = pm.getSnapshot();
    REQUIRE(snap1.xrunCount > 0);
    REQUIRE(snap1.callbackCount > 0);

    pm.resetCounters();
    auto snap2 = pm.getSnapshot();
    CHECK(snap2.xrunCount == 0);
    CHECK(snap2.callbackCount == 0);
}

// ═══════════════════════════════════════════════════════════════════
// Xrun threshold
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("setXrunThreshold changes the threshold")
{
    PerfMonitor pm;
    pm.setXrunThreshold(0.5);
    CHECK_THAT(pm.getXrunThreshold(), WithinAbs(0.5, 1e-6));
}

TEST_CASE("setXrunThreshold clamps below 0.1")
{
    PerfMonitor pm;
    pm.setXrunThreshold(0.01);
    CHECK_THAT(pm.getXrunThreshold(), WithinAbs(0.1, 1e-6));
}

TEST_CASE("setXrunThreshold clamps above 2.0")
{
    PerfMonitor pm;
    pm.setXrunThreshold(5.0);
    CHECK_THAT(pm.getXrunThreshold(), WithinAbs(2.0, 1e-6));
}

TEST_CASE("lower xrun threshold triggers xruns sooner")
{
    // Budget ~11609us. With threshold 0.1, xrun triggers at ~1161us.
    // A busy-wait of 2000us should trigger with 0.1 threshold but not 1.0.
    PerfMonitor pm;
    pm.prepare(kSampleRate, kBlockSize);
    pm.enable();

    // First run with default threshold (1.0) — 2000us < 11609us, no xrun
    for (int i = 0; i < 5; ++i)
    {
        pm.beginBlock();
        busyWaitUs(2000);
        pm.endBlock();
    }
    auto snap1 = pm.getSnapshot();
    CHECK(snap1.xrunCount == 0);

    pm.resetCounters();
    pm.setXrunThreshold(0.1f); // now xrun triggers at ~1161us

    for (int i = 0; i < 5; ++i)
    {
        pm.beginBlock();
        busyWaitUs(2000); // 2000us > 1161us → xrun
        pm.endBlock();
    }
    auto snap2 = pm.getSnapshot();
    CHECK(snap2.xrunCount > 0);
}

// ═══════════════════════════════════════════════════════════════════
// Slot profiling
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("slots empty when slot profiling is disabled")
{
    PerfMonitor pm;
    pm.prepare(kSampleRate, kBlockSize);
    pm.enable();
    // slot profiling off (default)
    runBlocksWithSlots(pm, kWindowLength + 5, 3);
    auto snap = pm.getSnapshot();
    CHECK(snap.slots.empty());
}

TEST_CASE("slots populated when slot profiling is enabled")
{
    PerfMonitor pm;
    pm.prepare(kSampleRate, kBlockSize);
    pm.enable();
    pm.enableSlotProfiling();

    for (int i = 0; i < kWindowLength + 5; ++i)
    {
        pm.beginBlock();
        for (int s = 0; s < 3; ++s)
        {
            pm.beginSlot(s, 100 + s);
            busyWaitUs(5); // ensure measurable timing
            pm.endSlot(s);
        }
        pm.endBlock();
    }

    auto snap = pm.getSnapshot();
    REQUIRE(snap.slots.size() == 3);
    for (int s = 0; s < 3; ++s)
    {
        CHECK(snap.slots[s].handle == 100 + s);
        CHECK(snap.slots[s].avgUs > 0.0);
        CHECK(snap.slots[s].peakUs >= snap.slots[s].avgUs);
    }
}

TEST_CASE("slot handle matches what was passed to beginSlot")
{
    PerfMonitor pm;
    pm.prepare(kSampleRate, kBlockSize);
    pm.enable();
    pm.enableSlotProfiling();

    for (int i = 0; i < kWindowLength + 5; ++i)
    {
        pm.beginBlock();
        pm.beginSlot(0, 42);
        pm.endSlot(0);
        pm.beginSlot(1, 99);
        pm.endSlot(1);
        pm.endBlock();
    }

    auto snap = pm.getSnapshot();
    REQUIRE(snap.slots.size() == 2);
    CHECK(snap.slots[0].handle == 42);
    CHECK(snap.slots[1].handle == 99);
}

TEST_CASE("disabling slot profiling clears slots from snapshot")
{
    PerfMonitor pm;
    pm.prepare(kSampleRate, kBlockSize);
    pm.enable();
    pm.enableSlotProfiling();

    for (int i = 0; i < kWindowLength + 5; ++i)
    {
        pm.beginBlock();
        pm.beginSlot(0, 100);
        pm.endSlot(0);
        pm.endBlock();
    }
    auto snap1 = pm.getSnapshot();
    REQUIRE_FALSE(snap1.slots.empty());

    pm.disableSlotProfiling();
    // Run another full window without slot profiling
    runBlocks(pm, kWindowLength + 5);
    auto snap2 = pm.getSnapshot();
    CHECK(snap2.slots.empty());
}

// ═══════════════════════════════════════════════════════════════════
// Slot profiling — edge cases
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("beginSlot with slotIndex >= kMaxSlots is ignored")
{
    PerfMonitor pm;
    pm.prepare(kSampleRate, kBlockSize);
    pm.enable();
    pm.enableSlotProfiling();

    for (int i = 0; i < kWindowLength + 5; ++i)
    {
        pm.beginBlock();
        pm.beginSlot(0, 100);
        pm.endSlot(0);
        pm.beginSlot(256, 999); // kMaxSlots = 256, index 256 is out of bounds
        pm.endSlot(256);
        pm.endBlock();
    }

    auto snap = pm.getSnapshot();
    // Only slot 0 should be present, slot 256 was ignored
    CHECK(snap.slots.size() == 1);
    CHECK(snap.slots[0].handle == 100);
}

TEST_CASE("zero slots when slot profiling enabled but no beginSlot calls")
{
    PerfMonitor pm;
    pm.prepare(kSampleRate, kBlockSize);
    pm.enable();
    pm.enableSlotProfiling();

    runBlocks(pm, kWindowLength + 5); // no beginSlot/endSlot calls
    auto snap = pm.getSnapshot();
    CHECK(snap.slots.empty());
}

// ═══════════════════════════════════════════════════════════════════
// Publish window
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("no data published before window elapses")
{
    PerfMonitor pm;
    pm.prepare(kSampleRate, kBlockSize);
    pm.enable();

    // Run fewer blocks than the window length
    int fewBlocks = kWindowLength > 2 ? kWindowLength - 2 : 0;
    if (fewBlocks > 0)
    {
        for (int i = 0; i < fewBlocks; ++i)
        {
            pm.beginBlock();
            busyWaitUs(10);
            pm.endBlock();
        }
        auto snap = pm.getSnapshot();
        // callbackCount is cumulative (always current), but timing data
        // should not yet be published (still zeroed from the seqlock buffer)
        CHECK(snap.callbackAvgUs == 0.0);
        // callbackCount is still visible via the atomic
        CHECK(snap.callbackCount == fewBlocks);
    }
}

TEST_CASE("data published after window elapses")
{
    PerfMonitor pm;
    pm.prepare(kSampleRate, kBlockSize);
    pm.enable();

    for (int i = 0; i < kWindowLength + 5; ++i)
    {
        pm.beginBlock();
        busyWaitUs(10);
        pm.endBlock();
    }
    auto snap = pm.getSnapshot();
    CHECK(snap.callbackAvgUs > 0.0);
}

// ═══════════════════════════════════════════════════════════════════
// Publish window — minimum 1
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("publish window is at least 1 callback even with extreme params")
{
    // sampleRate=1, blockSize=10000 → window = 1/10000/10 ≈ 0 → clamped to 1
    PerfMonitor pm;
    pm.prepare(1.0, 10000);
    pm.enable();

    // A single block should trigger a publish
    pm.beginBlock();
    busyWaitUs(10);
    pm.endBlock();

    auto snap = pm.getSnapshot();
    CHECK(snap.callbackAvgUs > 0.0);
    CHECK(snap.callbackCount == 1);
}

// ═══════════════════════════════════════════════════════════════════
// Safe to call methods before prepare
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("beginBlock/endBlock before prepare is safe")
{
    PerfMonitor pm;
    pm.enable();
    // Should not crash
    pm.beginBlock();
    pm.endBlock();
    auto snap = pm.getSnapshot();
    // No meaningful data, but no crash
    CHECK(snap.sampleRate == 0.0);
}

TEST_CASE("beginSlot/endSlot before prepare is safe")
{
    PerfMonitor pm;
    pm.enable();
    pm.enableSlotProfiling();
    pm.beginBlock();
    pm.beginSlot(0, 42);
    pm.endSlot(0);
    pm.endBlock();
    // Should not crash
}

// ═══════════════════════════════════════════════════════════════════
// prepare can be called again (reconfigure)
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("prepare with different params updates budget")
{
    PerfMonitor pm;
    pm.prepare(44100.0, 512);
    pm.enable();
    runBlocks(pm, 200);
    auto snap1 = pm.getSnapshot();
    double budget1 = snap1.bufferDurationUs;

    pm.prepare(96000.0, 128);
    runBlocks(pm, 200);
    auto snap2 = pm.getSnapshot();
    double budget2 = snap2.bufferDurationUs;

    double expected2 = 128.0 / 96000.0 * 1e6;
    CHECK_THAT(budget2, WithinRel(expected2, 1e-6));
    CHECK(budget2 != budget1);
}
