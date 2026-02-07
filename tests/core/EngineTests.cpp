#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/Buffer.h"
#include "core/Engine.h"
#include "core/PluginNode.h"

#include <juce_audio_formats/juce_audio_formats.h>

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

// ============================================================
// Test processor for Engine node management tests
// ============================================================

class EngineTestProcessor : public juce::AudioProcessor {
public:
    EngineTestProcessor(int numIn, int numOut, bool midi)
        : AudioProcessor(BusesProperties()
            .withInput("Input", juce::AudioChannelSet::canonicalChannelSet(std::max(numIn, 1)), numIn > 0)
            .withOutput("Output", juce::AudioChannelSet::canonicalChannelSet(std::max(numOut, 1)), numOut > 0))
        , acceptsMidi_(midi)
    {
        addParameter(new juce::AudioParameterFloat(
            juce::ParameterID{"gain", 1}, "Gain",
            juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    }

    const juce::String getName() const override { return "EngineTestPlugin"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) override {
        float gainVal = getParameters()[0]->getValue();
        audio.applyGain(gainVal);
    }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    bool acceptsMidi() const override { return acceptsMidi_; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

private:
    bool acceptsMidi_;
};

static std::unique_ptr<PluginNode> makeEngineTestNode(int numIn, int numOut, bool midi)
{
    auto proc = std::make_unique<EngineTestProcessor>(numIn, numOut, midi);
    return std::make_unique<PluginNode>(std::move(proc), numIn, numOut, midi);
}

// ============================================================
// Engine::addNode / getNode / getNodeName / getNodes
// ============================================================

TEST_CASE("Engine addNode adds a node and returns valid ID")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 512);

    auto node = makeEngineTestNode(0, 2, true);
    int id = engine.addNode(std::move(node), "MySynth");

    REQUIRE(id >= 0);
    REQUIRE(engine.getNode(id) != nullptr);
    REQUIRE(engine.getNodeName(id) == "MySynth");
}

TEST_CASE("Engine getNodes returns all added nodes")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 512);

    auto n1 = makeEngineTestNode(0, 2, true);
    auto n2 = makeEngineTestNode(2, 2, false);
    int id1 = engine.addNode(std::move(n1), "Synth");
    int id2 = engine.addNode(std::move(n2), "FX");

    auto nodes = engine.getNodes();
    REQUIRE(nodes.size() == 2);

    bool foundSynth = false, foundFX = false;
    for (const auto& kv : nodes)
    {
        if (kv.first == id1 && kv.second == "Synth") foundSynth = true;
        if (kv.first == id2 && kv.second == "FX") foundFX = true;
    }
    REQUIRE(foundSynth);
    REQUIRE(foundFX);
}

TEST_CASE("Engine getNode returns nullptr for invalid ID")
{
    Scheduler sched;
    Engine engine(sched);

    REQUIRE(engine.getNode(9999) == nullptr);
}

TEST_CASE("Engine getNodeName returns empty string for invalid ID")
{
    Scheduler sched;
    Engine engine(sched);

    REQUIRE(engine.getNodeName(9999) == "");
}

// ============================================================
// Engine::removeNode
// ============================================================

TEST_CASE("Engine removeNode removes a node")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 512);

    auto node = makeEngineTestNode(0, 2, true);
    int id = engine.addNode(std::move(node), "Synth");

    REQUIRE(engine.removeNode(id));
    REQUIRE(engine.getNode(id) == nullptr);
    REQUIRE(engine.getNodes().empty());
}

TEST_CASE("Engine removeNode returns false for invalid ID")
{
    Scheduler sched;
    Engine engine(sched);

    REQUIRE_FALSE(engine.removeNode(9999));
}

// ============================================================
// Engine::connect / disconnect / getConnections
// ============================================================

TEST_CASE("Engine connect creates a connection between nodes")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 512);

    auto synth = makeEngineTestNode(0, 2, true);
    auto fx = makeEngineTestNode(2, 2, false);
    int synthId = engine.addNode(std::move(synth), "Synth");
    int fxId = engine.addNode(std::move(fx), "FX");

    std::string error;
    int connId = engine.connect(synthId, "out", fxId, "in", error);
    REQUIRE(connId >= 0);
    REQUIRE(error.empty());

    auto conns = engine.getConnections();
    REQUIRE(conns.size() == 1);
    REQUIRE(conns[0].source.nodeId == synthId);
    REQUIRE(conns[0].dest.nodeId == fxId);
}

