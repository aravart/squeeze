#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/Node.h"

using namespace squeeze;
using Catch::Matchers::WithinAbs;

// ============================================================
// Test node implementations
// ============================================================

class GainNode : public Node {
    float gain = 1.0f;
    double sr = 0;
    int bs = 0;
    bool prepared = false;

public:
    void prepare(double sampleRate, int blockSize) override {
        sr = sampleRate;
        bs = blockSize;
        prepared = true;
    }

    void process(ProcessContext& ctx) override {
        for (int ch = 0; ch < ctx.outputAudio.getNumChannels(); ++ch) {
            ctx.outputAudio.copyFrom(ch, 0, ctx.inputAudio, ch, 0, ctx.numSamples);
            ctx.outputAudio.applyGain(ch, 0, ctx.numSamples, gain);
        }
    }

    void release() override { prepared = false; }

    bool isPrepared() const { return prepared; }
    double getSampleRate() const { return sr; }
    int getBlockSize() const { return bs; }

    std::vector<PortDescriptor> getInputPorts() const override {
        return {{"in", PortDirection::input, SignalType::audio, 2}};
    }

    std::vector<PortDescriptor> getOutputPorts() const override {
        return {{"out", PortDirection::output, SignalType::audio, 2}};
    }

    std::vector<std::string> getParameterNames() const override { return {"gain"}; }

    float getParameter(const std::string& name) const override {
        if (name == "gain") return gain;
        return 0.0f;
    }

    void setParameter(const std::string& name, float value) override {
        if (name == "gain") gain = value;
    }
};

class PassthroughNode : public Node {
    bool prepared = false;

public:
    void prepare(double, int) override { prepared = true; }

    void process(ProcessContext& ctx) override {
        for (int ch = 0; ch < ctx.outputAudio.getNumChannels(); ++ch)
            ctx.outputAudio.copyFrom(ch, 0, ctx.inputAudio, ch, 0, ctx.numSamples);
        ctx.outputMidi = ctx.inputMidi;
    }

    void release() override { prepared = false; }

    bool isPrepared() const { return prepared; }

    std::vector<PortDescriptor> getInputPorts() const override {
        return {
            {"in", PortDirection::input, SignalType::audio, 2},
            {"midi", PortDirection::input, SignalType::midi, 1}
        };
    }

    std::vector<PortDescriptor> getOutputPorts() const override {
        return {
            {"out", PortDirection::output, SignalType::audio, 2},
            {"midi", PortDirection::output, SignalType::midi, 1}
        };
    }
};

// ============================================================
// Lifecycle
// ============================================================

TEST_CASE("Node can be prepared with sample rate and block size")
{
    GainNode node;
    node.prepare(44100.0, 512);

    REQUIRE(node.isPrepared());
    REQUIRE(node.getSampleRate() == 44100.0);
    REQUIRE(node.getBlockSize() == 512);
}

TEST_CASE("Node can be released")
{
    GainNode node;
    node.prepare(44100.0, 512);
    node.release();

    REQUIRE_FALSE(node.isPrepared());
}

TEST_CASE("Node can be prepared again after release")
{
    GainNode node;
    node.prepare(44100.0, 512);
    node.release();
    node.prepare(48000.0, 256);

    REQUIRE(node.isPrepared());
    REQUIRE(node.getSampleRate() == 48000.0);
    REQUIRE(node.getBlockSize() == 256);
}

// ============================================================
// Port declaration
// ============================================================

TEST_CASE("GainNode declares one stereo audio input")
{
    GainNode node;
    auto inputs = node.getInputPorts();

    REQUIRE(inputs.size() == 1);
    REQUIRE(inputs[0].name == "in");
    REQUIRE(inputs[0].direction == PortDirection::input);
    REQUIRE(inputs[0].signalType == SignalType::audio);
    REQUIRE(inputs[0].channels == 2);
}

