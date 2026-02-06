#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/Engine.h"

using namespace squeeze;
using Catch::Matchers::WithinAbs;

// ============================================================
// Test nodes
// ============================================================

// Outputs a constant value on all channels
class ConstNode : public Node {
    float value_;
    int channels_;

public:
    ConstNode(float value, int channels = 2)
        : value_(value), channels_(channels) {}

    void prepare(double, int) override {}
    void process(ProcessContext& ctx) override {
        for (int ch = 0; ch < ctx.outputAudio.getNumChannels(); ++ch)
            for (int i = 0; i < ctx.numSamples; ++i)
                ctx.outputAudio.setSample(ch, i, value_);
    }
    void release() override {}

    std::vector<PortDescriptor> getInputPorts() const override { return {}; }
    std::vector<PortDescriptor> getOutputPorts() const override {
        return {{"out", PortDirection::output, SignalType::audio, channels_}};
    }
};

// Multiplies input by a fixed gain
class TestGainNode : public Node {
    float gain_;

public:
    TestGainNode(float gain) : gain_(gain) {}

    void prepare(double, int) override {}
    void process(ProcessContext& ctx) override {
        for (int ch = 0; ch < ctx.outputAudio.getNumChannels(); ++ch) {
            ctx.outputAudio.copyFrom(ch, 0, ctx.inputAudio, ch, 0, ctx.numSamples);
            ctx.outputAudio.applyGain(ch, 0, ctx.numSamples, gain_);
        }
    }
    void release() override {}

    std::vector<PortDescriptor> getInputPorts() const override {
        return {{"in", PortDirection::input, SignalType::audio, 2}};
    }
    std::vector<PortDescriptor> getOutputPorts() const override {
        return {{"out", PortDirection::output, SignalType::audio, 2}};
    }
};

// Passes MIDI through and generates audio (simulates a synth)
class TestSynthNode : public Node {
public:
    juce::MidiBuffer lastMidiReceived;

    void prepare(double, int) override {}
    void process(ProcessContext& ctx) override {
        lastMidiReceived = ctx.inputMidi;
        // Generate a constant tone to prove we ran
        for (int ch = 0; ch < ctx.outputAudio.getNumChannels(); ++ch)
            for (int i = 0; i < ctx.numSamples; ++i)
                ctx.outputAudio.setSample(ch, i, 0.25f);
    }
    void release() override {}

    std::vector<PortDescriptor> getInputPorts() const override {
        return {{"midi", PortDirection::input, SignalType::midi, 1}};
    }
    std::vector<PortDescriptor> getOutputPorts() const override {
        return {{"out", PortDirection::output, SignalType::audio, 2}};
    }
};

// Outputs MIDI events
class TestMidiSourceNode : public Node {
    juce::MidiBuffer events_;

public:
    void setEvents(const juce::MidiBuffer& events) { events_ = events; }

    void prepare(double, int) override {}
    void process(ProcessContext& ctx) override {
        ctx.outputMidi = events_;
    }
    void release() override {}

    std::vector<PortDescriptor> getInputPorts() const override { return {}; }
    std::vector<PortDescriptor> getOutputPorts() const override {
        return {{"midi", PortDirection::output, SignalType::midi, 1}};
    }
};

// Helper: run one block through engine
static void runBlock(Engine& engine, Scheduler& scheduler,
                     juce::AudioBuffer<float>& output, int numSamples)
{
    juce::MidiBuffer midiOut;
    // Process the scheduler command first
    engine.processBlock(output, midiOut, numSamples);
}

// ============================================================
// No graph → silence
// ============================================================

TEST_CASE("Engine outputs silence with no graph")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 512);

    juce::AudioBuffer<float> output(2, 512);
    // Fill with non-zero to verify it gets cleared
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            output.setSample(ch, i, 1.0f);

    runBlock(engine, sched, output, 512);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            REQUIRE_THAT(output.getSample(ch, i), WithinAbs(0.0f, 1e-6));
}

// ============================================================
// Single node
// ============================================================

TEST_CASE("Engine processes a single source node")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    ConstNode source(0.5f);
    source.prepare(44100.0, 64);

    Graph graph;
    graph.addNode(&source);

    engine.updateGraph(graph);

    juce::AudioBuffer<float> output(2, 64);
    runBlock(engine, sched, output, 64);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            REQUIRE_THAT(output.getSample(ch, i), WithinAbs(0.5f, 1e-6));
}

// ============================================================
// Two-node chain
// ============================================================

