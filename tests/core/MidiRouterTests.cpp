#include <catch2/catch_test_macros.hpp>
#include "core/MidiRouter.h"

using squeeze::MidiEvent;
using squeeze::MidiRoute;
using squeeze::MidiRouter;

// Helper: make a note-on event
static MidiEvent noteOn(int channel, int note, int velocity)
{
    MidiEvent e;
    e.data[0] = static_cast<uint8_t>(0x90 | (channel & 0x0F));
    e.data[1] = static_cast<uint8_t>(note & 0x7F);
    e.data[2] = static_cast<uint8_t>(velocity & 0x7F);
    e.size = 3;
    return e;
}

// Helper: make a CC event
static MidiEvent cc(int channel, int ccNum, int value)
{
    MidiEvent e;
    e.data[0] = static_cast<uint8_t>(0xB0 | (channel & 0x0F));
    e.data[1] = static_cast<uint8_t>(ccNum & 0x7F);
    e.data[2] = static_cast<uint8_t>(value & 0x7F);
    e.size = 3;
    return e;
}

// Helper: make a pitch bend event
static MidiEvent pitchBend(int channel, int lsb, int msb)
{
    MidiEvent e;
    e.data[0] = static_cast<uint8_t>(0xE0 | (channel & 0x0F));
    e.data[1] = static_cast<uint8_t>(lsb & 0x7F);
    e.data[2] = static_cast<uint8_t>(msb & 0x7F);
    e.size = 3;
    return e;
}

// Helper: count events in a MidiBuffer
static int countEvents(const juce::MidiBuffer& buf)
{
    int count = 0;
    for (auto it = buf.begin(); it != buf.end(); ++it)
        ++count;
    return count;
}

// ============================================================
// Device queue management
// ============================================================

TEST_CASE("MidiRouter: createDeviceQueue succeeds")
{
    MidiRouter router;
    std::string error;
    REQUIRE(router.createDeviceQueue("KeyStep", error));
    REQUIRE(router.hasDeviceQueue("KeyStep"));
}

TEST_CASE("MidiRouter: createDeviceQueue for existing device is no-op")
{
    MidiRouter router;
    std::string error;
    REQUIRE(router.createDeviceQueue("KeyStep", error));
    REQUIRE(router.createDeviceQueue("KeyStep", error));
    REQUIRE(router.hasDeviceQueue("KeyStep"));
}

TEST_CASE("MidiRouter: removeDeviceQueue removes the device")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    router.removeDeviceQueue("KeyStep");
    REQUIRE_FALSE(router.hasDeviceQueue("KeyStep"));
}

TEST_CASE("MidiRouter: removeDeviceQueue for unknown device is no-op")
{
    MidiRouter router;
    router.removeDeviceQueue("Ghost");
    REQUIRE_FALSE(router.hasDeviceQueue("Ghost"));
}

TEST_CASE("MidiRouter: hasDeviceQueue returns correct state")
{
    MidiRouter router;
    std::string error;
    REQUIRE_FALSE(router.hasDeviceQueue("KeyStep"));
    router.createDeviceQueue("KeyStep", error);
    REQUIRE(router.hasDeviceQueue("KeyStep"));
    router.removeDeviceQueue("KeyStep");
    REQUIRE_FALSE(router.hasDeviceQueue("KeyStep"));
}

// ============================================================
// Route management
// ============================================================

TEST_CASE("MidiRouter: addRoute succeeds with valid parameters")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    int id = router.addRoute("KeyStep", 5, 0, -1, error);
    REQUIRE(id > 0);
}

TEST_CASE("MidiRouter: addRoute fails without device queue")
{
    MidiRouter router;
    std::string error;
    int id = router.addRoute("Ghost", 5, 0, -1, error);
    REQUIRE(id == -1);
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("MidiRouter: addRoute fails with invalid channel filter")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);

    REQUIRE(router.addRoute("KeyStep", 5, -1, -1, error) == -1);
    REQUIRE(router.addRoute("KeyStep", 5, 17, -1, error) == -1);
}

