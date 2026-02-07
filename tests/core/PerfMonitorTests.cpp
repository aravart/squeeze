#include <catch2/catch_test_macros.hpp>

#include "core/PerfMonitor.h"

#include <thread>

using namespace squeeze;

TEST_CASE("PerfMonitor is disabled by default")
{
    PerfMonitor pm;
    REQUIRE_FALSE(pm.isEnabled());
    REQUIRE_FALSE(pm.isNodeProfilingEnabled());
}

TEST_CASE("PerfMonitor enable and disable")
{
    PerfMonitor pm;

    pm.enable();
    REQUIRE(pm.isEnabled());

    pm.disable();
    REQUIRE_FALSE(pm.isEnabled());
}

TEST_CASE("PerfMonitor node profiling enable and disable")
{
    PerfMonitor pm;

    pm.enableNodeProfiling();
    REQUIRE(pm.isNodeProfilingEnabled());

    pm.disableNodeProfiling();
    REQUIRE_FALSE(pm.isNodeProfilingEnabled());
}

TEST_CASE("getSnapshot returns defaults when disabled")
{
    PerfMonitor pm;
    auto snap = pm.getSnapshot();

    REQUIRE(snap.callbackAvgUs == 0.0);
    REQUIRE(snap.callbackPeakUs == 0.0);
    REQUIRE(snap.cpuLoadPercent == 0.0);
    REQUIRE(snap.xrunCount == 0);
    REQUIRE(snap.nodes.empty());
    REQUIRE(snap.midi.empty());
    REQUIRE(snap.sampleRate == 0.0);
    REQUIRE(snap.blockSize == 0);
    REQUIRE(snap.bufferDurationUs == 0.0);
}

TEST_CASE("getSnapshot returns defaults when enabled but no data published")
{
    PerfMonitor pm;
    pm.enable();
    pm.prepare(44100.0, 512);

    auto snap = pm.getSnapshot();
    REQUIRE(snap.callbackAvgUs == 0.0);
    REQUIRE(snap.callbackPeakUs == 0.0);
    REQUIRE(snap.cpuLoadPercent == 0.0);
    REQUIRE(snap.xrunCount == 0);
}

TEST_CASE("prepare sets buffer duration and system context")
{
    PerfMonitor pm;
    pm.enable();
    pm.prepare(44100.0, 512);

    // Trigger enough callbacks to publish (threshold = 44100/512/10 = 8)
    for (int i = 0; i < 10; ++i)
    {
        pm.beginCallback();
        pm.endCallback();
    }

    auto snap = pm.getSnapshot();
    REQUIRE(snap.sampleRate == 44100.0);
    REQUIRE(snap.blockSize == 512);
    // bufferDurationUs = 512/44100 * 1e6 ~= 11609.977
    REQUIRE(snap.bufferDurationUs > 11609.0);
    REQUIRE(snap.bufferDurationUs < 11611.0);
}

TEST_CASE("Callback timing is measured after publish")
{
    PerfMonitor pm;
    pm.enable();
    // Use settings that make threshold = 1 callback per publish
    pm.prepare(1000.0, 100);  // threshold = 1000/100/10 = 1

    pm.beginCallback();
    // Burn a tiny bit of time
    volatile int x = 0;
    for (int i = 0; i < 1000; ++i) x += i;
    (void)x;
    pm.endCallback();

    auto snap = pm.getSnapshot();
    REQUIRE(snap.callbackAvgUs > 0.0);
    REQUIRE(snap.callbackPeakUs > 0.0);
    REQUIRE(snap.callbackPeakUs >= snap.callbackAvgUs);
}

TEST_CASE("CPU load is calculated correctly")
{
    PerfMonitor pm;
    pm.enable();
    pm.prepare(1000.0, 100);  // threshold=1, budget=100000us

    pm.beginCallback();
    pm.endCallback();

    auto snap = pm.getSnapshot();
    // CPU load should be callbackAvgUs / bufferDurationUs * 100
    // With 100000us budget, actual callback takes very little time
    REQUIRE(snap.cpuLoadPercent >= 0.0);
    REQUIRE(snap.cpuLoadPercent < 100.0);  // should be well under budget

    // Verify the formula: cpu = avg / budget * 100
    double expected = (snap.callbackAvgUs / snap.bufferDurationUs) * 100.0;
    REQUIRE(snap.cpuLoadPercent == expected);
}

