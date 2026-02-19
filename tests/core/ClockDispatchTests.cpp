#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/ClockDispatch.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <vector>

using namespace squeeze;
using Catch::Matchers::WithinAbs;

// ═══════════════════════════════════════════════════════════════════
// Test helper: thread-safe callback collector
// ═══════════════════════════════════════════════════════════════════

struct CallbackCollector {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<double> beats;

    static void callback(uint32_t /*clockId*/, double beat, void* userData)
    {
        auto* self = static_cast<CallbackCollector*>(userData);
        std::lock_guard<std::mutex> lock(self->mutex);
        self->beats.push_back(beat);
        self->cv.notify_all();
    }

    bool waitFor(size_t count, int timeoutMs = 500)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                           [&] { return beats.size() >= count; });
    }

    std::vector<double> getBeats()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return beats;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex);
        beats.clear();
    }
};

// ═══════════════════════════════════════════════════════════════════
// addClock
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("ClockDispatch addClock returns unique IDs")
{
    ClockDispatch cd;
    CallbackCollector col;
    uint32_t id1 = cd.addClock(1.0, 0.0, CallbackCollector::callback, &col);
    uint32_t id2 = cd.addClock(0.5, 0.0, CallbackCollector::callback, &col);
    CHECK(id1 != 0);
    CHECK(id2 != 0);
    CHECK(id1 != id2);
}

TEST_CASE("ClockDispatch addClock rejects resolution <= 0")
{
    ClockDispatch cd;
    CallbackCollector col;
    CHECK(cd.addClock(0.0, 0.0, CallbackCollector::callback, &col) == 0);
    CHECK(cd.addClock(-1.0, 0.0, CallbackCollector::callback, &col) == 0);
}

TEST_CASE("ClockDispatch addClock rejects latencyMs < 0")
{
    ClockDispatch cd;
    CallbackCollector col;
    CHECK(cd.addClock(1.0, -1.0, CallbackCollector::callback, &col) == 0);
}

TEST_CASE("ClockDispatch addClock rejects null callback")
{
    ClockDispatch cd;
    CHECK(cd.addClock(1.0, 0.0, nullptr, nullptr) == 0);
}

// ═══════════════════════════════════════════════════════════════════
// removeClock
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("ClockDispatch removeClock with invalid ID is no-op")
{
    ClockDispatch cd;
    cd.removeClock(999); // should not crash
}

TEST_CASE("ClockDispatch removeClock stops further callbacks")
{
    ClockDispatch cd;
    CallbackCollector col;
    uint32_t id = cd.addClock(1.0, 0.0, CallbackCollector::callback, &col);

    // Push a beat range that crosses boundary at 1.0
    cd.pushBeatRange({0.0, 1.5, 120.0, false, 0.0, 0.0});
    col.waitFor(1);

    auto beats = col.getBeats();
    CHECK(beats.size() == 1);
    col.clear();

    // Remove and push another range — no callback expected
    cd.removeClock(id);
    cd.pushBeatRange({1.5, 2.5, 120.0, false, 0.0, 0.0});
    // Give dispatch thread time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(col.getBeats().empty());
}

// ═══════════════════════════════════════════════════════════════════
// pushBeatRange — basic boundary detection
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("ClockDispatch fires callback at correct beat boundary")
{
    ClockDispatch cd;
    CallbackCollector col;
    cd.addClock(1.0, 0.0, CallbackCollector::callback, &col);

    // Range [0, 1.5) should fire at beat 1.0
    cd.pushBeatRange({0.0, 1.5, 120.0, false, 0.0, 0.0});
    col.waitFor(1);

    auto beats = col.getBeats();
    REQUIRE(beats.size() == 1);
    CHECK_THAT(beats[0], WithinAbs(1.0, 1e-9));
}