TEST_CASE("Engine routes audio through a two-node chain")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    ConstNode source(1.0f);
    TestGainNode gain(0.5f);
    source.prepare(44100.0, 64);
    gain.prepare(44100.0, 64);

    Graph graph;
    int srcId = graph.addNode(&source);
    int gainId = graph.addNode(&gain);
    graph.connect({srcId, PortDirection::output, "out"},
                  {gainId, PortDirection::input, "in"});

    engine.updateGraph(graph);

    juce::AudioBuffer<float> output(2, 64);
    runBlock(engine, sched, output, 64);

    // 1.0 * 0.5 = 0.5
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            REQUIRE_THAT(output.getSample(ch, i), WithinAbs(0.5f, 1e-6));
}

// ============================================================
// Three-node chain
// ============================================================

TEST_CASE("Engine routes audio through a three-node chain")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    ConstNode source(1.0f);
    TestGainNode gain1(0.5f);
    TestGainNode gain2(0.5f);
    source.prepare(44100.0, 64);
    gain1.prepare(44100.0, 64);
    gain2.prepare(44100.0, 64);

    Graph graph;
    int srcId = graph.addNode(&source);
    int g1Id = graph.addNode(&gain1);
    int g2Id = graph.addNode(&gain2);
    graph.connect({srcId, PortDirection::output, "out"},
                  {g1Id, PortDirection::input, "in"});
    graph.connect({g1Id, PortDirection::output, "out"},
                  {g2Id, PortDirection::input, "in"});

    engine.updateGraph(graph);

    juce::AudioBuffer<float> output(2, 64);
    runBlock(engine, sched, output, 64);

    // 1.0 * 0.5 * 0.5 = 0.25
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            REQUIRE_THAT(output.getSample(ch, i), WithinAbs(0.25f, 1e-6));
}

// ============================================================
// MIDI routing
// ============================================================

TEST_CASE("Engine routes MIDI from source to synth")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    TestMidiSourceNode midiSrc;
    TestSynthNode synth;
    midiSrc.prepare(44100.0, 64);
    synth.prepare(44100.0, 64);

    juce::MidiBuffer events;
    events.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    midiSrc.setEvents(events);

    Graph graph;
    int midiId = graph.addNode(&midiSrc);
    int synthId = graph.addNode(&synth);
    graph.connect({midiId, PortDirection::output, "midi"},
                  {synthId, PortDirection::input, "midi"});

    engine.updateGraph(graph);

    juce::AudioBuffer<float> output(2, 64);
    runBlock(engine, sched, output, 64);

    // Synth should have received the MIDI
    int midiCount = 0;
    for (const auto& m : synth.lastMidiReceived)
    {
        (void)m;
        midiCount++;
    }
    REQUIRE(midiCount == 1);

    // Synth outputs 0.25
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            REQUIRE_THAT(output.getSample(ch, i), WithinAbs(0.25f, 1e-6));
}

// ============================================================
// MIDI + audio chain
// ============================================================

TEST_CASE("Engine handles MIDI source -> synth -> effect chain")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    TestMidiSourceNode midiSrc;
    TestSynthNode synth;
    TestGainNode effect(0.5f);
    midiSrc.prepare(44100.0, 64);
    synth.prepare(44100.0, 64);
    effect.prepare(44100.0, 64);

    juce::MidiBuffer events;
    events.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    midiSrc.setEvents(events);

    Graph graph;
    int midiId = graph.addNode(&midiSrc);
    int synthId = graph.addNode(&synth);
    int effectId = graph.addNode(&effect);
    graph.connect({midiId, PortDirection::output, "midi"},
                  {synthId, PortDirection::input, "midi"});
    graph.connect({synthId, PortDirection::output, "out"},
                  {effectId, PortDirection::input, "in"});

    engine.updateGraph(graph);

    juce::AudioBuffer<float> output(2, 64);
    runBlock(engine, sched, output, 64);

    // Synth outputs 0.25, gain halves it -> 0.125
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            REQUIRE_THAT(output.getSample(ch, i), WithinAbs(0.125f, 1e-6));
}

// ============================================================
// Parallel branches (leaf node summing)
// ============================================================

TEST_CASE("Two parallel source nodes are summed to output")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    ConstNode source1(0.3f);
    ConstNode source2(0.4f);
    source1.prepare(44100.0, 64);
    source2.prepare(44100.0, 64);

    Graph graph;
    graph.addNode(&source1);
    graph.addNode(&source2);
    // No connections — both are leaf nodes

    engine.updateGraph(graph);

    juce::AudioBuffer<float> output(2, 64);
    runBlock(engine, sched, output, 64);

    // 0.3 + 0.4 = 0.7
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            REQUIRE_THAT(output.getSample(ch, i), WithinAbs(0.7f, 1e-6));
}