TEST_CASE("Engine connect returns -1 for invalid nodes")
{
    Scheduler sched;
    Engine engine(sched);

    std::string error;
    int connId = engine.connect(999, "out", 888, "in", error);
    REQUIRE(connId < 0);
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("Engine disconnect removes a connection")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 512);

    auto synth = makeEngineTestNode(0, 2, true);
    auto fx = makeEngineTestNode(2, 2, false);
    int synthId = engine.addNode(std::move(synth), "Synth");
    int fxId = engine.addNode(std::move(fx), "FX");

    std::string error;
    int connId = engine.connect(synthId, "out", fxId, "in", error);
    REQUIRE(connId >= 0);

    REQUIRE(engine.disconnect(connId));
    REQUIRE(engine.getConnections().empty());
}

TEST_CASE("Engine disconnect returns false for invalid connection ID")
{
    Scheduler sched;
    Engine engine(sched);

    REQUIRE_FALSE(engine.disconnect(9999));
}

// ============================================================
// Engine::updateGraph (no-arg) pushes internal graph
// ============================================================

TEST_CASE("Engine updateGraph no-arg pushes internal graph to audio thread")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    // Add a ConstNode via the owned-node API
    auto constNode = std::make_unique<ConstNode>(0.6f);
    constNode->prepare(44100.0, 64);
    engine.addNode(std::move(constNode), "Source");

    engine.updateGraph();

    juce::AudioBuffer<float> output(2, 64);
    juce::MidiBuffer midi;
    engine.processBlock(output, midi, 64);

    REQUIRE_THAT(output.getSample(0, 0), WithinAbs(0.6f, 1e-6));
}

// ============================================================
// Engine::setParameter / getParameter / getParameterNames
// ============================================================

TEST_CASE("Engine setParameterByName and getParameterByName work through Engine API")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 512);

    auto node = makeEngineTestNode(2, 2, false);
    int id = engine.addNode(std::move(node), "FX");

    REQUIRE_THAT(engine.getParameterByName(id, "Gain"), WithinAbs(0.5f, 1e-3));

    REQUIRE(engine.setParameterByName(id, "Gain", 0.75f));
    REQUIRE_THAT(engine.getParameterByName(id, "Gain"), WithinAbs(0.75f, 1e-3));
}

TEST_CASE("Engine setParameter and getParameter work by index")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 512);

    auto node = makeEngineTestNode(2, 2, false);
    int id = engine.addNode(std::move(node), "FX");

    REQUIRE_THAT(engine.getParameter(id, 0), WithinAbs(0.5f, 1e-3));

    REQUIRE(engine.setParameter(id, 0, 0.75f));
    REQUIRE_THAT(engine.getParameter(id, 0), WithinAbs(0.75f, 1e-3));
}

TEST_CASE("Engine setParameter returns false for invalid node ID")
{
    Scheduler sched;
    Engine engine(sched);

    REQUIRE_FALSE(engine.setParameter(9999, 0, 0.5f));
}

TEST_CASE("Engine setParameterByName returns false for invalid node ID")
{
    Scheduler sched;
    Engine engine(sched);

    REQUIRE_FALSE(engine.setParameterByName(9999, "Gain", 0.5f));
}

TEST_CASE("Engine getParameterDescriptors returns descriptors for a PluginNode")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 512);

    auto node = makeEngineTestNode(2, 2, false);
    int id = engine.addNode(std::move(node), "FX");

    auto descs = engine.getParameterDescriptors(id);
    REQUIRE(descs.size() == 1);
    REQUIRE(descs[0].name == "Gain");
    REQUIRE(descs[0].index == 0);
}

TEST_CASE("Engine getParameterDescriptors returns empty for invalid node ID")
{
    Scheduler sched;
    Engine engine(sched);

    REQUIRE(engine.getParameterDescriptors(9999).empty());
}

TEST_CASE("Engine getParameterText returns display text")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 512);

    auto node = makeEngineTestNode(2, 2, false);
    int id = engine.addNode(std::move(node), "FX");

    auto text = engine.getParameterText(id, 0);
    REQUIRE_FALSE(text.empty());
}

TEST_CASE("Engine getParameterText returns empty for invalid node ID")
{
    Scheduler sched;
    Engine engine(sched);

    REQUIRE(engine.getParameterText(9999, 0).empty());
}

// ============================================================
// Engine::loadPluginCache / getAvailablePluginNames
// ============================================================

TEST_CASE("Engine getAvailablePluginNames returns empty with no cache loaded")
{
    Scheduler sched;
    Engine engine(sched);

    REQUIRE(engine.getAvailablePluginNames().empty());
}

