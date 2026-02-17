#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/PluginNode.h"
#include "core/TestProcessor.h"

using namespace squeeze;

// ═══════════════════════════════════════════════════════════════════
// Helper
// ═══════════════════════════════════════════════════════════════════

static std::unique_ptr<PluginNode> makeEffect()
{
    return std::make_unique<PluginNode>(
        std::make_unique<TestProcessor>(2, 2, false), 2, 2, false);
}

static std::unique_ptr<PluginNode> makeInstrument()
{
    return std::make_unique<PluginNode>(
        std::make_unique<TestProcessor>(0, 2, true), 0, 2, true);
}

// ═══════════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PluginNode construction with effect config")
{
    auto node = makeEffect();
    REQUIRE(node != nullptr);
    CHECK(node->getPluginName() == "TestProcessor");
}

TEST_CASE("PluginNode construction with instrument config")
{
    auto node = makeInstrument();
    REQUIRE(node != nullptr);
    CHECK(node->getPluginName() == "TestProcessor");
}

// ═══════════════════════════════════════════════════════════════════
// Ports
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PluginNode effect ports match constructor args")
{
    auto node = makeEffect();
    auto inputs = node->getInputPorts();
    auto outputs = node->getOutputPorts();

    REQUIRE(inputs.size() == 1);
    CHECK(inputs[0].name == "in");
    CHECK(inputs[0].signalType == SignalType::audio);
    CHECK(inputs[0].channels == 2);

    REQUIRE(outputs.size() == 1);
    CHECK(outputs[0].name == "out");
    CHECK(outputs[0].signalType == SignalType::audio);
    CHECK(outputs[0].channels == 2);
}

TEST_CASE("PluginNode instrument ports include MIDI")
{
    auto node = makeInstrument();
    auto inputs = node->getInputPorts();
    auto outputs = node->getOutputPorts();

    // Instrument: no audio input, but has MIDI input
    REQUIRE(inputs.size() == 1);
    CHECK(inputs[0].name == "midi_in");
    CHECK(inputs[0].signalType == SignalType::midi);

    // Audio output + MIDI output
    REQUIRE(outputs.size() == 2);
    CHECK(outputs[0].name == "out");
    CHECK(outputs[0].signalType == SignalType::audio);
    CHECK(outputs[0].channels == 2);
    CHECK(outputs[1].name == "midi_out");
    CHECK(outputs[1].signalType == SignalType::midi);
}

// ═══════════════════════════════════════════════════════════════════
// Lifecycle delegation
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PluginNode prepare delegates to processor")
{
    auto proc = std::make_unique<TestProcessor>(2, 2, false);
    auto* rawProc = proc.get();
    auto node = std::make_unique<PluginNode>(std::move(proc), 2, 2, false);

    node->prepare(48000.0, 256);
    CHECK(rawProc->preparedSampleRate == 48000.0);
    CHECK(rawProc->preparedBlockSize == 256);
}

TEST_CASE("PluginNode release delegates to processor")
{
    auto proc = std::make_unique<TestProcessor>(2, 2, false);
    auto* rawProc = proc.get();
    auto node = std::make_unique<PluginNode>(std::move(proc), 2, 2, false);

    node->prepare(44100.0, 512);
    node->release();
    CHECK(rawProc->preparedSampleRate == 0.0);
    CHECK(rawProc->preparedBlockSize == 0);
}

// ═══════════════════════════════════════════════════════════════════
// Processing
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PluginNode process delegates to processBlock")
{
    auto proc = std::make_unique<TestProcessor>(2, 2, false);
    auto* rawProc = proc.get();
    auto node = std::make_unique<PluginNode>(std::move(proc), 2, 2, false);

    node->prepare(44100.0, 64);

    juce::AudioBuffer<float> inBuf(2, 64);
    juce::AudioBuffer<float> outBuf(2, 64);
    juce::MidiBuffer inMidi, outMidi;
    inBuf.clear();
    outBuf.clear();

    ProcessContext ctx{inBuf, outBuf, inMidi, outMidi, 64};
    node->process(ctx);

    CHECK(rawProc->processBlockCalled);
    CHECK(rawProc->lastBlockSize == 64);
}