TEST_CASE("ClockDispatch fires multiple boundaries in one range")
{
    ClockDispatch cd;
    CallbackCollector col;
    cd.addClock(0.25, 0.0, CallbackCollector::callback, &col);

    // Range [0, 1.0) should fire at 0.25, 0.5, 0.75, 1.0
    cd.pushBeatRange({0.0, 1.0, 120.0, false, 0.0, 0.0});
    col.waitFor(4);

    auto beats = col.getBeats();
    REQUIRE(beats.size() == 4);
    CHECK_THAT(beats[0], WithinAbs(0.25, 1e-9));
    CHECK_THAT(beats[1], WithinAbs(0.50, 1e-9));
    CHECK_THAT(beats[2], WithinAbs(0.75, 1e-9));
    CHECK_THAT(beats[3], WithinAbs(1.00, 1e-9));
}

TEST_CASE("ClockDispatch no callback when no boundary crossed")
{
    ClockDispatch cd;
    CallbackCollector col;
    cd.addClock(1.0, 0.0, CallbackCollector::callback, &col);

    // Range [0, 0.1) — no beat boundary at resolution 1.0
    cd.pushBeatRange({0.0, 0.1, 120.0, false, 0.0, 0.0});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(col.getBeats().empty());
}

// ═══════════════════════════════════════════════════════════════════
// Latency shift
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("ClockDispatch latency shifts detection window correctly")
{
    ClockDispatch cd;
    CallbackCollector col;

    // At 120 BPM, 500ms latency = 500 * (120/60000) = 1.0 beat
    // So shifted window = [0 + 1.0, 0.5 + 1.0) = [1.0, 1.5)
    // Boundary at beat 1.0 is at windowStart, not inside (startSlot+1..endSlot)
    // Wait... floor(1.0/1.0) = 1, floor(1.5/1.0) = 1. startSlot+1 = 2 > endSlot = 1
    // So no fire. Need a range that includes the boundary inside.
    // Let's use range [-0.1, 0.5) shifted to [0.9, 1.5). floor(0.9) = 0, floor(1.5) = 1. fire at 1.0
    // Actually we can't push negative beats. Let me just use a range [0, 0.5] with latency that
    // shifts past a boundary.
    // At 120 BPM, 250ms = 0.5 beats. Shifted window = [0.5, 1.0). floor(0.5) = 0, floor(1.0) = 1. fire at 1.0
    cd.addClock(1.0, 250.0, CallbackCollector::callback, &col);

    cd.pushBeatRange({0.0, 0.5, 120.0, false, 0.0, 0.0});
    // Shifted: [0.5, 1.0) — boundary at 1.0: floor(0.5/1.0) = 0, floor(1.0/1.0) = 1 → fire at 1.0
    col.waitFor(1);

    auto beats = col.getBeats();
    REQUIRE(beats.size() == 1);
    CHECK_THAT(beats[0], WithinAbs(1.0, 1e-9));
}

// ═══════════════════════════════════════════════════════════════════
// Loop-aware: partial wrap
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("ClockDispatch loop-aware partial wrap fires correct boundaries")
{
    ClockDispatch cd;
    CallbackCollector col;

    // Looping [0, 4), res=1.0, latency=500ms at 120BPM = 1.0 beat lookahead
    // Beat range: [3.0, 3.5), shifted by 1.0 = [4.0, 4.5)
    // Full wrap since windowStart >= loopEnd (4.0 >= 4.0)
    // wrappedStart = 0 + fmod(4.0 - 4.0, 4.0) = 0.0
    // wrappedEnd   = 0 + fmod(4.5 - 4.0, 4.0) = 0.5
    // Fire boundaries in [0.0, 0.5): none at res=1.0 (floor(0/1) = 0, floor(0.5/1) = 0)
    // Let me use a wider range to actually cross a boundary.

    // Range [2.5, 3.5), shifted = [3.5, 4.5), looping [0, 4)
    // Partial wrap: windowStart (3.5) < loopEnd (4.0), windowEnd (4.5) > loopEnd
    // Fire [3.5, 4.0]: floor(3.5/1.0) = 3, floor(4.0/1.0) = 4 → fire at 4.0
    // Overflow = 4.5 - 4.0 = 0.5; fire [0.0, 0.5): floor(0.0/1.0) = 0, floor(0.5/1.0) = 0 → nothing
    // So we should get beat 4.0
    cd.addClock(1.0, 500.0, CallbackCollector::callback, &col);
    cd.pushBeatRange({2.5, 3.5, 120.0, true, 0.0, 4.0});
    col.waitFor(1);

    auto beats = col.getBeats();
    REQUIRE(beats.size() == 1);
    CHECK_THAT(beats[0], WithinAbs(4.0, 1e-9));
}