TEST_CASE("Only leaf nodes contribute to output, not mid-chain nodes")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    ConstNode source(1.0f);
    TestGainNode gain(0.5f);
    source.prepare(44100.0, 64);
    gain.prepare(44100.0, 64);

    Graph graph;
    int srcId = graph.addNode(&source);
    int gainId = graph.addNode(&gain);
    graph.connect({srcId, PortDirection::output, "out"},
                  {gainId, PortDirection::input, "in"});

    engine.updateGraph(graph);

    juce::AudioBuffer<float> output(2, 64);
    runBlock(engine, sched, output, 64);

    // Only the gain node is a leaf. source is consumed by gain.
    // Output should be 0.5, NOT 1.0 + 0.5 = 1.5
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            REQUIRE_THAT(output.getSample(ch, i), WithinAbs(0.5f, 1e-6));
}

TEST_CASE("Parallel chains are summed: two independent chains")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    // Chain 1: source(1.0) -> gain(0.25) => 0.25
    // Chain 2: source(0.5) -> gain(0.5) => 0.25
    // Output: 0.25 + 0.25 = 0.5
    ConstNode src1(1.0f);
    TestGainNode gain1(0.25f);
    ConstNode src2(0.5f);
    TestGainNode gain2(0.5f);
    src1.prepare(44100.0, 64);
    gain1.prepare(44100.0, 64);
    src2.prepare(44100.0, 64);
    gain2.prepare(44100.0, 64);

    Graph graph;
    int s1 = graph.addNode(&src1);
    int g1 = graph.addNode(&gain1);
    int s2 = graph.addNode(&src2);
    int g2 = graph.addNode(&gain2);
    graph.connect({s1, PortDirection::output, "out"},
                  {g1, PortDirection::input, "in"});
    graph.connect({s2, PortDirection::output, "out"},
                  {g2, PortDirection::input, "in"});

    engine.updateGraph(graph);

    juce::AudioBuffer<float> output(2, 64);
    runBlock(engine, sched, output, 64);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            REQUIRE_THAT(output.getSample(ch, i), WithinAbs(0.5f, 1e-6));
}

// ============================================================
// Graph update while running
// ============================================================

TEST_CASE("Engine can swap to a new graph")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    ConstNode source1(1.0f);
    ConstNode source2(0.3f);
    source1.prepare(44100.0, 64);
    source2.prepare(44100.0, 64);

    // First graph: source1
    Graph graph1;
    graph1.addNode(&source1);
    engine.updateGraph(graph1);

    juce::AudioBuffer<float> output(2, 64);
    runBlock(engine, sched, output, 64);

    REQUIRE_THAT(output.getSample(0, 0), WithinAbs(1.0f, 1e-6));

    // Second graph: source2
    Graph graph2;
    graph2.addNode(&source2);
    engine.updateGraph(graph2);

    runBlock(engine, sched, output, 64);

    REQUIRE_THAT(output.getSample(0, 0), WithinAbs(0.3f, 1e-6));

    // Collect the old snapshot garbage
    sched.collectGarbage();
}

// ============================================================
// Empty graph → silence
// ============================================================

TEST_CASE("Engine outputs silence for empty graph")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    Graph graph;  // empty
    engine.updateGraph(graph);

    juce::AudioBuffer<float> output(2, 64);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            output.setSample(ch, i, 1.0f);

    runBlock(engine, sched, output, 64);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            REQUIRE_THAT(output.getSample(ch, i), WithinAbs(0.0f, 1e-6));
}

// ============================================================
// Unconnected node gets silence input
// ============================================================

TEST_CASE("Unconnected node receives silence as audio input")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    // Gain node with no source connected — input should be silence
    TestGainNode gain(2.0f);
    gain.prepare(44100.0, 64);

    Graph graph;
    graph.addNode(&gain);
    engine.updateGraph(graph);

    juce::AudioBuffer<float> output(2, 64);
    runBlock(engine, sched, output, 64);

    // silence * 2.0 = silence
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            REQUIRE_THAT(output.getSample(ch, i), WithinAbs(0.0f, 1e-6));
}

// ============================================================
// Multiple blocks
// ============================================================

TEST_CASE("Engine produces consistent output across multiple blocks")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    ConstNode source(0.7f);
    source.prepare(44100.0, 64);

    Graph graph;
    graph.addNode(&source);
    engine.updateGraph(graph);

    juce::AudioBuffer<float> output(2, 64);

    for (int block = 0; block < 10; ++block)
    {
        runBlock(engine, sched, output, 64);

        for (int ch = 0; ch < 2; ++ch)
            REQUIRE_THAT(output.getSample(ch, 0), WithinAbs(0.7f, 1e-6));
    }
}

// ============================================================
// Prepare for testing
// ============================================================

TEST_CASE("prepareForTesting sets sample rate and block size")
{
    Scheduler sched;
    Engine engine(sched);

    engine.prepareForTesting(48000.0, 256);

    REQUIRE(engine.getSampleRate() == 48000.0);
    REQUIRE(engine.getBlockSize() == 256);
}