TEST_CASE("No xrun when callback is within budget")
{
    PerfMonitor pm;
    pm.enable();
    pm.prepare(1000.0, 100);  // budget = 100000us = 100ms (very generous)

    pm.beginCallback();
    pm.endCallback();

    auto snap = pm.getSnapshot();
    REQUIRE(snap.xrunCount == 0);
}

TEST_CASE("Xrun is detected when callback exceeds budget")
{
    PerfMonitor pm;
    pm.enable();
    // budget = 1/1000000 * 1e6 = 1us (impossibly tight)
    pm.prepare(1000000.0, 1);

    pm.beginCallback();
    // Even the clock reads will likely exceed 1us
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    pm.endCallback();

    auto snap = pm.getSnapshot();
    REQUIRE(snap.xrunCount >= 1);
}

TEST_CASE("Xrun count is cumulative across publish windows")
{
    PerfMonitor pm;
    pm.enable();
    pm.prepare(1000000.0, 1);  // 1us budget

    // First window
    pm.beginCallback();
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    pm.endCallback();

    // Second window
    pm.beginCallback();
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    pm.endCallback();

    auto snap = pm.getSnapshot();
    REQUIRE(snap.xrunCount >= 2);
}

TEST_CASE("resetCounters zeros xrun count")
{
    PerfMonitor pm;
    pm.enable();
    pm.prepare(1000000.0, 1);  // 1us budget

    pm.beginCallback();
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    pm.endCallback();

    auto snap = pm.getSnapshot();
    REQUIRE(snap.xrunCount >= 1);

    pm.resetCounters();
    snap = pm.getSnapshot();
    REQUIRE(snap.xrunCount == 0);
}

TEST_CASE("Per-node timing when node profiling enabled")
{
    PerfMonitor pm;
    pm.enable();
    pm.enableNodeProfiling();
    pm.prepare(1000.0, 100);  // threshold=1

    pm.beginCallback();

    pm.beginNode(0, 42);
    volatile int x = 0;
    for (int i = 0; i < 100; ++i) x += i;
    (void)x;
    pm.endNode(0);

    pm.beginNode(1, 99);
    for (int i = 0; i < 100; ++i) x += i;
    (void)x;
    pm.endNode(1);

    pm.endCallback();

    auto snap = pm.getSnapshot();
    REQUIRE(snap.nodes.size() == 2);
    REQUIRE(snap.nodes[0].nodeId == 42);
    REQUIRE(snap.nodes[0].avgUs >= 0.0);
    REQUIRE(snap.nodes[1].nodeId == 99);
    REQUIRE(snap.nodes[1].avgUs >= 0.0);
}

TEST_CASE("Per-node timing is no-op when profiling disabled")
{
    PerfMonitor pm;
    pm.enable();
    // Node profiling is off by default
    pm.prepare(1000.0, 100);  // threshold=1

    pm.beginCallback();
    pm.beginNode(0, 42);
    pm.endNode(0);
    pm.endCallback();

    auto snap = pm.getSnapshot();
    REQUIRE(snap.nodes.empty());
}

TEST_CASE("slotIndex >= kMaxNodes is silently ignored")
{
    PerfMonitor pm;
    pm.enable();
    pm.enableNodeProfiling();
    pm.prepare(1000.0, 100);

    pm.beginCallback();
    // Should not crash
    pm.beginNode(kMaxNodes, 1);
    pm.endNode(kMaxNodes);
    pm.beginNode(kMaxNodes + 100, 2);
    pm.endNode(kMaxNodes + 100);
    pm.beginNode(-1, 3);
    pm.endNode(-1);
    pm.endCallback();

    auto snap = pm.getSnapshot();
    REQUIRE(snap.nodes.empty());
}

TEST_CASE("reportMidiQueue tracks fill level and dropped count")
{
    PerfMonitor pm;
    pm.enable();
    pm.prepare(1000.0, 100);  // threshold=1

    pm.beginCallback();
    pm.reportMidiQueue(10, 5, 2);
    pm.endCallback();

    auto snap = pm.getSnapshot();
    REQUIRE(snap.midi.size() == 1);
    REQUIRE(snap.midi[0].nodeId == 10);
    REQUIRE(snap.midi[0].fillLevel == 5);
    REQUIRE(snap.midi[0].droppedCount == 2);
}