// ═══════════════════════════════════════════════════════════════════
// Loop-aware: full wrap
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("ClockDispatch loop-aware full wrap fires correct boundaries")
{
    ClockDispatch cd;
    CallbackCollector col;

    // Looping [0, 4), res=1.0, latency=2000ms at 120BPM = 4.0 beats lookahead
    // Range: [0.5, 1.5), shifted by 4.0 = [4.5, 5.5)
    // Full wrap: windowStart (4.5) >= loopEnd (4.0)
    // loopLen = 4.0
    // wrappedStart = 0 + fmod(4.5 - 4.0, 4.0) = 0.5
    // wrappedEnd   = 0 + fmod(5.5 - 4.0, 4.0) = 1.5
    // wrappedStart < wrappedEnd, so fire [0.5, 1.5): beat 1.0
    cd.addClock(1.0, 2000.0, CallbackCollector::callback, &col);
    cd.pushBeatRange({0.5, 1.5, 120.0, true, 0.0, 4.0});
    col.waitFor(1);

    auto beats = col.getBeats();
    REQUIRE(beats.size() == 1);
    CHECK_THAT(beats[0], WithinAbs(1.0, 1e-9));
}

// ═══════════════════════════════════════════════════════════════════
// Prime
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("ClockDispatch prime fires all boundaries in lookahead window")
{
    ClockDispatch cd;
    CallbackCollector col;

    // res=1.0, latency=1000ms at 120BPM = 2.0 beats
    // prime at beat 0.0 → priming window [0.0, 2.0)
    // Boundaries: 1.0, 2.0 → wait, [0, 2): floor(0/1) = 0, floor(2/1) = 2 → fire at 1.0, 2.0
    // Actually: startSlot = 0, endSlot = 2, fire t=1 (1.0) and t=2 (2.0)
    // But 2.0 is at the edge — floor(2.0/1.0) = 2, so endSlot = 2, so fire t=1 and t=2
    // Wait, primeEnd is primeStart + latencyBeats = 0 + 2.0 = 2.0
    // fireBoundaries(sub, 0.0, 2.0): floor(0/1) = 0, floor(2/1) = 2 → fire at 1.0 and 2.0
    cd.addClock(1.0, 1000.0, CallbackCollector::callback, &col);
    cd.prime(0.0, 120.0, false, 0.0, 0.0);
    col.waitFor(2);

    auto beats = col.getBeats();
    REQUIRE(beats.size() == 2);
    CHECK_THAT(beats[0], WithinAbs(1.0, 1e-9));
    CHECK_THAT(beats[1], WithinAbs(2.0, 1e-9));
}

