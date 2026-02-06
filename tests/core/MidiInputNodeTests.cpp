#include <catch2/catch_test_macros.hpp>
#include "core/MidiInputNode.h"

using namespace squeeze;

// ============================================================
// Port declarations
// ============================================================

TEST_CASE("MidiInputNode has no input ports")
{
    // Construct with a bogus device identifier (won't open a real device)
    MidiInputNode node("TestDevice", "bogus_id");
    auto inputs = node.getInputPorts();
    REQUIRE(inputs.empty());
}

TEST_CASE("MidiInputNode has one MIDI output port")
{
    MidiInputNode node("TestDevice", "bogus_id");
    auto outputs = node.getOutputPorts();

    REQUIRE(outputs.size() == 1);
    REQUIRE(outputs[0].name == "midi_out");
    REQUIRE(outputs[0].direction == PortDirection::output);
    REQUIRE(outputs[0].signalType == SignalType::midi);
    REQUIRE(outputs[0].channels == 1);
}

// ============================================================
// Device name accessor
// ============================================================

TEST_CASE("MidiInputNode getDeviceName returns the device name")
{
    MidiInputNode node("My MIDI Keyboard", "bogus_id");
    REQUIRE(node.getDeviceName() == "My MIDI Keyboard");
}

// ============================================================
// Process with empty queue
// ============================================================

TEST_CASE("MidiInputNode process with empty queue outputs empty MidiBuffer")
{
    MidiInputNode node("TestDevice", "bogus_id");
    node.prepare(44100.0, 512);

    juce::AudioBuffer<float> inputAudio(1, 64);
    juce::AudioBuffer<float> outputAudio(1, 64);
    juce::MidiBuffer inputMidi, outputMidi;

    ProcessContext ctx{inputAudio, outputAudio, inputMidi, outputMidi, 64};
    node.process(ctx);

    REQUIRE(outputMidi.isEmpty());
}

// ============================================================
// Simulated MIDI push -> process -> verify output
// ============================================================

TEST_CASE("MidiInputNode handleIncomingMidiMessage pushes events to process output")
{
    MidiInputNode node("TestDevice", "bogus_id");
    node.prepare(44100.0, 512);

    // Simulate a MIDI note-on from the MIDI thread
    auto noteOn = juce::MidiMessage::noteOn(1, 60, (uint8_t)100);
    node.handleIncomingMidiMessage(nullptr, noteOn);

    juce::AudioBuffer<float> inputAudio(1, 64);
    juce::AudioBuffer<float> outputAudio(1, 64);
    juce::MidiBuffer inputMidi, outputMidi;

    ProcessContext ctx{inputAudio, outputAudio, inputMidi, outputMidi, 64};
    node.process(ctx);

    // Should have exactly one MIDI event
    int count = 0;
    for (const auto& meta : outputMidi)
    {
        (void)meta;
        count++;
    }
    REQUIRE(count == 1);

    // Verify the event data matches note-on
    auto iter = outputMidi.begin();
    auto meta = *iter;
    REQUIRE(meta.numBytes == 3);
    // Note-on channel 1: status 0x90, note 60, velocity 100
    REQUIRE(meta.data[0] == 0x90);
    REQUIRE(meta.data[1] == 60);
    REQUIRE(meta.data[2] == 100);
}

// ============================================================
// Multiple messages in one block arrive in order
// ============================================================

TEST_CASE("MidiInputNode multiple messages arrive in order")
{
    MidiInputNode node("TestDevice", "bogus_id");
    node.prepare(44100.0, 512);

    // Push note-on, CC, note-off
    node.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, (uint8_t)100));
    node.handleIncomingMidiMessage(nullptr, juce::MidiMessage::controllerEvent(1, 1, 64));
    node.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60, (uint8_t)0));

    juce::AudioBuffer<float> inputAudio(1, 64);
    juce::AudioBuffer<float> outputAudio(1, 64);
    juce::MidiBuffer inputMidi, outputMidi;

    ProcessContext ctx{inputAudio, outputAudio, inputMidi, outputMidi, 64};
    node.process(ctx);

    // Should have 3 events in order
    std::vector<uint8_t> statusBytes;
    for (const auto& meta : outputMidi)
        statusBytes.push_back(meta.data[0]);

    REQUIRE(statusBytes.size() == 3);
    REQUIRE(statusBytes[0] == 0x90); // note-on
    REQUIRE(statusBytes[1] == 0xB0); // CC
    REQUIRE(statusBytes[2] == 0x80); // note-off
}

// ============================================================
// SysEx messages are skipped
// ============================================================

TEST_CASE("MidiInputNode skips SysEx messages")
{
    MidiInputNode node("TestDevice", "bogus_id");
    node.prepare(44100.0, 512);

    // Push a SysEx message
    const uint8_t sysex[] = {0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7};
    auto sysExMsg = juce::MidiMessage::createSysExMessage(sysex + 1, 4);
    node.handleIncomingMidiMessage(nullptr, sysExMsg);

    // Also push a regular note-on
    node.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, (uint8_t)100));

    juce::AudioBuffer<float> inputAudio(1, 64);
    juce::AudioBuffer<float> outputAudio(1, 64);
    juce::MidiBuffer inputMidi, outputMidi;

    ProcessContext ctx{inputAudio, outputAudio, inputMidi, outputMidi, 64};
    node.process(ctx);

    // Only the note-on should appear
    int count = 0;
    for (const auto& meta : outputMidi)
    {
        (void)meta;
        count++;
    }
    REQUIRE(count == 1);
}

// ============================================================
// Queue overflow handled gracefully
// ============================================================

TEST_CASE("MidiInputNode handles queue overflow gracefully")
{
    MidiInputNode node("TestDevice", "bogus_id");
    node.prepare(44100.0, 512);

    // Push 1025 messages (queue capacity is 1024)
    for (int i = 0; i < 1025; ++i)
        node.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, (uint8_t)100));

    juce::AudioBuffer<float> inputAudio(1, 64);
    juce::AudioBuffer<float> outputAudio(1, 64);
    juce::MidiBuffer inputMidi, outputMidi;

    ProcessContext ctx{inputAudio, outputAudio, inputMidi, outputMidi, 64};
    node.process(ctx);

    // Should get exactly 1024 events (the queue capacity)
    int count = 0;
    for (const auto& meta : outputMidi)
    {
        (void)meta;
        count++;
    }
    REQUIRE(count == 1024);
}

// ============================================================
// create() with invalid device name
// ============================================================

TEST_CASE("MidiInputNode create with invalid device name returns nullptr and error")
{
    std::string error;
    auto node = MidiInputNode::create("Definitely Not A Real MIDI Device 12345", error);

    REQUIRE(node == nullptr);
    REQUIRE_FALSE(error.empty());
}

// ============================================================
// Process clears output midi each call
// ============================================================

TEST_CASE("MidiInputNode process clears outputMidi before writing")
{
    MidiInputNode node("TestDevice", "bogus_id");
    node.prepare(44100.0, 512);

    // First call with one event
    node.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, (uint8_t)100));

    juce::AudioBuffer<float> inputAudio(1, 64);
    juce::AudioBuffer<float> outputAudio(1, 64);
    juce::MidiBuffer inputMidi, outputMidi;

    ProcessContext ctx{inputAudio, outputAudio, inputMidi, outputMidi, 64};
    node.process(ctx);

    // Second call with no new events — output should be empty
    node.process(ctx);

    REQUIRE(outputMidi.isEmpty());
}