TEST_CASE("MidiRouter: addRoute fails with invalid note filter")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);

    REQUIRE(router.addRoute("KeyStep", 5, 0, -2, error) == -1);
    REQUIRE(router.addRoute("KeyStep", 5, 0, 128, error) == -1);
}

TEST_CASE("MidiRouter: removeRoute returns true for existing, false for unknown")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    int id = router.addRoute("KeyStep", 5, 0, -1, error);

    REQUIRE(router.removeRoute(id));
    REQUIRE_FALSE(router.removeRoute(id));
    REQUIRE_FALSE(router.removeRoute(9999));
}

TEST_CASE("MidiRouter: removeRoutesForNode removes matching routes")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    router.addRoute("KeyStep", 5, 0, -1, error);
    router.addRoute("KeyStep", 5, 1, -1, error);
    router.addRoute("KeyStep", 8, 0, -1, error);

    REQUIRE(router.removeRoutesForNode(5));
    auto routes = router.getRoutes();
    REQUIRE(routes.size() == 1);
    REQUIRE(routes[0].nodeId == 8);
}

TEST_CASE("MidiRouter: removeRoutesForDevice removes matching routes")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    router.createDeviceQueue("Launchpad", error);
    router.addRoute("KeyStep", 5, 0, -1, error);
    router.addRoute("Launchpad", 8, 0, -1, error);

    REQUIRE(router.removeRoutesForDevice("KeyStep"));
    auto routes = router.getRoutes();
    REQUIRE(routes.size() == 1);
    REQUIRE(routes[0].deviceName == "Launchpad");
}

TEST_CASE("MidiRouter: removeDeviceQueue also removes routes for that device")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    router.addRoute("KeyStep", 5, 0, -1, error);
    router.addRoute("KeyStep", 8, 0, -1, error);

    router.removeDeviceQueue("KeyStep");
    REQUIRE(router.getRoutes().empty());
}

TEST_CASE("MidiRouter: getRoutes returns staged routes")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    router.addRoute("KeyStep", 5, 0, -1, error);
    router.addRoute("KeyStep", 8, 2, 36, error);

    auto routes = router.getRoutes();
    REQUIRE(routes.size() == 2);
    REQUIRE(routes[0].nodeId == 5);
    REQUIRE(routes[0].channelFilter == 0);
    REQUIRE(routes[1].nodeId == 8);
    REQUIRE(routes[1].channelFilter == 2);
    REQUIRE(routes[1].noteFilter == 36);
}

TEST_CASE("MidiRouter: route IDs monotonically increase and are never reused")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    int id1 = router.addRoute("KeyStep", 5, 0, -1, error);
    int id2 = router.addRoute("KeyStep", 8, 0, -1, error);
    router.removeRoute(id1);
    int id3 = router.addRoute("KeyStep", 9, 0, -1, error);

    REQUIRE(id1 == 1);
    REQUIRE(id2 == 2);
    REQUIRE(id3 == 3);
}

// ============================================================
// Dispatch
// ============================================================

TEST_CASE("MidiRouter: dispatch with no commit is no-op")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    router.addRoute("KeyStep", 5, 0, -1, error);

    juce::MidiBuffer buf;
    std::unordered_map<int, juce::MidiBuffer*> nodeBuffers = {{5, &buf}};
    router.pushMidiEvent("KeyStep", noteOn(0, 60, 100));

    router.dispatch(nodeBuffers, 512);
    REQUIRE(countEvents(buf) == 0);
}

TEST_CASE("MidiRouter: push and dispatch delivers event to destination MidiBuffer")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    router.addRoute("KeyStep", 5, 0, -1, error);
    router.commit();

    juce::MidiBuffer buf;
    std::unordered_map<int, juce::MidiBuffer*> nodeBuffers = {{5, &buf}};

    router.pushMidiEvent("KeyStep", noteOn(0, 60, 100));
    router.dispatch(nodeBuffers, 512);

    REQUIRE(countEvents(buf) == 1);
}