TEST_CASE("Engine loadPluginCache returns false for nonexistent file")
{
    Scheduler sched;
    Engine engine(sched);

    REQUIRE_FALSE(engine.loadPluginCache("/nonexistent/path.xml"));
}

TEST_CASE("Engine findPluginByName returns nullptr with no cache")
{
    Scheduler sched;
    Engine engine(sched);

    REQUIRE(engine.findPluginByName("Nonexistent") == nullptr);
}

// ============================================================
// Engine::addPlugin with no cache
// ============================================================

TEST_CASE("Engine addPlugin returns -1 when plugin not in cache")
{
    Scheduler sched;
    Engine engine(sched);

    std::string error;
    REQUIRE(engine.addPlugin("Nonexistent", error) == -1);
    REQUIRE_FALSE(error.empty());
}

// ============================================================
// Engine::addMidiInput
// ============================================================

TEST_CASE("Engine addMidiInput returns -1 for nonexistent device")
{
    Scheduler sched;
    Engine engine(sched);

    std::string error;
    REQUIRE(engine.addMidiInput("Nonexistent MIDI Device 12345", error) == -1);
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("Engine getAvailableMidiInputs returns a vector")
{
    Scheduler sched;
    Engine engine(sched);

    auto inputs = engine.getAvailableMidiInputs();
    // Can't guarantee any devices, but should not crash
    REQUIRE(inputs.size() >= 0);
}

// ============================================================
// Engine::refreshMidiInputs
// ============================================================

TEST_CASE("Engine refreshMidiInputs returns result with added and removed vectors")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 512);

    auto result = engine.refreshMidiInputs();
    // Can't guarantee devices, but result should have valid vectors
    REQUIRE(result.added.size() >= 0);
    REQUIRE(result.removed.size() >= 0);
}

// ============================================================
// Engine::getGraph
// ============================================================

TEST_CASE("Engine getGraph returns internal graph reference")
{
    Scheduler sched;
    Engine engine(sched);

    auto& graph = engine.getGraph();
    REQUIRE(graph.getNodeCount() == 0);

    auto node = makeEngineTestNode(0, 2, true);
    engine.addNode(std::move(node), "Synth");

    REQUIRE(graph.getNodeCount() == 1);
}

// ============================================================
// MIDI channel filtering
// ============================================================

TEST_CASE("MIDI channel filter passes matching channel")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    TestMidiSourceNode midiSrc;
    TestSynthNode synth;
    midiSrc.prepare(44100.0, 64);
    synth.prepare(44100.0, 64);

    juce::MidiBuffer events;
    events.addEvent(juce::MidiMessage::noteOn(3, 60, 0.8f), 0);
    midiSrc.setEvents(events);

    Graph graph;
    int midiId = graph.addNode(&midiSrc);
    int synthId = graph.addNode(&synth);
    graph.connect({midiId, PortDirection::output, "midi"},
                  {synthId, PortDirection::input, "midi"},
                  3);  // filter to channel 3

    engine.updateGraph(graph);

    juce::AudioBuffer<float> output(2, 64);
    runBlock(engine, sched, output, 64);

    int midiCount = 0;
    for (const auto& m : synth.lastMidiReceived)
    {
        (void)m;
        midiCount++;
    }
    REQUIRE(midiCount == 1);
}

TEST_CASE("MIDI channel filter blocks non-matching channel")
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
                  {synthId, PortDirection::input, "midi"},
                  3);  // filter to channel 3, but event is on channel 1

    engine.updateGraph(graph);

    juce::AudioBuffer<float> output(2, 64);
    runBlock(engine, sched, output, 64);

    int midiCount = 0;
    for (const auto& m : synth.lastMidiReceived)
    {
        (void)m;
        midiCount++;
    }
    REQUIRE(midiCount == 0);
}

TEST_CASE("MIDI channel 0 passes all channels")
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
    events.addEvent(juce::MidiMessage::noteOn(5, 64, 0.6f), 10);
    events.addEvent(juce::MidiMessage::noteOn(10, 67, 0.7f), 20);
    midiSrc.setEvents(events);

    Graph graph;
    int midiId = graph.addNode(&midiSrc);
    int synthId = graph.addNode(&synth);
    graph.connect({midiId, PortDirection::output, "midi"},
                  {synthId, PortDirection::input, "midi"},
                  0);  // channel 0 = all pass

    engine.updateGraph(graph);

    juce::AudioBuffer<float> output(2, 64);
    runBlock(engine, sched, output, 64);

    int midiCount = 0;
    for (const auto& m : synth.lastMidiReceived)
    {
        (void)m;
        midiCount++;
    }
    REQUIRE(midiCount == 3);
}

