#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/Node.h"
#include "core/GainNode.h"

using namespace squeeze;

// --- Local test helper: PassthroughNode (audio + MIDI, no params) ---

class PassthroughNode : public Node {
public:
    void prepare(double /*sampleRate*/, int /*blockSize*/) override { prepared_ = true; }
    void release() override { prepared_ = false; }

    void process(ProcessContext& ctx) override
    {
        for (int ch = 0; ch < ctx.outputAudio.getNumChannels(); ++ch)
            ctx.outputAudio.copyFrom(ch, 0, ctx.inputAudio, ch, 0, ctx.numSamples);
        ctx.outputMidi = ctx.inputMidi;
    }

    std::vector<PortDescriptor> getInputPorts() const override
    {
        return {{"in", PortDirection::input, SignalType::audio, 2},
                {"midi_in", PortDirection::input, SignalType::midi, 1}};
    }

    std::vector<PortDescriptor> getOutputPorts() const override
    {
        return {{"out", PortDirection::output, SignalType::audio, 2},
                {"midi_out", PortDirection::output, SignalType::midi, 1}};
    }

    bool isPrepared() const { return prepared_; }

private:
    bool prepared_ = false;
};

// ═══════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Node: prepare sets internal state")
{
    PassthroughNode node;
    REQUIRE_FALSE(node.isPrepared());
    node.prepare(44100.0, 512);
    REQUIRE(node.isPrepared());
}

TEST_CASE("Node: release clears internal state")
{
    PassthroughNode node;
    node.prepare(44100.0, 512);
    node.release();
    REQUIRE_FALSE(node.isPrepared());
}

TEST_CASE("Node: re-prepare after release succeeds")
{
    PassthroughNode node;
    node.prepare(44100.0, 512);
    node.release();
    node.prepare(48000.0, 256);
    REQUIRE(node.isPrepared());
}

// ═══════════════════════════════════════════════════════════════════
// Port declaration
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("GainNode: has one audio input port")
{
    GainNode node;
    auto inputs = node.getInputPorts();
    REQUIRE(inputs.size() == 1);
    CHECK(inputs[0].name == "in");
    CHECK(inputs[0].direction == PortDirection::input);
    CHECK(inputs[0].signalType == SignalType::audio);
    CHECK(inputs[0].channels == 2);
}

TEST_CASE("GainNode: has one audio output port")
{
    GainNode node;
    auto outputs = node.getOutputPorts();
    REQUIRE(outputs.size() == 1);
    CHECK(outputs[0].name == "out");
    CHECK(outputs[0].direction == PortDirection::output);
    CHECK(outputs[0].signalType == SignalType::audio);
    CHECK(outputs[0].channels == 2);
}

TEST_CASE("PassthroughNode: has audio and MIDI input ports")
{
    PassthroughNode node;
    auto inputs = node.getInputPorts();
    REQUIRE(inputs.size() == 2);
    CHECK(inputs[0].name == "in");
    CHECK(inputs[0].signalType == SignalType::audio);
    CHECK(inputs[1].name == "midi_in");
    CHECK(inputs[1].signalType == SignalType::midi);
}

TEST_CASE("PassthroughNode: has audio and MIDI output ports")
{
    PassthroughNode node;
    auto outputs = node.getOutputPorts();
    REQUIRE(outputs.size() == 2);
    CHECK(outputs[0].name == "out");
    CHECK(outputs[0].signalType == SignalType::audio);
    CHECK(outputs[1].name == "midi_out");
    CHECK(outputs[1].signalType == SignalType::midi);
}

TEST_CASE("Port declarations are stable across calls")
{
    GainNode node;
    auto a = node.getInputPorts();
    auto b = node.getInputPorts();
    REQUIRE(a == b);

    auto oa = node.getOutputPorts();
    auto ob = node.getOutputPorts();
    REQUIRE(oa == ob);
}

TEST_CASE("All declared ports pass isValid")
{
    GainNode gain;
    for (const auto& p : gain.getInputPorts()) CHECK(isValid(p));
    for (const auto& p : gain.getOutputPorts()) CHECK(isValid(p));

    PassthroughNode pt;
    for (const auto& p : pt.getInputPorts()) CHECK(isValid(p));
    for (const auto& p : pt.getOutputPorts()) CHECK(isValid(p));
}

// ═══════════════════════════════════════════════════════════════════
// Audio processing
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("GainNode: unity gain passes audio through")
{
    GainNode node;
    node.prepare(44100.0, 4);

    juce::AudioBuffer<float> in(2, 4), out(2, 4);
    juce::MidiBuffer midiIn, midiOut;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            in.setSample(ch, i, 0.5f);
    out.clear();

    ProcessContext ctx{in, out, midiIn, midiOut, 4};
    node.process(ctx);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            CHECK(out.getSample(ch, i) == Catch::Approx(0.5f));

    node.release();
}

TEST_CASE("GainNode: applies gain to audio")
{
    GainNode node;
    node.prepare(44100.0, 4);
    node.setParameter("gain", 0.5f);

    juce::AudioBuffer<float> in(2, 4), out(2, 4);
    juce::MidiBuffer midiIn, midiOut;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            in.setSample(ch, i, 1.0f);
    out.clear();

    ProcessContext ctx{in, out, midiIn, midiOut, 4};
    node.process(ctx);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            CHECK(out.getSample(ch, i) == Catch::Approx(0.5f));

    node.release();
}

TEST_CASE("GainNode: zero gain produces silence")
{
    GainNode node;
    node.prepare(44100.0, 4);
    node.setParameter("gain", 0.0f);

    juce::AudioBuffer<float> in(2, 4), out(2, 4);
    juce::MidiBuffer midiIn, midiOut;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            in.setSample(ch, i, 1.0f);
    out.clear();

    ProcessContext ctx{in, out, midiIn, midiOut, 4};
    node.process(ctx);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            CHECK(out.getSample(ch, i) == Catch::Approx(0.0f));

    node.release();
}