TEST_CASE("MidiRouter: dispatch preserves MIDI data bytes")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    router.addRoute("KeyStep", 5, 0, -1, error);
    router.commit();

    juce::MidiBuffer buf;
    std::unordered_map<int, juce::MidiBuffer*> nodeBuffers = {{5, &buf}};

    auto sent = noteOn(2, 64, 127);
    router.pushMidiEvent("KeyStep", sent);
    router.dispatch(nodeBuffers, 512);

    auto it = buf.begin();
    REQUIRE(it != buf.end());
    auto meta = *it;
    REQUIRE(meta.numBytes == 3);
    REQUIRE(meta.data[0] == sent.data[0]);
    REQUIRE(meta.data[1] == sent.data[1]);
    REQUIRE(meta.data[2] == sent.data[2]);
    REQUIRE(meta.samplePosition == 0);
}

TEST_CASE("MidiRouter: multiple routes fan-out from one device")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    router.addRoute("KeyStep", 5, 0, -1, error);
    router.addRoute("KeyStep", 8, 0, -1, error);
    router.commit();

    juce::MidiBuffer buf5, buf8;
    std::unordered_map<int, juce::MidiBuffer*> nodeBuffers = {{5, &buf5}, {8, &buf8}};

    router.pushMidiEvent("KeyStep", noteOn(0, 60, 100));
    router.dispatch(nodeBuffers, 512);

    REQUIRE(countEvents(buf5) == 1);
    REQUIRE(countEvents(buf8) == 1);
}

TEST_CASE("MidiRouter: multiple routes fan-in to one node")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    router.createDeviceQueue("Launchpad", error);
    router.addRoute("KeyStep", 5, 0, -1, error);
    router.addRoute("Launchpad", 5, 0, -1, error);
    router.commit();

    juce::MidiBuffer buf;
    std::unordered_map<int, juce::MidiBuffer*> nodeBuffers = {{5, &buf}};

    router.pushMidiEvent("KeyStep", noteOn(0, 60, 100));
    router.pushMidiEvent("Launchpad", noteOn(0, 72, 80));
    router.dispatch(nodeBuffers, 512);

    REQUIRE(countEvents(buf) == 2);
}

TEST_CASE("MidiRouter: dispatch with no events is no-op")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    router.addRoute("KeyStep", 5, 0, -1, error);
    router.commit();

    juce::MidiBuffer buf;
    std::unordered_map<int, juce::MidiBuffer*> nodeBuffers = {{5, &buf}};

    router.dispatch(nodeBuffers, 512);
    REQUIRE(countEvents(buf) == 0);
}

// ============================================================
// Filtering
// ============================================================

TEST_CASE("MidiRouter: channel filter 0 passes all channels")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    router.addRoute("KeyStep", 5, 0, -1, error); // channel 0 = all
    router.commit();

    juce::MidiBuffer buf;
    std::unordered_map<int, juce::MidiBuffer*> nodeBuffers = {{5, &buf}};

    for (int ch = 0; ch < 16; ++ch)
        router.pushMidiEvent("KeyStep", noteOn(ch, 60, 100));

    router.dispatch(nodeBuffers, 512);
    REQUIRE(countEvents(buf) == 16);
}

TEST_CASE("MidiRouter: channel filter rejects non-matching channel")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    router.addRoute("KeyStep", 5, 1, -1, error); // channel 1 only
    router.commit();

    juce::MidiBuffer buf;
    std::unordered_map<int, juce::MidiBuffer*> nodeBuffers = {{5, &buf}};

    // MIDI channel 1 = status byte channel 0
    router.pushMidiEvent("KeyStep", noteOn(0, 60, 100)); // ch 1 — match
    router.pushMidiEvent("KeyStep", noteOn(1, 60, 100)); // ch 2 — no match
    router.pushMidiEvent("KeyStep", noteOn(9, 60, 100)); // ch 10 — no match

    router.dispatch(nodeBuffers, 512);
    REQUIRE(countEvents(buf) == 1);
}