// ============================================================
// MIDI fan-in (multiple sources to one destination)
// ============================================================

TEST_CASE("Multiple MIDI sources merge into one synth node")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    TestMidiSourceNode src1, src2;
    TestSynthNode synth;
    src1.prepare(44100.0, 64);
    src2.prepare(44100.0, 64);
    synth.prepare(44100.0, 64);

    juce::MidiBuffer events1;
    events1.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    src1.setEvents(events1);

    juce::MidiBuffer events2;
    events2.addEvent(juce::MidiMessage::noteOn(1, 72, 0.6f), 5);
    events2.addEvent(juce::MidiMessage::noteOn(1, 76, 0.7f), 10);
    src2.setEvents(events2);

    Graph graph;
    int id1 = graph.addNode(&src1);
    int id2 = graph.addNode(&src2);
    int idSynth = graph.addNode(&synth);
    graph.connect({id1, PortDirection::output, "midi"},
                  {idSynth, PortDirection::input, "midi"});
    graph.connect({id2, PortDirection::output, "midi"},
                  {idSynth, PortDirection::input, "midi"});

    engine.updateGraph(graph);

    juce::AudioBuffer<float> output(2, 64);
    runBlock(engine, sched, output, 64);

    // Synth should receive all 3 events merged from both sources
    int midiCount = 0;
    for (const auto& m : synth.lastMidiReceived)
    {
        (void)m;
        midiCount++;
    }
    REQUIRE(midiCount == 3);
}

TEST_CASE("Multiple MIDI sources with channel filters merge correctly")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    TestMidiSourceNode src1, src2;
    TestSynthNode synth;
    src1.prepare(44100.0, 64);
    src2.prepare(44100.0, 64);
    synth.prepare(44100.0, 64);

    // src1 sends on channels 1 and 2
    juce::MidiBuffer events1;
    events1.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    events1.addEvent(juce::MidiMessage::noteOn(2, 64, 0.8f), 5);
    src1.setEvents(events1);

    // src2 sends on channels 3 and 4
    juce::MidiBuffer events2;
    events2.addEvent(juce::MidiMessage::noteOn(3, 72, 0.6f), 0);
    events2.addEvent(juce::MidiMessage::noteOn(4, 76, 0.7f), 10);
    src2.setEvents(events2);

    Graph graph;
    int id1 = graph.addNode(&src1);
    int id2 = graph.addNode(&src2);
    int idSynth = graph.addNode(&synth);
    // src1 filtered to channel 1 only
    graph.connect({id1, PortDirection::output, "midi"},
                  {idSynth, PortDirection::input, "midi"}, 1);
    // src2 filtered to channel 3 only
    graph.connect({id2, PortDirection::output, "midi"},
                  {idSynth, PortDirection::input, "midi"}, 3);

    engine.updateGraph(graph);

    juce::AudioBuffer<float> output(2, 64);
    runBlock(engine, sched, output, 64);

    // Should receive 2 events: ch1 note from src1, ch3 note from src2
    int midiCount = 0;
    std::vector<int> receivedNotes;
    for (const auto& m : synth.lastMidiReceived)
    {
        auto msg = m.getMessage();
        receivedNotes.push_back(msg.getNoteNumber());
        midiCount++;
    }
    REQUIRE(midiCount == 2);
    REQUIRE(std::find(receivedNotes.begin(), receivedNotes.end(), 60) != receivedNotes.end());
    REQUIRE(std::find(receivedNotes.begin(), receivedNotes.end(), 72) != receivedNotes.end());
}

// ============================================================
// Helper: write a WAV file for buffer tests
// ============================================================

static juce::File createEngineTestWav(int numChannels, int numSamples,
                                       double sampleRate, float fillValue = 0.5f)
{
    auto tempFile = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("squeeze_engine_test.wav");

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(tempFile),
                            sampleRate, numChannels, 16, {}, 0));
    REQUIRE(writer != nullptr);

    juce::AudioBuffer<float> data(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
            data.setSample(ch, s, fillValue);

    writer->writeFromAudioSampleBuffer(data, 0, numSamples);
    writer.reset();
    return tempFile;
}

// ============================================================
// Engine buffer management
// ============================================================

