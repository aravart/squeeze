#include <catch2/catch_test_macros.hpp>
#include "core/Port.h"

using namespace squeeze;

// ============================================================
// PortDescriptor construction
// ============================================================

TEST_CASE("PortDescriptor describes a stereo audio input")
{
    PortDescriptor port{"in", PortDirection::input, SignalType::audio, 2};

    REQUIRE(port.name == "in");
    REQUIRE(port.direction == PortDirection::input);
    REQUIRE(port.signalType == SignalType::audio);
    REQUIRE(port.channels == 2);
}

TEST_CASE("PortDescriptor describes a mono audio output")
{
    PortDescriptor port{"out", PortDirection::output, SignalType::audio, 1};

    REQUIRE(port.direction == PortDirection::output);
    REQUIRE(port.signalType == SignalType::audio);
    REQUIRE(port.channels == 1);
}

TEST_CASE("PortDescriptor describes a MIDI input")
{
    PortDescriptor port{"midi", PortDirection::input, SignalType::midi, 1};

    REQUIRE(port.signalType == SignalType::midi);
    REQUIRE(port.channels == 1);
}

TEST_CASE("PortDescriptor supports multichannel audio")
{
    PortDescriptor port{"surround", PortDirection::output, SignalType::audio, 6};
    REQUIRE(port.channels == 6);
}

// ============================================================
// PortDescriptor equality
// ============================================================

TEST_CASE("PortDescriptors with identical fields are equal")
{
    PortDescriptor a{"in", PortDirection::input, SignalType::audio, 2};
    PortDescriptor b{"in", PortDirection::input, SignalType::audio, 2};
    REQUIRE(a == b);
}

TEST_CASE("PortDescriptors with different names are not equal")
{
    PortDescriptor a{"in", PortDirection::input, SignalType::audio, 2};
    PortDescriptor b{"main", PortDirection::input, SignalType::audio, 2};
    REQUIRE(a != b);
}

TEST_CASE("PortDescriptors with different directions are not equal")
{
    PortDescriptor a{"port", PortDirection::input, SignalType::audio, 2};
    PortDescriptor b{"port", PortDirection::output, SignalType::audio, 2};
    REQUIRE(a != b);
}

TEST_CASE("PortDescriptors with different signal types are not equal")
{
    PortDescriptor a{"port", PortDirection::input, SignalType::audio, 1};
    PortDescriptor b{"port", PortDirection::input, SignalType::midi, 1};
    REQUIRE(a != b);
}

TEST_CASE("PortDescriptors with different channel counts are not equal")
{
    PortDescriptor a{"out", PortDirection::output, SignalType::audio, 1};
    PortDescriptor b{"out", PortDirection::output, SignalType::audio, 2};
    REQUIRE(a != b);
}

// ============================================================
// PortDescriptor validation
// ============================================================

TEST_CASE("Valid stereo audio port passes validation")
{
    PortDescriptor port{"in", PortDirection::input, SignalType::audio, 2};
    REQUIRE(isValid(port));
}

TEST_CASE("Valid MIDI port passes validation")
{
    PortDescriptor port{"midi", PortDirection::input, SignalType::midi, 1};
    REQUIRE(isValid(port));
}

TEST_CASE("Port with empty name is invalid")
{
    PortDescriptor port{"", PortDirection::input, SignalType::audio, 2};
    REQUIRE_FALSE(isValid(port));
}

TEST_CASE("Port with zero channels is invalid")
{
    PortDescriptor port{"in", PortDirection::input, SignalType::audio, 0};
    REQUIRE_FALSE(isValid(port));
}

TEST_CASE("Port with negative channels is invalid")
{
    PortDescriptor port{"in", PortDirection::input, SignalType::audio, -1};
    REQUIRE_FALSE(isValid(port));
}

TEST_CASE("MIDI port with channels != 1 is invalid")
{
    PortDescriptor port{"midi", PortDirection::input, SignalType::midi, 2};
    REQUIRE_FALSE(isValid(port));
}