TEST_CASE("PluginNode effect copies input to output before processBlock")
{
    auto node = makeEffect();
    node->prepare(44100.0, 64);

    juce::AudioBuffer<float> inBuf(2, 64);
    juce::AudioBuffer<float> outBuf(2, 64);
    juce::MidiBuffer inMidi, outMidi;

    // Fill input with known value
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            inBuf.setSample(ch, i, 0.5f);
    outBuf.clear();

    ProcessContext ctx{inBuf, outBuf, inMidi, outMidi, 64};
    node->process(ctx);

    // TestProcessor applies Gain=1.0 (default), so output should match input
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            CHECK(outBuf.getSample(ch, i) == 0.5f);
}

TEST_CASE("PluginNode instrument clears output before processBlock")
{
    auto node = makeInstrument();
    node->prepare(44100.0, 64);

    juce::AudioBuffer<float> inBuf(1, 64);
    juce::AudioBuffer<float> outBuf(2, 64);
    juce::MidiBuffer inMidi, outMidi;

    // Pre-fill output with junk
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            outBuf.setSample(ch, i, 99.0f);

    ProcessContext ctx{inBuf, outBuf, inMidi, outMidi, 64};
    node->process(ctx);

    // Instrument clears output, then TestProcessor applies gain (1.0 * 0.0 = 0.0)
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            CHECK(outBuf.getSample(ch, i) == 0.0f);
}

TEST_CASE("PluginNode forwards MIDI input to processor")
{
    auto node = makeInstrument();
    node->prepare(44100.0, 64);

    juce::AudioBuffer<float> inBuf(1, 64);
    juce::AudioBuffer<float> outBuf(2, 64);
    juce::MidiBuffer inMidi, outMidi;
    inBuf.clear();
    outBuf.clear();

    // Add a note-on to input MIDI
    inMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);

    ProcessContext ctx{inBuf, outBuf, inMidi, outMidi, 64};
    node->process(ctx);

    // The output MIDI should contain the event (copied before processBlock)
    CHECK(outMidi.getNumEvents() >= 1);
}

// ═══════════════════════════════════════════════════════════════════
// Parameters
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PluginNode parameter map built correctly")
{
    auto node = makeEffect();
    auto descs = node->getParameterDescriptors();

    REQUIRE(descs.size() == 2);

    // Find Gain and Mix (order may vary)
    bool foundGain = false, foundMix = false;
    for (const auto& d : descs)
    {
        if (d.name == "Gain") foundGain = true;
        if (d.name == "Mix") foundMix = true;
    }
    CHECK(foundGain);
    CHECK(foundMix);
}

TEST_CASE("PluginNode getParameter and setParameter")
{
    auto node = makeEffect();

    // Gain default is 1.0
    CHECK(node->getParameter("Gain") == 1.0f);

    node->setParameter("Gain", 0.5f);
    CHECK_THAT(node->getParameter("Gain"),
               Catch::Matchers::WithinAbs(0.5f, 0.01f));
}

TEST_CASE("PluginNode getParameterText returns non-empty for valid param")
{
    auto node = makeEffect();
    auto text = node->getParameterText("Gain");
    CHECK(!text.empty());
}

TEST_CASE("PluginNode unknown parameter name returns 0.0f and is no-op")
{
    auto node = makeEffect();

    CHECK(node->getParameter("NonExistent") == 0.0f);
    node->setParameter("NonExistent", 0.5f);  // should not crash
    CHECK(node->getParameterText("NonExistent").empty());
}

// ═══════════════════════════════════════════════════════════════════
// Accessors
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PluginNode getProcessor returns the wrapped processor")
{
    auto proc = std::make_unique<TestProcessor>(2, 2, false);
    auto* rawProc = proc.get();
    auto node = std::make_unique<PluginNode>(std::move(proc), 2, 2, false);

    CHECK(node->getProcessor() == rawProc);
}