TEST_CASE("ClockDispatch prime with zero latency fires nothing")
{
    ClockDispatch cd;
    CallbackCollector col;
    cd.addClock(1.0, 0.0, CallbackCollector::callback, &col);
    cd.prime(0.0, 120.0, false, 0.0, 0.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(col.getBeats().empty());
}

TEST_CASE("ClockDispatch prime + first range are contiguous")
{
    ClockDispatch cd;
    CallbackCollector col;

    // res=1.0, latency=500ms at 120BPM = 1.0 beat
    // prime at beat 0.0 → window [0.0, 1.0) → fire at 1.0? floor(0/1)=0, floor(1.0/1)=1 → fire 1.0
    // First range [0.0, 0.5) shifted → [1.0, 1.5) → floor(1.0)=1, floor(1.5)=1 → no fire
    // So we see: prime fires 1.0, first range fires nothing. Perfect contiguity.
    cd.addClock(1.0, 500.0, CallbackCollector::callback, &col);
    cd.prime(0.0, 120.0, false, 0.0, 0.0);
    col.waitFor(1);

    auto beats = col.getBeats();
    REQUIRE(beats.size() == 1);
    CHECK_THAT(beats[0], WithinAbs(1.0, 1e-9));

    col.clear();

    // Now push first range — shifted to [1.0, 1.5), no new boundary
    cd.pushBeatRange({0.0, 0.5, 120.0, false, 0.0, 0.0});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(col.getBeats().empty());

    // Push range that crosses next boundary: [0.5, 1.5) shifted → [1.5, 2.5)
    // floor(1.5)=1, floor(2.5)=2 → fire at 2.0
    cd.pushBeatRange({0.5, 1.5, 120.0, false, 0.0, 0.0});
    col.waitFor(1);

    beats = col.getBeats();
    REQUIRE(beats.size() == 1);
    CHECK_THAT(beats[0], WithinAbs(2.0, 1e-9));
}

// ═══════════════════════════════════════════════════════════════════
// onTransportStop
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("ClockDispatch onTransportStop clears pending prime")
{
    ClockDispatch cd;
    CallbackCollector col;
    cd.addClock(1.0, 1000.0, CallbackCollector::callback, &col);

    // Prime then immediately stop before dispatch thread processes
    cd.prime(0.0, 120.0, false, 0.0, 0.0);
    cd.onTransportStop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Prime should have been cancelled
    CHECK(col.getBeats().empty());
}

// ═══════════════════════════════════════════════════════════════════
// Multiple subscriptions
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("ClockDispatch multiple subscriptions fire independently")
{
    ClockDispatch cd;
    CallbackCollector col1, col2;

    cd.addClock(1.0, 0.0, CallbackCollector::callback, &col1);
    cd.addClock(0.5, 0.0, CallbackCollector::callback, &col2);

    cd.pushBeatRange({0.0, 1.5, 120.0, false, 0.0, 0.0});

    col1.waitFor(1);
    col2.waitFor(3);

    auto beats1 = col1.getBeats();
    REQUIRE(beats1.size() == 1);
    CHECK_THAT(beats1[0], WithinAbs(1.0, 1e-9));

    auto beats2 = col2.getBeats();
    REQUIRE(beats2.size() == 3);
    CHECK_THAT(beats2[0], WithinAbs(0.5, 1e-9));
    CHECK_THAT(beats2[1], WithinAbs(1.0, 1e-9));
    CHECK_THAT(beats2[2], WithinAbs(1.5, 1e-9));
}

// ═══════════════════════════════════════════════════════════════════
// Callback exception safety
// ═══════════════════════════════════════════════════════════════════

static void throwingCallback(uint32_t, double, void*)
{
    throw std::runtime_error("test exception");
}

TEST_CASE("ClockDispatch callback exception does not crash dispatch thread")
{
    ClockDispatch cd;
    CallbackCollector col;

    // Add throwing clock, then a good one
    cd.addClock(1.0, 0.0, throwingCallback, nullptr);
    cd.addClock(1.0, 0.0, CallbackCollector::callback, &col);

    cd.pushBeatRange({0.0, 1.5, 120.0, false, 0.0, 0.0});
    col.waitFor(1);

    // Good callback should still receive its beat
    auto beats = col.getBeats();
    REQUIRE(beats.size() == 1);
    CHECK_THAT(beats[0], WithinAbs(1.0, 1e-9));
}

// ═══════════════════════════════════════════════════════════════════
// Constructor/destructor (thread lifecycle)
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("ClockDispatch constructor and destructor manage thread lifecycle")
{
    // Just verifying no crash or hang on create+destroy
    { ClockDispatch cd; }
    { ClockDispatch cd; }
}