TEST_CASE("MidiRouter: note filter -1 passes all notes")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    router.addRoute("KeyStep", 5, 0, -1, error); // note -1 = all
    router.commit();

    juce::MidiBuffer buf;
    std::unordered_map<int, juce::MidiBuffer*> nodeBuffers = {{5, &buf}};

    router.pushMidiEvent("KeyStep", noteOn(0, 36, 100));
    router.pushMidiEvent("KeyStep", noteOn(0, 60, 100));
    router.pushMidiEvent("KeyStep", noteOn(0, 127, 100));

    router.dispatch(nodeBuffers, 512);
    REQUIRE(countEvents(buf) == 3);
}

TEST_CASE("MidiRouter: note filter rejects non-matching note")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    router.addRoute("KeyStep", 5, 0, 36, error); // note 36 only
    router.commit();

    juce::MidiBuffer buf;
    std::unordered_map<int, juce::MidiBuffer*> nodeBuffers = {{5, &buf}};

    router.pushMidiEvent("KeyStep", noteOn(0, 36, 100)); // match
    router.pushMidiEvent("KeyStep", noteOn(0, 37, 100)); // no match
    router.pushMidiEvent("KeyStep", noteOn(0, 60, 100)); // no match

    router.dispatch(nodeBuffers, 512);
    REQUIRE(countEvents(buf) == 1);
}

TEST_CASE("MidiRouter: note filter passes non-note messages")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);
    router.addRoute("KeyStep", 5, 0, 36, error); // note 36 only
    router.commit();

    juce::MidiBuffer buf;
    std::unordered_map<int, juce::MidiBuffer*> nodeBuffers = {{5, &buf}};

    // CC and pitch bend should pass through even with note filter
    router.pushMidiEvent("KeyStep", cc(0, 1, 64));
    router.pushMidiEvent("KeyStep", pitchBend(0, 0, 64));

    router.dispatch(nodeBuffers, 512);
    REQUIRE(countEvents(buf) == 2);
}

// ============================================================
// Monitoring
// ============================================================

TEST_CASE("MidiRouter: getQueueFillLevel reflects pushed events")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);

    REQUIRE(router.getQueueFillLevel("KeyStep") == 0);

    router.pushMidiEvent("KeyStep", noteOn(0, 60, 100));
    router.pushMidiEvent("KeyStep", noteOn(0, 64, 100));
    REQUIRE(router.getQueueFillLevel("KeyStep") == 2);
}

TEST_CASE("MidiRouter: getDroppedCount increments on overflow")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);

    // Fill queue to capacity (1024)
    for (int i = 0; i < 1024; ++i)
        router.pushMidiEvent("KeyStep", noteOn(0, i % 128, 100));

    REQUIRE(router.getDroppedCount("KeyStep") == 0);

    // This should fail — queue is full
    REQUIRE_FALSE(router.pushMidiEvent("KeyStep", noteOn(0, 60, 100)));
    REQUIRE(router.getDroppedCount("KeyStep") == 1);

    // Push a few more
    router.pushMidiEvent("KeyStep", noteOn(0, 61, 100));
    router.pushMidiEvent("KeyStep", noteOn(0, 62, 100));
    REQUIRE(router.getDroppedCount("KeyStep") == 3);
}

TEST_CASE("MidiRouter: resetDroppedCounts clears counts")
{
    MidiRouter router;
    std::string error;
    router.createDeviceQueue("KeyStep", error);

    // Fill and overflow
    for (int i = 0; i < 1025; ++i)
        router.pushMidiEvent("KeyStep", noteOn(0, i % 128, 100));

    REQUIRE(router.getDroppedCount("KeyStep") > 0);
    router.resetDroppedCounts();
    REQUIRE(router.getDroppedCount("KeyStep") == 0);
}

TEST_CASE("MidiRouter: pushMidiEvent for unknown device returns false")
{
    MidiRouter router;
    REQUIRE_FALSE(router.pushMidiEvent("Ghost", noteOn(0, 60, 100)));
}
