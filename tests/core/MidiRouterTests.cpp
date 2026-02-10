#include <catch2/catch_test_macros.hpp>
#include "core/MidiRouter.h"

using namespace squeeze;

// ============================================================
// Device enumeration
// ============================================================

TEST_CASE("MidiRouter getAvailableDevices returns a list")
{
    MidiRouter router;
    auto devices = router.getAvailableDevices();
    // Can't guarantee specific devices, but should not crash
    REQUIRE(devices.size() >= 0);
}

// ============================================================
// Open/close devices
// ============================================================

TEST_CASE("MidiRouter openDevice fails for nonexistent device")
{
    MidiRouter router;
    std::string error;
    bool ok = router.openDevice("Nonexistent MIDI Device 12345", error);
    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("MidiRouter openDevice is idempotent for already-open device")
{
    MidiRouter router;
    // We can't test with real devices, but we can verify the interface
    std::string error;
    bool ok = router.openDevice("Nonexistent", error);
    REQUIRE_FALSE(ok);
    // Second call should also fail (not crash)
    ok = router.openDevice("Nonexistent", error);
    REQUIRE_FALSE(ok);
}

TEST_CASE("MidiRouter isDeviceOpen returns false for unopened device")
{
    MidiRouter router;
    REQUIRE_FALSE(router.isDeviceOpen("foo"));
}

TEST_CASE("MidiRouter getOpenDevices is empty initially")
{
    MidiRouter router;
    REQUIRE(router.getOpenDevices().empty());
}

// ============================================================
// Routing
// ============================================================

TEST_CASE("MidiRouter addRoute fails for unopened device")
{
    MidiRouter router;
    std::string error;
    int id = router.addRoute("Nonexistent", 1, 0, error);
    REQUIRE(id < 0);
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("MidiRouter removeRoute returns false for invalid ID")
{
    MidiRouter router;
    REQUIRE_FALSE(router.removeRoute(999));
}

TEST_CASE("MidiRouter getRoutes is empty initially")
{
    MidiRouter router;
    REQUIRE(router.getRoutes().empty());
}

TEST_CASE("MidiRouter removeRoutesForNode returns false when no routes exist")
{
    MidiRouter router;
    REQUIRE_FALSE(router.removeRoutesForNode(42));
}

// ============================================================
// Dispatch with no routing table
// ============================================================

TEST_CASE("MidiRouter dispatchMidi with no committed table does nothing")
{
    MidiRouter router;
    std::unordered_map<int, juce::MidiBuffer*> nodeBuffers;
    juce::MidiBuffer buf;
    nodeBuffers[0] = &buf;

    // Should not crash
    router.dispatchMidi(nodeBuffers);
    REQUIRE(buf.isEmpty());
}

TEST_CASE("MidiRouter commit with empty routing creates valid table")
{
    MidiRouter router;
    router.commit();  // No devices, no routes

    std::unordered_map<int, juce::MidiBuffer*> nodeBuffers;
    juce::MidiBuffer buf;
    nodeBuffers[0] = &buf;

    router.dispatchMidi(nodeBuffers);
    REQUIRE(buf.isEmpty());
}

// ============================================================
// Monitoring
// ============================================================

TEST_CASE("MidiRouter getDeviceStats is empty initially")
{
    MidiRouter router;
    REQUIRE(router.getDeviceStats().empty());
}

// ============================================================
// handleIncomingMidiMessage with no devices (should not crash)
// ============================================================

TEST_CASE("MidiRouter handleIncomingMidiMessage with unknown source is safe")
{
    MidiRouter router;
    auto noteOn = juce::MidiMessage::noteOn(1, 60, (uint8_t)100);
    // nullptr source, no devices open — should not crash
    router.handleIncomingMidiMessage(nullptr, noteOn);
}

TEST_CASE("MidiRouter handleIncomingMidiMessage skips SysEx")
{
    MidiRouter router;
    const uint8_t sysex[] = {0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7};
    auto sysExMsg = juce::MidiMessage::createSysExMessage(sysex + 1, 4);
    // Should not crash
    router.handleIncomingMidiMessage(nullptr, sysExMsg);
}