// ============================================================
// PortAddress
// ============================================================

TEST_CASE("PortAddress identifies a specific port on a specific node")
{
    PortAddress addr{42, PortDirection::output, "out"};

    REQUIRE(addr.nodeId == 42);
    REQUIRE(addr.direction == PortDirection::output);
    REQUIRE(addr.portName == "out");
}

TEST_CASE("PortAddresses with identical fields are equal")
{
    PortAddress a{1, PortDirection::input, "in"};
    PortAddress b{1, PortDirection::input, "in"};
    REQUIRE(a == b);
}

TEST_CASE("PortAddresses with different node IDs are not equal")
{
    PortAddress a{1, PortDirection::input, "in"};
    PortAddress b{2, PortDirection::input, "in"};
    REQUIRE(a != b);
}

TEST_CASE("PortAddresses with different directions are not equal")
{
    PortAddress a{1, PortDirection::input, "port"};
    PortAddress b{1, PortDirection::output, "port"};
    REQUIRE(a != b);
}

TEST_CASE("PortAddresses with different names are not equal")
{
    PortAddress a{1, PortDirection::input, "in"};
    PortAddress b{1, PortDirection::input, "sidechain"};
    REQUIRE(a != b);
}

// ============================================================
// Connection compatibility
// ============================================================

TEST_CASE("Output audio to input audio with matching channels can connect")
{
    PortDescriptor src{"out", PortDirection::output, SignalType::audio, 2};
    PortDescriptor dst{"in", PortDirection::input, SignalType::audio, 2};
    REQUIRE(canConnect(src, dst));
}

TEST_CASE("Output MIDI to input MIDI can connect")
{
    PortDescriptor src{"midi_out", PortDirection::output, SignalType::midi, 1};
    PortDescriptor dst{"midi_in", PortDirection::input, SignalType::midi, 1};
    REQUIRE(canConnect(src, dst));
}

TEST_CASE("Input to input cannot connect")
{
    PortDescriptor a{"in", PortDirection::input, SignalType::audio, 2};
    PortDescriptor b{"in", PortDirection::input, SignalType::audio, 2};
    REQUIRE_FALSE(canConnect(a, b));
}

TEST_CASE("Output to output cannot connect")
{
    PortDescriptor a{"out", PortDirection::output, SignalType::audio, 2};
    PortDescriptor b{"out", PortDirection::output, SignalType::audio, 2};
    REQUIRE_FALSE(canConnect(a, b));
}

TEST_CASE("Input to output cannot connect (wrong direction)")
{
    PortDescriptor src{"in", PortDirection::input, SignalType::audio, 2};
    PortDescriptor dst{"out", PortDirection::output, SignalType::audio, 2};
    REQUIRE_FALSE(canConnect(src, dst));
}

TEST_CASE("Audio to MIDI cannot connect")
{
    PortDescriptor src{"out", PortDirection::output, SignalType::audio, 1};
    PortDescriptor dst{"midi_in", PortDirection::input, SignalType::midi, 1};
    REQUIRE_FALSE(canConnect(src, dst));
}

TEST_CASE("MIDI to audio cannot connect")
{
    PortDescriptor src{"midi_out", PortDirection::output, SignalType::midi, 1};
    PortDescriptor dst{"in", PortDirection::input, SignalType::audio, 1};
    REQUIRE_FALSE(canConnect(src, dst));
}

TEST_CASE("Mismatched audio channel counts can connect")
{
    PortDescriptor src{"out", PortDirection::output, SignalType::audio, 1};
    PortDescriptor dst{"in", PortDirection::input, SignalType::audio, 2};
    REQUIRE(canConnect(src, dst));
}

TEST_CASE("Mono audio ports can connect")
{
    PortDescriptor src{"out", PortDirection::output, SignalType::audio, 1};
    PortDescriptor dst{"in", PortDirection::input, SignalType::audio, 1};
    REQUIRE(canConnect(src, dst));
}