TEST_CASE("PassthroughNode: copies audio unchanged")
{
    PassthroughNode node;
    node.prepare(44100.0, 4);

    juce::AudioBuffer<float> in(2, 4), out(2, 4);
    juce::MidiBuffer midiIn, midiOut;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            in.setSample(ch, i, static_cast<float>(ch * 4 + i));
    out.clear();

    ProcessContext ctx{in, out, midiIn, midiOut, 4};
    node.process(ctx);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            CHECK(out.getSample(ch, i) == in.getSample(ch, i));

    node.release();
}

TEST_CASE("PassthroughNode: copies MIDI unchanged")
{
    PassthroughNode node;
    node.prepare(44100.0, 4);

    juce::AudioBuffer<float> in(2, 4), out(2, 4);
    juce::MidiBuffer midiIn, midiOut;
    midiIn.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    midiIn.addEvent(juce::MidiMessage::noteOff(1, 60), 2);

    ProcessContext ctx{in, out, midiIn, midiOut, 4};
    node.process(ctx);

    int count = 0;
    for (const auto metadata : midiOut)
    {
        (void)metadata;
        count++;
    }
    CHECK(count == 2);

    node.release();
}

TEST_CASE("GainNode: processes silence input correctly")
{
    GainNode node;
    node.prepare(44100.0, 4);
    node.setParameter("gain", 2.0f);

    juce::AudioBuffer<float> in(2, 4), out(2, 4);
    juce::MidiBuffer midiIn, midiOut;
    in.clear();
    out.clear();

    ProcessContext ctx{in, out, midiIn, midiOut, 4};
    node.process(ctx);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            CHECK(out.getSample(ch, i) == Catch::Approx(0.0f));

    node.release();
}

// ═══════════════════════════════════════════════════════════════════
// Parameters — descriptors
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("GainNode: parameter descriptors contain gain")
{
    GainNode node;
    auto descs = node.getParameterDescriptors();
    REQUIRE(descs.size() == 1);
    CHECK(descs[0].name == "gain");
    CHECK(descs[0].defaultValue == Catch::Approx(1.0f));
    CHECK(descs[0].numSteps == 0);
    CHECK(descs[0].automatable == true);
    CHECK(descs[0].boolean == false);
    CHECK(descs[0].label == "");
    CHECK(descs[0].group == "");
}

TEST_CASE("GainNode: parameter descriptors are stable across calls")
{
    GainNode node;
    auto a = node.getParameterDescriptors();
    auto b = node.getParameterDescriptors();
    REQUIRE(a.size() == b.size());
    CHECK(a[0].name == b[0].name);
    CHECK(a[0].defaultValue == b[0].defaultValue);
}

// ═══════════════════════════════════════════════════════════════════
// Parameters — string-based get/set
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("GainNode: getParameter returns default value")
{
    GainNode node;
    CHECK(node.getParameter("gain") == Catch::Approx(1.0f));
}

TEST_CASE("GainNode: setParameter then getParameter roundtrips")
{
    GainNode node;
    node.setParameter("gain", 0.75f);
    CHECK(node.getParameter("gain") == Catch::Approx(0.75f));
}

TEST_CASE("GainNode: getParameter with unknown name returns 0.0f")
{
    GainNode node;
    CHECK(node.getParameter("unknown") == Catch::Approx(0.0f));
}

TEST_CASE("GainNode: setParameter with unknown name is a no-op")
{
    GainNode node;
    node.setParameter("unknown", 0.5f);
    CHECK(node.getParameter("gain") == Catch::Approx(1.0f));
}

// ═══════════════════════════════════════════════════════════════════
// Parameters — display text
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("GainNode: getParameterText returns text for known name")
{
    GainNode node;
    auto text = node.getParameterText("gain");
    CHECK_FALSE(text.empty());
}

TEST_CASE("GainNode: getParameterText returns empty for unknown name")
{
    GainNode node;
    CHECK(node.getParameterText("unknown") == "");
}

// ═══════════════════════════════════════════════════════════════════
// Parameters — no parameters (base class defaults)
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PassthroughNode: getParameterDescriptors returns empty")
{
    PassthroughNode node;
    CHECK(node.getParameterDescriptors().empty());
}

TEST_CASE("PassthroughNode: getParameter for unknown name returns 0.0f")
{
    PassthroughNode node;
    CHECK(node.getParameter("anything") == Catch::Approx(0.0f));
}

// ═══════════════════════════════════════════════════════════════════
// Polymorphism
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Node: base pointer calls derived process correctly")
{
    std::unique_ptr<Node> node = std::make_unique<GainNode>();
    node->prepare(44100.0, 4);
    node->setParameter("gain", 0.5f);

    juce::AudioBuffer<float> in(2, 4), out(2, 4);
    juce::MidiBuffer midiIn, midiOut;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            in.setSample(ch, i, 1.0f);
    out.clear();

    ProcessContext ctx{in, out, midiIn, midiOut, 4};
    node->process(ctx);

    CHECK(out.getSample(0, 0) == Catch::Approx(0.5f));
    node->release();
}

TEST_CASE("Node: different node types coexist")
{
    std::vector<std::unique_ptr<Node>> nodes;
    nodes.push_back(std::make_unique<GainNode>());
    nodes.push_back(std::make_unique<PassthroughNode>());

    REQUIRE(nodes.size() == 2);
    CHECK(nodes[0]->getInputPorts().size() == 1);
    CHECK(nodes[1]->getInputPorts().size() == 2);
}