TEST_CASE("GainNode declares one stereo audio output")
{
    GainNode node;
    auto outputs = node.getOutputPorts();

    REQUIRE(outputs.size() == 1);
    REQUIRE(outputs[0].name == "out");
    REQUIRE(outputs[0].direction == PortDirection::output);
    REQUIRE(outputs[0].signalType == SignalType::audio);
    REQUIRE(outputs[0].channels == 2);
}

TEST_CASE("PassthroughNode declares audio and MIDI inputs")
{
    PassthroughNode node;
    auto inputs = node.getInputPorts();

    REQUIRE(inputs.size() == 2);
    REQUIRE(inputs[0].signalType == SignalType::audio);
    REQUIRE(inputs[1].signalType == SignalType::midi);
}

TEST_CASE("PassthroughNode declares audio and MIDI outputs")
{
    PassthroughNode node;
    auto outputs = node.getOutputPorts();

    REQUIRE(outputs.size() == 2);
    REQUIRE(outputs[0].signalType == SignalType::audio);
    REQUIRE(outputs[1].signalType == SignalType::midi);
}

TEST_CASE("Port declarations are stable across calls")
{
    GainNode node;
    auto first = node.getInputPorts();
    auto second = node.getInputPorts();

    REQUIRE(first == second);
}

TEST_CASE("All declared ports pass validation")
{
    GainNode gain;
    PassthroughNode passthrough;

    for (auto& p : gain.getInputPorts()) REQUIRE(isValid(p));
    for (auto& p : gain.getOutputPorts()) REQUIRE(isValid(p));
    for (auto& p : passthrough.getInputPorts()) REQUIRE(isValid(p));
    for (auto& p : passthrough.getOutputPorts()) REQUIRE(isValid(p));
}

// ============================================================
// Audio processing
// ============================================================

TEST_CASE("GainNode at unity passes audio through unchanged")
{
    GainNode node;
    node.prepare(44100.0, 4);

    juce::AudioBuffer<float> input(2, 4);
    juce::AudioBuffer<float> output(2, 4);
    juce::MidiBuffer midiIn, midiOut;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            input.setSample(ch, i, 0.5f);

    output.clear();
    ProcessContext ctx{input, output, midiIn, midiOut, 4};
    node.process(ctx);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            REQUIRE_THAT(output.getSample(ch, i), WithinAbs(0.5f, 1e-6));
}

TEST_CASE("GainNode applies gain to audio")
{
    GainNode node;
    node.prepare(44100.0, 4);
    node.setParameter("gain", 0.5f);

    juce::AudioBuffer<float> input(2, 4);
    juce::AudioBuffer<float> output(2, 4);
    juce::MidiBuffer midiIn, midiOut;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            input.setSample(ch, i, 1.0f);

    output.clear();
    ProcessContext ctx{input, output, midiIn, midiOut, 4};
    node.process(ctx);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            REQUIRE_THAT(output.getSample(ch, i), WithinAbs(0.5f, 1e-6));
}

TEST_CASE("GainNode at zero gain produces silence")
{
    GainNode node;
    node.prepare(44100.0, 4);
    node.setParameter("gain", 0.0f);

    juce::AudioBuffer<float> input(2, 4);
    juce::AudioBuffer<float> output(2, 4);
    juce::MidiBuffer midiIn, midiOut;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            input.setSample(ch, i, 1.0f);

    output.clear();
    ProcessContext ctx{input, output, midiIn, midiOut, 4};
    node.process(ctx);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            REQUIRE_THAT(output.getSample(ch, i), WithinAbs(0.0f, 1e-6));
}

TEST_CASE("PassthroughNode copies audio unchanged")
{
    PassthroughNode node;
    node.prepare(44100.0, 4);

    juce::AudioBuffer<float> input(2, 4);
    juce::AudioBuffer<float> output(2, 4);
    juce::MidiBuffer midiIn, midiOut;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            input.setSample(ch, i, 0.75f);

    output.clear();
    ProcessContext ctx{input, output, midiIn, midiOut, 4};
    node.process(ctx);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            REQUIRE_THAT(output.getSample(ch, i), WithinAbs(0.75f, 1e-6));
}

