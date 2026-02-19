#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "ffi/squeeze_ffi.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

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

// Helper: thread-safe callback collector
struct ClockCollector {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<double> beats;

    static void callback(uint32_t /*clockId*/, double beat, void* userData)
    {
        auto* self = static_cast<ClockCollector*>(userData);
        std::lock_guard<std::mutex> lock(self->mutex);
        self->beats.push_back(beat);
        self->cv.notify_all();
    }

    bool waitFor(size_t count, int timeoutMs = 1000)
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
};


// ═══════════════════════════════════════════════════════════════════
// sq_clock_create / sq_clock_destroy
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_clock_create returns non-null with valid params")
{
    FFIEngine e;
    ClockCollector col;
    SqClock clk = sq_clock_create(e, 1.0, 0.0, ClockCollector::callback, &col);
    REQUIRE(clk != nullptr);
    sq_clock_destroy(clk);
}

TEST_CASE("sq_clock_create returns NULL for resolution <= 0")
{
    FFIEngine e;
    ClockCollector col;
    CHECK(sq_clock_create(e, 0.0, 0.0, ClockCollector::callback, &col) == nullptr);
    CHECK(sq_clock_create(e, -1.0, 0.0, ClockCollector::callback, &col) == nullptr);
}

TEST_CASE("sq_clock_create returns NULL for latency < 0")
{
    FFIEngine e;
    ClockCollector col;
    CHECK(sq_clock_create(e, 1.0, -1.0, ClockCollector::callback, &col) == nullptr);
}

TEST_CASE("sq_clock_create returns NULL for null callback")
{
    FFIEngine e;
    CHECK(sq_clock_create(e, 1.0, 0.0, nullptr, nullptr) == nullptr);
}

TEST_CASE("sq_clock_create returns NULL for null engine")
{
    ClockCollector col;
    CHECK(sq_clock_create(nullptr, 1.0, 0.0, ClockCollector::callback, &col) == nullptr);
}

TEST_CASE("sq_clock_destroy NULL is no-op")
{
    sq_clock_destroy(nullptr); // should not crash
}


// ═══════════════════════════════════════════════════════════════════
// sq_clock_get_resolution / sq_clock_get_latency
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_clock_get_resolution returns correct value")
{
    FFIEngine e;
    ClockCollector col;
    SqClock clk = sq_clock_create(e, 0.25, 50.0, ClockCollector::callback, &col);
    REQUIRE(clk != nullptr);
    CHECK_THAT(sq_clock_get_resolution(clk), WithinAbs(0.25, 1e-9));
    sq_clock_destroy(clk);
}

TEST_CASE("sq_clock_get_latency returns correct value")
{
    FFIEngine e;
    ClockCollector col;
    SqClock clk = sq_clock_create(e, 0.25, 50.0, ClockCollector::callback, &col);
    REQUIRE(clk != nullptr);
    CHECK_THAT(sq_clock_get_latency(clk), WithinAbs(50.0, 1e-9));
    sq_clock_destroy(clk);
}

TEST_CASE("sq_clock_get_resolution returns 0 for null clock")
{
    CHECK(sq_clock_get_resolution(nullptr) == 0.0);
}

TEST_CASE("sq_clock_get_latency returns 0 for null clock")
{
    CHECK(sq_clock_get_latency(nullptr) == 0.0);
}


// ═══════════════════════════════════════════════════════════════════
// Clock callback fires during render
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Clock callback fires during render")
{
    FFIEngine e;
    ClockCollector col;

    // res=1.0 beat, latency=0ms → fire at each whole beat
    SqClock clk = sq_clock_create(e, 1.0, 0.0, ClockCollector::callback, &col);
    REQUIRE(clk != nullptr);

    sq_transport_play(e);
    e.flush(); // process play command

    // At 120 BPM, 44100 Hz, bs=512:
    // beatsPerBlock = 512 / (44100 * 60 / 120) = 512 / 22050 ≈ 0.02322
    // Need ~43 blocks to cross beat 1.0
    for (int i = 0; i < 50; ++i)
        e.flush();

    // Give dispatch thread time
    col.waitFor(1);

    auto beats = col.getBeats();
    CHECK(beats.size() >= 1);
    if (!beats.empty())
        CHECK_THAT(beats[0], WithinAbs(1.0, 1e-9));

    sq_clock_destroy(clk);
}

TEST_CASE("Clock fires at correct beats with res=0.25")
{
    FFIEngine e;
    ClockCollector col;

    SqClock clk = sq_clock_create(e, 0.25, 0.0, ClockCollector::callback, &col);
    REQUIRE(clk != nullptr);

    sq_transport_play(e);
    e.flush(); // process play command

    // Render enough blocks to reach beat 1.0+ (43 blocks at 512 samples each)
    for (int i = 0; i < 50; ++i)
        e.flush();

    col.waitFor(4);

    auto beats = col.getBeats();
    CHECK(beats.size() >= 4);
    if (beats.size() >= 4)
    {
        CHECK_THAT(beats[0], WithinAbs(0.25, 1e-9));
        CHECK_THAT(beats[1], WithinAbs(0.50, 1e-9));
        CHECK_THAT(beats[2], WithinAbs(0.75, 1e-9));
        CHECK_THAT(beats[3], WithinAbs(1.00, 1e-9));
    }

    sq_clock_destroy(clk);
}

TEST_CASE("sq_clock_destroy stops further callbacks")
{
    FFIEngine e;
    ClockCollector col;

    SqClock clk = sq_clock_create(e, 1.0, 0.0, ClockCollector::callback, &col);
    REQUIRE(clk != nullptr);

    sq_transport_play(e);
    e.flush();

    // Render past beat 1.0
    for (int i = 0; i < 50; ++i)
        e.flush();
    col.waitFor(1);

    size_t countBefore = col.getBeats().size();
    sq_clock_destroy(clk);

    // Render more blocks — no new callbacks expected
    for (int i = 0; i < 50; ++i)
        e.flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    CHECK(col.getBeats().size() == countBefore);
}