TEST_CASE("reportMidiQueue peak fill level tracks maximum")
{
    PerfMonitor pm;
    pm.enable();
    // threshold = 44100/512/10 = 8
    pm.prepare(44100.0, 512);

    // Simulate several callbacks with varying fill levels
    for (int i = 0; i < 10; ++i)
    {
        pm.beginCallback();
        int fill = (i == 3) ? 100 : 5;  // spike at callback 3
        pm.reportMidiQueue(10, fill, 0);
        pm.endCallback();
    }

    auto snap = pm.getSnapshot();
    REQUIRE(snap.midi.size() == 1);
    REQUIRE(snap.midi[0].peakFillLevel >= 100);
}

TEST_CASE("Multiple MIDI nodes tracked independently")
{
    PerfMonitor pm;
    pm.enable();
    pm.prepare(1000.0, 100);  // threshold=1

    pm.beginCallback();
    pm.reportMidiQueue(10, 3, 0);
    pm.reportMidiQueue(20, 7, 1);
    pm.endCallback();

    auto snap = pm.getSnapshot();
    REQUIRE(snap.midi.size() == 2);
    REQUIRE(snap.midi[0].nodeId == 10);
    REQUIRE(snap.midi[0].fillLevel == 3);
    REQUIRE(snap.midi[1].nodeId == 20);
    REQUIRE(snap.midi[1].fillLevel == 7);
    REQUIRE(snap.midi[1].droppedCount == 1);
}

TEST_CASE("More than kMaxMidiNodes silently ignored")
{
    PerfMonitor pm;
    pm.enable();
    pm.prepare(1000.0, 100);

    pm.beginCallback();
    // Report more than kMaxMidiNodes different nodes
    for (int i = 0; i < kMaxMidiNodes + 10; ++i)
        pm.reportMidiQueue(i, 1, 0);
    pm.endCallback();

    auto snap = pm.getSnapshot();
    REQUIRE((int)snap.midi.size() == kMaxMidiNodes);
}

TEST_CASE("Disabled PerfMonitor: RT methods are no-ops")
{
    PerfMonitor pm;
    // Don't enable — disabled by default
    pm.prepare(1000.0, 100);

    // All of these should be no-ops (no crash)
    pm.beginCallback();
    pm.beginNode(0, 1);
    pm.endNode(0);
    pm.reportMidiQueue(1, 5, 0);
    pm.endCallback();

    // No data should be published
    pm.enable();
    auto snap = pm.getSnapshot();
    REQUIRE(snap.callbackAvgUs == 0.0);
    REQUIRE(snap.xrunCount == 0);
}

TEST_CASE("Callback peak tracks worst case across window")
{
    PerfMonitor pm;
    pm.enable();
    pm.prepare(44100.0, 512);  // threshold=8

    // Do 10 callbacks, one with a sleep
    for (int i = 0; i < 10; ++i)
    {
        pm.beginCallback();
        if (i == 3)
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        pm.endCallback();
    }

    auto snap = pm.getSnapshot();
    // Peak should be significantly larger than average
    REQUIRE(snap.callbackPeakUs > snap.callbackAvgUs);
}

TEST_CASE("getSnapshot returns defaults after disable")
{
    PerfMonitor pm;
    pm.enable();
    pm.prepare(1000.0, 100);

    // Generate some data
    pm.beginCallback();
    pm.endCallback();

    // Verify data exists
    auto snap = pm.getSnapshot();
    REQUIRE(snap.sampleRate == 1000.0);

    // Disable and verify defaults
    pm.disable();
    snap = pm.getSnapshot();
    REQUIRE(snap.callbackAvgUs == 0.0);
    REQUIRE(snap.sampleRate == 0.0);
}

TEST_CASE("Per-node peak tracks worst case")
{
    PerfMonitor pm;
    pm.enable();
    pm.enableNodeProfiling();
    pm.prepare(44100.0, 512);  // threshold=8

    for (int i = 0; i < 10; ++i)
    {
        pm.beginCallback();
        pm.beginNode(0, 42);
        if (i == 5)
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        pm.endNode(0);
        pm.endCallback();
    }

    auto snap = pm.getSnapshot();
    REQUIRE(snap.nodes.size() == 1);
    REQUIRE(snap.nodes[0].peakUs > snap.nodes[0].avgUs);
}