TEST_CASE("PassthroughNode copies MIDI unchanged")
{
    PassthroughNode node;
    node.prepare(44100.0, 512);

    juce::AudioBuffer<float> input(2, 512);
    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midiIn, midiOut;

    // Add a note-on message at sample offset 0
    midiIn.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    // Add a note-off at sample offset 100
    midiIn.addEvent(juce::MidiMessage::noteOff(1, 60, 0.0f), 100);

    ProcessContext ctx{input, output, midiIn, midiOut, 512};
    node.process(ctx);

    int count = 0;
    for (const auto metadata : midiOut)
    {
        (void)metadata;
        count++;
    }
    REQUIRE(count == 2);
}

TEST_CASE("Node processes silence correctly")
{
    GainNode node;
    node.prepare(44100.0, 4);

    juce::AudioBuffer<float> input(2, 4);
    juce::AudioBuffer<float> output(2, 4);
    juce::MidiBuffer midiIn, midiOut;

    input.clear();
    output.clear();
    ProcessContext ctx{input, output, midiIn, midiOut, 4};
    node.process(ctx);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            REQUIRE_THAT(output.getSample(ch, i), WithinAbs(0.0f, 1e-6));
}

// ============================================================
// Parameters
// ============================================================

TEST_CASE("GainNode exposes gain parameter")
{
    GainNode node;
    auto names = node.getParameterNames();

    REQUIRE(names.size() == 1);
    REQUIRE(names[0] == "gain");
}

TEST_CASE("GainNode parameter defaults to 1.0")
{
    GainNode node;
    REQUIRE_THAT(node.getParameter("gain"), WithinAbs(1.0f, 1e-6));
}

TEST_CASE("GainNode parameter can be set and read back")
{
    GainNode node;
    node.setParameter("gain", 0.75f);
    REQUIRE_THAT(node.getParameter("gain"), WithinAbs(0.75f, 1e-6));
}

TEST_CASE("Unknown parameter returns 0.0")
{
    GainNode node;
    REQUIRE_THAT(node.getParameter("nonexistent"), WithinAbs(0.0f, 1e-6));
}

TEST_CASE("Setting unknown parameter is a no-op")
{
    GainNode node;
    node.setParameter("nonexistent", 99.0f);
    // Should not crash, and known parameter is unaffected
    REQUIRE_THAT(node.getParameter("gain"), WithinAbs(1.0f, 1e-6));
}

TEST_CASE("Node with no parameters returns empty list")
{
    PassthroughNode node;
    REQUIRE(node.getParameterNames().empty());
}

TEST_CASE("Node with no parameters returns 0.0 for any name")
{
    PassthroughNode node;
    REQUIRE_THAT(node.getParameter("anything"), WithinAbs(0.0f, 1e-6));
}

// ============================================================
// Polymorphism
// ============================================================

TEST_CASE("Node can be used through base pointer")
{
    std::unique_ptr<Node> node = std::make_unique<GainNode>();
    node->prepare(44100.0, 4);

    juce::AudioBuffer<float> input(2, 4);
    juce::AudioBuffer<float> output(2, 4);
    juce::MidiBuffer midiIn, midiOut;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            input.setSample(ch, i, 1.0f);

    output.clear();
    ProcessContext ctx{input, output, midiIn, midiOut, 4};
    node->process(ctx);

    REQUIRE_THAT(output.getSample(0, 0), WithinAbs(1.0f, 1e-6));

    node->release();
}

TEST_CASE("Different node types can coexist as base pointers")
{
    std::vector<std::unique_ptr<Node>> nodes;
    nodes.push_back(std::make_unique<GainNode>());
    nodes.push_back(std::make_unique<PassthroughNode>());

    REQUIRE(nodes[0]->getInputPorts().size() == 1);
    REQUIRE(nodes[1]->getInputPorts().size() == 2);
}