TEST_CASE("Engine loadBuffer loads a file and returns valid ID")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 512);

    auto tempFile = createEngineTestWav(2, 1000, 44100.0);

    std::string err;
    int id = engine.loadBuffer(tempFile.getFullPathName().toStdString(), err);
    REQUIRE(id >= 0);
    CHECK(err.empty());

    Buffer* buf = engine.getBuffer(id);
    REQUIRE(buf != nullptr);
    CHECK(buf->getNumChannels() == 2);
    CHECK(buf->getLengthInSamples() == 1000);

    tempFile.deleteFile();
}

TEST_CASE("Engine loadBuffer returns -1 for nonexistent file")
{
    Scheduler sched;
    Engine engine(sched);

    std::string err;
    int id = engine.loadBuffer("/no/such/file.wav", err);
    REQUIRE(id == -1);
    CHECK(!err.empty());
}

TEST_CASE("Engine createBuffer creates an empty buffer")
{
    Scheduler sched;
    Engine engine(sched);

    std::string err;
    int id = engine.createBuffer(2, 44100, 44100.0, "recording", err);
    REQUIRE(id >= 0);
    CHECK(err.empty());

    Buffer* buf = engine.getBuffer(id);
    REQUIRE(buf != nullptr);
    CHECK(buf->getNumChannels() == 2);
    CHECK(buf->getLengthInSamples() == 44100);
    CHECK(buf->getName() == "recording");
    CHECK(buf->writePosition.load() == 0);
}

TEST_CASE("Engine createBuffer returns -1 for invalid params")
{
    Scheduler sched;
    Engine engine(sched);

    std::string err;
    int id = engine.createBuffer(0, 44100, 44100.0, "bad", err);
    REQUIRE(id == -1);
    CHECK(!err.empty());
}

TEST_CASE("Engine buffer IDs are monotonically increasing")
{
    Scheduler sched;
    Engine engine(sched);

    std::string err;
    int id1 = engine.createBuffer(1, 100, 44100.0, "a", err);
    int id2 = engine.createBuffer(1, 100, 44100.0, "b", err);
    int id3 = engine.createBuffer(1, 100, 44100.0, "c", err);

    REQUIRE(id1 >= 0);
    REQUIRE(id2 > id1);
    REQUIRE(id3 > id2);
}

TEST_CASE("Engine removeBuffer removes a buffer")
{
    Scheduler sched;
    Engine engine(sched);

    std::string err;
    int id = engine.createBuffer(1, 100, 44100.0, "temp", err);
    REQUIRE(id >= 0);

    REQUIRE(engine.removeBuffer(id));
    CHECK(engine.getBuffer(id) == nullptr);
    CHECK(engine.getBufferName(id).empty());
}

TEST_CASE("Engine removeBuffer returns false for invalid ID")
{
    Scheduler sched;
    Engine engine(sched);

    REQUIRE_FALSE(engine.removeBuffer(9999));
}

TEST_CASE("Engine buffer IDs are never reused after removal")
{
    Scheduler sched;
    Engine engine(sched);

    std::string err;
    int id1 = engine.createBuffer(1, 100, 44100.0, "a", err);
    engine.removeBuffer(id1);
    int id2 = engine.createBuffer(1, 100, 44100.0, "b", err);

    REQUIRE(id2 > id1);
}

TEST_CASE("Engine getBufferName returns name or empty")
{
    Scheduler sched;
    Engine engine(sched);

    std::string err;
    int id = engine.createBuffer(1, 100, 44100.0, "myname", err);
    CHECK(engine.getBufferName(id) == "myname");
    CHECK(engine.getBufferName(9999).empty());
}

TEST_CASE("Engine getBuffers returns all buffers")
{
    Scheduler sched;
    Engine engine(sched);

    std::string err;
    int id1 = engine.createBuffer(1, 100, 44100.0, "buf1", err);
    int id2 = engine.createBuffer(1, 100, 44100.0, "buf2", err);

    auto bufs = engine.getBuffers();
    REQUIRE(bufs.size() == 2);

    bool found1 = false, found2 = false;
    for (const auto& kv : bufs)
    {
        if (kv.first == id1 && kv.second == "buf1") found1 = true;
        if (kv.first == id2 && kv.second == "buf2") found2 = true;
    }
    CHECK(found1);
    CHECK(found2);
}

TEST_CASE("Engine getBuffer returns nullptr for invalid ID")
{
    Scheduler sched;
    Engine engine(sched);

    CHECK(engine.getBuffer(9999) == nullptr);
}
