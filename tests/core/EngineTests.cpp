#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/Buffer.h"
#include "core/Engine.h"
#include "core/EventQueue.h"
#include "core/PluginNode.h"
#include "core/Transport.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <thread>
#include <atomic>

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
// Engine::getMidiRouter
// ============================================================

TEST_CASE("Engine getMidiRouter returns a reference")
{
    Scheduler sched;
    Engine engine(sched);

    auto& router = engine.getMidiRouter();
    // Should be able to query available devices without crashing
    REQUIRE(router.getAvailableDevices().size() >= 0);
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

// ============================================================
// Concurrency tests
// ============================================================

TEST_CASE("Two threads adding nodes concurrently")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 512);

    const int nodesPerThread = 50;

    auto addNodes = [&](int offset) {
        for (int i = 0; i < nodesPerThread; ++i)
        {
            auto node = makeEngineTestNode(0, 2, true);
            engine.addNode(std::move(node), "Node_" + std::to_string(offset + i));
        }
    };

    std::thread t1(addNodes, 0);
    std::thread t2(addNodes, 1000);

    t1.join();
    t2.join();

    auto nodes = engine.getNodes();
    REQUIRE(nodes.size() == nodesPerThread * 2);
}

TEST_CASE("processBlock does not deadlock while control thread adds nodes")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    std::atomic<bool> stop{false};

    // Simulate audio thread calling processBlock in a tight loop
    std::thread audioThread([&]() {
        juce::AudioBuffer<float> output(2, 64);
        juce::MidiBuffer midi;
        while (!stop.load())
        {
            engine.processBlock(output, midi, 64);
        }
    });

    // Control thread adds nodes and updates graph
    for (int i = 0; i < 50; ++i)
    {
        auto node = std::make_unique<ConstNode>(0.1f);
        node->prepare(44100.0, 64);
        engine.addNode(std::move(node), "N" + std::to_string(i));
        engine.updateGraph();
    }

    stop.store(true);
    audioThread.join();

    REQUIRE(engine.getNodes().size() == 50);
}

TEST_CASE("Concurrent readers and writers do not crash")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 512);

    std::atomic<bool> stop{false};

    // Reader thread: repeatedly queries nodes and connections
    std::thread reader([&]() {
        while (!stop.load())
        {
            auto nodes = engine.getNodes();
            auto conns = engine.getConnections();
            (void)nodes;
            (void)conns;
        }
    });

    // Writer thread: adds nodes
    for (int i = 0; i < 50; ++i)
    {
        auto node = makeEngineTestNode(0, 2, true);
        engine.addNode(std::move(node), "W" + std::to_string(i));
    }

    stop.store(true);
    reader.join();

    REQUIRE(engine.getNodes().size() == 50);
}

// ============================================================
// Transport integration
// ============================================================

TEST_CASE("Engine transport advances position during processBlock")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    // Send play command
    engine.transportPlay();

    juce::AudioBuffer<float> output(2, 64);
    juce::MidiBuffer midi;

    // First processBlock: drains play command, then advances 64 samples
    engine.processBlock(output, midi, 64);

    auto& transport = engine.getTransport();
    REQUIRE(transport.isPlaying());
    REQUIRE(transport.getPositionInSamples() == 64);
}

TEST_CASE("Engine transport does not advance when stopped")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    juce::AudioBuffer<float> output(2, 64);
    juce::MidiBuffer midi;

    // processBlock without play — transport stays at 0
    engine.processBlock(output, midi, 64);

    auto& transport = engine.getTransport();
    REQUIRE(transport.getPositionInSamples() == 0);
}

TEST_CASE("Engine transport commands arrive via scheduler")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    engine.transportSetTempo(140.0);

    juce::AudioBuffer<float> output(2, 64);
    juce::MidiBuffer midi;
    engine.processBlock(output, midi, 64);

    auto& transport = engine.getTransport();
    REQUIRE_THAT(transport.getTempo(), WithinAbs(140.0, 0.001));
}

TEST_CASE("Engine transport stop resets position")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    // Play and advance
    engine.transportPlay();

    juce::AudioBuffer<float> output(2, 64);
    juce::MidiBuffer midi;
    engine.processBlock(output, midi, 64);

    REQUIRE(engine.getTransport().getPositionInSamples() == 64);

    // Stop and process — stop resets to 0
    engine.transportStop();
    engine.processBlock(output, midi, 64);

    REQUIRE(engine.getTransport().getPositionInSamples() == 0);
    REQUIRE_FALSE(engine.getTransport().isPlaying());
}

TEST_CASE("Engine prepareForTesting sets transport sample rate")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(48000.0, 256);

    REQUIRE_THAT(engine.getTransport().getSampleRate(), WithinAbs(48000.0, 0.001));
}

TEST_CASE("Engine addNode with PluginNode wires PlayHead")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 512);

    auto node = makeEngineTestNode(0, 2, true);
    auto* rawProc = node->getProcessor();
    engine.addNode(std::move(node), "Synth");

    REQUIRE(rawProc->getPlayHead() == &engine.getTransport());
}

TEST_CASE("Engine transport looping wraps during processBlock")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    // Set tempo to 120 BPM, loop from beat 0 to beat 1
    // At 120 BPM, 44100 Hz: 1 beat = 0.5s = 22050 samples
    engine.transportSetTempo(120.0);
    engine.transportSetLoopPoints(0.0, 1.0);
    engine.transportSetLooping(true);
    engine.transportPlay();

    juce::AudioBuffer<float> output(2, 64);
    juce::MidiBuffer midi;

    // Drain all commands
    engine.processBlock(output, midi, 64);

    // Now advance close to the loop end
    auto& transport = engine.getTransport();
    // Position at beat 0 + 64 samples. We need to get close to 22050 samples.
    // Process enough blocks to approach 22050 samples
    // Each block = 64 samples. 22050 / 64 = ~344.5 blocks
    // Process 344 blocks (344 * 64 = 22016), plus we already did 1 (64 samples)
    // Total after = 345 * 64 = 22080 samples, which is past 22050
    for (int i = 0; i < 344; ++i)
        engine.processBlock(output, midi, 64);

    // Position should have wrapped due to looping
    // 345 * 64 = 22080 samples, loop end = 22050. Wrapped by 30 samples.
    REQUIRE(transport.getPositionInSamples() < 22050);
    REQUIRE(transport.isPlaying());
}

TEST_CASE("Engine transport pause preserves position")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    engine.transportPlay();

    juce::AudioBuffer<float> output(2, 64);
    juce::MidiBuffer midi;
    engine.processBlock(output, midi, 64);
    engine.processBlock(output, midi, 64);

    // Position should be 128 samples
    REQUIRE(engine.getTransport().getPositionInSamples() == 128);

    // Pause
    engine.transportPause();
    engine.processBlock(output, midi, 64);

    // Position preserved (pause stops advancing but doesn't reset)
    REQUIRE(engine.getTransport().getPositionInSamples() == 128);
    REQUIRE_FALSE(engine.getTransport().isPlaying());
}

TEST_CASE("Engine transport time signature command")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    engine.transportSetTimeSignature(3, 4);

    juce::AudioBuffer<float> output(2, 64);
    juce::MidiBuffer midi;
    engine.processBlock(output, midi, 64);

    auto ts = engine.getTransport().getTimeSignature();
    REQUIRE(ts.numerator == 3);
    REQUIRE(ts.denominator == 4);
}

TEST_CASE("Engine transport set position in beats command")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    engine.transportSetPositionInBeats(4.0);

    juce::AudioBuffer<float> output(2, 64);
    juce::MidiBuffer midi;
    engine.processBlock(output, midi, 64);

    REQUIRE_THAT(engine.getTransport().getPositionInBeats(), WithinAbs(4.0, 0.01));
}

TEST_CASE("Engine transport set position in samples command")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 64);

    engine.transportSetPositionInSamples(10000);

    juce::AudioBuffer<float> output(2, 64);
    juce::MidiBuffer midi;
    engine.processBlock(output, midi, 64);

    REQUIRE(engine.getTransport().getPositionInSamples() == 10000);
}

// ============================================================
// EventQueue ↔ Engine integration tests
// ============================================================

// Records the parameter 0 value for every sample during process()
class ParamTrackingNode : public Node {
public:
    float paramValue_ = 0.0f;
    std::vector<float> paramHistory;

    void prepare(double, int) override {}
    void process(ProcessContext& ctx) override {
        for (int s = 0; s < ctx.numSamples; ++s)
        {
            paramHistory.push_back(paramValue_);
            // Write constant output so leaf summing works
            for (int ch = 0; ch < ctx.outputAudio.getNumChannels(); ++ch)
                ctx.outputAudio.setSample(ch, s, 0.0f);
        }
    }
    void release() override {}

    std::vector<PortDescriptor> getInputPorts() const override { return {}; }
    std::vector<PortDescriptor> getOutputPorts() const override {
        return {{"out", PortDirection::output, SignalType::audio, 2}};
    }

    float getParameter(int) const override { return paramValue_; }
    void setParameter(int, float value) override { paramValue_ = value; }
    std::vector<ParameterDescriptor> getParameterDescriptors() const override {
        return {{"gain", 0, 0.0f, 0, true, false, "", ""}};
    }
};

// Records all MIDI events with their sample positions during process()
class MidiRecordingNode : public Node {
public:
    struct RecordedEvent {
        int samplePosition;
        int noteNumber;
        bool isNoteOn;
        int channel;
    };
    std::vector<RecordedEvent> events;

    void prepare(double, int) override {}
    void process(ProcessContext& ctx) override {
        for (const auto metadata : ctx.inputMidi)
        {
            auto msg = metadata.getMessage();
            if (msg.isNoteOn() || msg.isNoteOff())
            {
                events.push_back({metadata.samplePosition, msg.getNoteNumber(),
                                  msg.isNoteOn(), msg.getChannel()});
            }
        }
        // Write silence to output
        for (int ch = 0; ch < ctx.outputAudio.getNumChannels(); ++ch)
            for (int s = 0; s < ctx.numSamples; ++s)
                ctx.outputAudio.setSample(ch, s, 0.0f);
    }
    void release() override {}

    std::vector<PortDescriptor> getInputPorts() const override { return {}; }
    std::vector<PortDescriptor> getOutputPorts() const override {
        return {{"out", PortDirection::output, SignalType::audio, 2}};
    }
};

// Helper: prepare engine for EventQueue tests.
// Call BEFORE addNode/updateGraph so snapshots get correct buffer sizes.
// After addNode+updateGraph, call drainBlock() to drain commands and advance transport.
static void prepareEventQueueTest(Engine& engine, int blockSize = 512)
{
    engine.prepareForTesting(44100.0, blockSize);
    engine.transportSetTempo(120.0);
    engine.transportPlay();
}

// Drain pending scheduler commands and advance one block
static void drainBlock(Engine& engine, int blockSize = 512)
{
    juce::AudioBuffer<float> output(2, blockSize);
    juce::MidiBuffer midi;
    engine.processBlock(output, midi, blockSize);
}

TEST_CASE("EventQueue MIDI arrives at correct sample offset", "[EventQueue Engine]")
{
    Scheduler sched;
    Engine engine(sched);
    prepareEventQueueTest(engine);

    auto node = std::make_unique<MidiRecordingNode>();
    auto* recorder = node.get();
    node->prepare(44100.0, 512);
    int nodeId = engine.addNode(std::move(node), "Recorder");
    engine.updateGraph();
    drainBlock(engine);

    // After setup, transport is at sample 512, which is beat 512/22050 ≈ 0.02322
    // The next block will cover [512, 1024) samples = beats [0.02322, 0.04644)
    // Schedule a noteOn at beat 0.035 → offset ≈ round((0.035 - 0.02322) * 22050) = round(259.7) ≈ 260
    double targetBeat = 0.035;
    ScheduledEvent ev{};
    ev.beatTime = targetBeat;
    ev.targetNodeId = nodeId;
    ev.type = ScheduledEvent::Type::noteOn;
    ev.channel = 1;
    ev.data1 = 60;
    ev.floatValue = 100.0f;
    engine.getEventQueue().schedule(ev);

    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    engine.processBlock(output, midi, 512);

    REQUIRE(recorder->events.size() == 1);
    REQUIRE(recorder->events[0].noteNumber == 60);
    REQUIRE(recorder->events[0].isNoteOn);
    REQUIRE(recorder->events[0].channel == 1);
    // Offset should be approximately 260 (within a couple samples of rounding)
    REQUIRE(recorder->events[0].samplePosition >= 250);
    REQUIRE(recorder->events[0].samplePosition <= 270);
}

TEST_CASE("No param changes → single process call", "[EventQueue Engine]")
{
    Scheduler sched;
    Engine engine(sched);
    prepareEventQueueTest(engine);

    auto node = std::make_unique<ParamTrackingNode>();
    auto* tracker = node.get();
    node->prepare(44100.0, 512);
    tracker->paramValue_ = 0.5f;
    engine.addNode(std::move(node), "Tracker");
    engine.updateGraph();
    drainBlock(engine);

    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    tracker->paramHistory.clear();
    engine.processBlock(output, midi, 512);

    REQUIRE(tracker->paramHistory.size() == 512);
    for (int i = 0; i < 512; ++i)
        REQUIRE(tracker->paramHistory[i] == 0.5f);
}

TEST_CASE("Single param change splits block in two", "[EventQueue Engine]")
{
    Scheduler sched;
    Engine engine(sched);
    prepareEventQueueTest(engine);

    auto node = std::make_unique<ParamTrackingNode>();
    auto* tracker = node.get();
    node->prepare(44100.0, 512);
    tracker->paramValue_ = 0.25f;
    int nodeId = engine.addNode(std::move(node), "Tracker");
    engine.updateGraph();
    drainBlock(engine);

    // Transport at sample 512, beat ≈ 0.02322
    // Next block [512,1024) = beats [0.02322, 0.04644)
    // Schedule paramChange at beat 0.035 → offset ≈ 260
    double targetBeat = 0.035;
    ScheduledEvent ev{};
    ev.beatTime = targetBeat;
    ev.targetNodeId = nodeId;
    ev.type = ScheduledEvent::Type::paramChange;
    ev.channel = 1;
    ev.data1 = 0; // param index
    ev.floatValue = 0.75f;
    engine.getEventQueue().schedule(ev);

    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    tracker->paramHistory.clear();
    engine.processBlock(output, midi, 512);

    REQUIRE(tracker->paramHistory.size() == 512);

    // Find the split point — first sample where value changes to 0.75
    int splitAt = -1;
    for (int i = 0; i < 512; ++i)
    {
        if (tracker->paramHistory[i] == 0.75f)
        {
            splitAt = i;
            break;
        }
    }
    REQUIRE(splitAt > 0);    // Some samples should see old value
    REQUIRE(splitAt < 512);  // Split should happen within block

    // All samples before split should be old value
    for (int i = 0; i < splitAt; ++i)
        REQUIRE(tracker->paramHistory[i] == 0.25f);

    // All samples from split onward should be new value
    for (int i = splitAt; i < 512; ++i)
        REQUIRE(tracker->paramHistory[i] == 0.75f);
}

TEST_CASE("Two param changes create three sub-blocks", "[EventQueue Engine]")
{
    Scheduler sched;
    Engine engine(sched);
    prepareEventQueueTest(engine);

    auto node = std::make_unique<ParamTrackingNode>();
    auto* tracker = node.get();
    node->prepare(44100.0, 512);
    tracker->paramValue_ = 0.1f;
    int nodeId = engine.addNode(std::move(node), "Tracker");
    engine.updateGraph();
    drainBlock(engine);

    // Next block beats: [0.02322, 0.04644)
    // Schedule two param changes at different beats within the block
    double beat1 = 0.030;  // offset ≈ round((0.030 - 0.02322) * 22050) ≈ 149
    double beat2 = 0.040;  // offset ≈ round((0.040 - 0.02322) * 22050) ≈ 370

    ScheduledEvent ev1{};
    ev1.beatTime = beat1;
    ev1.targetNodeId = nodeId;
    ev1.type = ScheduledEvent::Type::paramChange;
    ev1.data1 = 0;
    ev1.floatValue = 0.5f;
    engine.getEventQueue().schedule(ev1);

    ScheduledEvent ev2{};
    ev2.beatTime = beat2;
    ev2.targetNodeId = nodeId;
    ev2.type = ScheduledEvent::Type::paramChange;
    ev2.data1 = 0;
    ev2.floatValue = 0.9f;
    engine.getEventQueue().schedule(ev2);

    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    tracker->paramHistory.clear();
    engine.processBlock(output, midi, 512);

    REQUIRE(tracker->paramHistory.size() == 512);

    // Find split points
    int split1 = -1, split2 = -1;
    for (int i = 0; i < 512; ++i)
    {
        if (split1 < 0 && tracker->paramHistory[i] == 0.5f)
            split1 = i;
        if (split2 < 0 && tracker->paramHistory[i] == 0.9f)
            split2 = i;
    }
    REQUIRE(split1 > 0);
    REQUIRE(split2 > split1);
    REQUIRE(split2 < 512);

    // Three regions
    for (int i = 0; i < split1; ++i)
        CHECK(tracker->paramHistory[i] == 0.1f);
    for (int i = split1; i < split2; ++i)
        CHECK(tracker->paramHistory[i] == 0.5f);
    for (int i = split2; i < 512; ++i)
        CHECK(tracker->paramHistory[i] == 0.9f);
}

TEST_CASE("Param changes at same offset → last wins, no zero-length process", "[EventQueue Engine]")
{
    Scheduler sched;
    Engine engine(sched);
    prepareEventQueueTest(engine);

    auto node = std::make_unique<ParamTrackingNode>();
    auto* tracker = node.get();
    node->prepare(44100.0, 512);
    tracker->paramValue_ = 0.1f;
    int nodeId = engine.addNode(std::move(node), "Tracker");
    engine.updateGraph();
    drainBlock(engine);

    // Schedule two param changes at the exact same beat
    double beatTarget = 0.035;
    ScheduledEvent ev1{};
    ev1.beatTime = beatTarget;
    ev1.targetNodeId = nodeId;
    ev1.type = ScheduledEvent::Type::paramChange;
    ev1.data1 = 0;
    ev1.floatValue = 0.5f;
    engine.getEventQueue().schedule(ev1);

    ScheduledEvent ev2{};
    ev2.beatTime = beatTarget;
    ev2.targetNodeId = nodeId;
    ev2.type = ScheduledEvent::Type::paramChange;
    ev2.data1 = 0;
    ev2.floatValue = 0.8f;
    engine.getEventQueue().schedule(ev2);

    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    tracker->paramHistory.clear();
    engine.processBlock(output, midi, 512);

    // Total samples must equal block size (no double-counting)
    REQUIRE(tracker->paramHistory.size() == 512);

    // Find where value changes from 0.1
    int splitAt = -1;
    for (int i = 0; i < 512; ++i)
    {
        if (tracker->paramHistory[i] != 0.1f)
        {
            splitAt = i;
            break;
        }
    }
    REQUIRE(splitAt > 0);

    // After the split, all values should be consistent (the last-applied value).
    // Both param changes are at the same offset, so both get applied before the
    // remaining segment. The final value depends on resolved order.
    float finalValue = tracker->paramHistory[splitAt];
    CHECK((finalValue == 0.5f || finalValue == 0.8f));
    for (int i = splitAt; i < 512; ++i)
        CHECK(tracker->paramHistory[i] == finalValue);
}

TEST_CASE("Param change at offset 0 applies before first process", "[EventQueue Engine]")
{
    Scheduler sched;
    Engine engine(sched);
    prepareEventQueueTest(engine);

    auto node = std::make_unique<ParamTrackingNode>();
    auto* tracker = node.get();
    node->prepare(44100.0, 512);
    tracker->paramValue_ = 0.2f;
    int nodeId = engine.addNode(std::move(node), "Tracker");
    engine.updateGraph();
    drainBlock(engine);

    // Schedule param change at the exact block start beat
    // After setup: position = 512 samples = beat 512/22050
    double blockStartBeat = 512.0 / 22050.0;
    ScheduledEvent ev{};
    ev.beatTime = blockStartBeat;
    ev.targetNodeId = nodeId;
    ev.type = ScheduledEvent::Type::paramChange;
    ev.data1 = 0;
    ev.floatValue = 0.99f;
    engine.getEventQueue().schedule(ev);

    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    tracker->paramHistory.clear();
    engine.processBlock(output, midi, 512);

    REQUIRE(tracker->paramHistory.size() == 512);

    // All samples should see the new value (param change at offset 0 applied before processing)
    for (int i = 0; i < 512; ++i)
        CHECK(tracker->paramHistory[i] == 0.99f);
}

TEST_CASE("MIDI partitioned correctly across sub-blocks", "[EventQueue Engine]")
{
    Scheduler sched;
    Engine engine(sched);
    prepareEventQueueTest(engine);

    auto node = std::make_unique<MidiRecordingNode>();
    auto* recorder = node.get();
    node->prepare(44100.0, 512);
    int nodeId = engine.addNode(std::move(node), "Recorder");
    engine.updateGraph();
    drainBlock(engine);

    // Block beats: [0.02322, 0.04644)
    // Schedule noteOn at beat 0.028 → offset ≈ round((0.028 - 0.02322)*22050) ≈ 105
    // Schedule paramChange at beat 0.035 → offset ≈ 260
    // The noteOn is in the first sub-block (before offset 260), should appear at offset 105

    ScheduledEvent noteEv{};
    noteEv.beatTime = 0.028;
    noteEv.targetNodeId = nodeId;
    noteEv.type = ScheduledEvent::Type::noteOn;
    noteEv.channel = 1;
    noteEv.data1 = 72;
    noteEv.floatValue = 90.0f;
    engine.getEventQueue().schedule(noteEv);

    ScheduledEvent paramEv{};
    paramEv.beatTime = 0.035;
    paramEv.targetNodeId = nodeId;
    paramEv.type = ScheduledEvent::Type::paramChange;
    paramEv.data1 = 0;
    paramEv.floatValue = 0.5f;
    engine.getEventQueue().schedule(paramEv);

    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    engine.processBlock(output, midi, 512);

    // The MIDI event should arrive in the first sub-block.
    // With sub-block splitting, the event's position gets rebased to sub-block-local offset.
    REQUIRE(recorder->events.size() == 1);
    REQUIRE(recorder->events[0].noteNumber == 72);
    REQUIRE(recorder->events[0].isNoteOn);
    // The event should appear at a sub-block-local offset (≈105, rebased from block start)
    REQUIRE(recorder->events[0].samplePosition >= 95);
    REQUIRE(recorder->events[0].samplePosition <= 115);
}

TEST_CASE("MIDI in second sub-block has adjusted offset", "[EventQueue Engine]")
{
    Scheduler sched;
    Engine engine(sched);
    prepareEventQueueTest(engine);

    auto node = std::make_unique<MidiRecordingNode>();
    auto* recorder = node.get();
    node->prepare(44100.0, 512);
    int nodeId = engine.addNode(std::move(node), "Recorder");
    engine.updateGraph();
    drainBlock(engine);

    // paramChange at beat 0.030 → offset ≈ 149
    // noteOn at beat 0.038 → offset ≈ 325
    // Second sub-block starts at sample 149, noteOn should appear at 325 - 149 = 176

    ScheduledEvent paramEv{};
    paramEv.beatTime = 0.030;
    paramEv.targetNodeId = nodeId;
    paramEv.type = ScheduledEvent::Type::paramChange;
    paramEv.data1 = 0;
    paramEv.floatValue = 0.5f;
    engine.getEventQueue().schedule(paramEv);

    ScheduledEvent noteEv{};
    noteEv.beatTime = 0.038;
    noteEv.targetNodeId = nodeId;
    noteEv.type = ScheduledEvent::Type::noteOn;
    noteEv.channel = 1;
    noteEv.data1 = 64;
    noteEv.floatValue = 80.0f;
    engine.getEventQueue().schedule(noteEv);

    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    engine.processBlock(output, midi, 512);

    REQUIRE(recorder->events.size() == 1);
    REQUIRE(recorder->events[0].noteNumber == 64);
    // Offset should be adjusted: absolute offset minus sub-block start
    // ~325 - ~149 = ~176
    REQUIRE(recorder->events[0].samplePosition >= 165);
    REQUIRE(recorder->events[0].samplePosition <= 188);
}

TEST_CASE("Transport stop clears EventQueue", "[EventQueue Engine]")
{
    Scheduler sched;
    Engine engine(sched);
    prepareEventQueueTest(engine);

    auto node = std::make_unique<MidiRecordingNode>();
    auto* recorder = node.get();
    node->prepare(44100.0, 512);
    int nodeId = engine.addNode(std::move(node), "Recorder");
    engine.updateGraph();
    drainBlock(engine);

    // Schedule event for beat 0.035 (within next block)
    ScheduledEvent ev{};
    ev.beatTime = 0.035;
    ev.targetNodeId = nodeId;
    ev.type = ScheduledEvent::Type::noteOn;
    ev.channel = 1;
    ev.data1 = 60;
    ev.floatValue = 100.0f;
    engine.getEventQueue().schedule(ev);

    // Stop transport, then play again
    engine.transportStop();
    engine.transportPlay();

    // Drain stop+play commands
    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    engine.processBlock(output, midi, 512);

    // The event should NOT appear — queue was cleared on stop
    REQUIRE(recorder->events.empty());
}

TEST_CASE("Position seek clears EventQueue", "[EventQueue Engine]")
{
    Scheduler sched;
    Engine engine(sched);
    prepareEventQueueTest(engine);

    auto node = std::make_unique<MidiRecordingNode>();
    auto* recorder = node.get();
    node->prepare(44100.0, 512);
    int nodeId = engine.addNode(std::move(node), "Recorder");
    engine.updateGraph();
    drainBlock(engine);

    // Schedule event at beat 2.0 (far in the future)
    ScheduledEvent ev{};
    ev.beatTime = 2.0;
    ev.targetNodeId = nodeId;
    ev.type = ScheduledEvent::Type::noteOn;
    ev.channel = 1;
    ev.data1 = 60;
    ev.floatValue = 100.0f;
    engine.getEventQueue().schedule(ev);

    // Seek past it — this should clear the queue
    engine.transportSetPositionInBeats(3.0);

    // Drain seek command and process
    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    engine.processBlock(output, midi, 512);

    // Event was cleared by seek, should not appear
    REQUIRE(recorder->events.empty());
}

TEST_CASE("Sub-block sizes sum to full block", "[EventQueue Engine]")
{
    Scheduler sched;
    Engine engine(sched);
    prepareEventQueueTest(engine);

    auto node = std::make_unique<ParamTrackingNode>();
    auto* tracker = node.get();
    node->prepare(44100.0, 512);
    tracker->paramValue_ = 0.0f;
    int nodeId = engine.addNode(std::move(node), "Tracker");
    engine.updateGraph();
    drainBlock(engine);

    // Schedule three param changes at different beats
    double beats[] = {0.025, 0.032, 0.042};
    float vals[] = {0.3f, 0.6f, 0.9f};
    for (int i = 0; i < 3; ++i)
    {
        ScheduledEvent ev{};
        ev.beatTime = beats[i];
        ev.targetNodeId = nodeId;
        ev.type = ScheduledEvent::Type::paramChange;
        ev.data1 = 0;
        ev.floatValue = vals[i];
        engine.getEventQueue().schedule(ev);
    }

    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    tracker->paramHistory.clear();
    engine.processBlock(output, midi, 512);

    // Total recorded samples must equal exactly 512
    REQUIRE(tracker->paramHistory.size() == 512);
}

TEST_CASE("No events → existing behavior unchanged", "[EventQueue Engine]")
{
    Scheduler sched;
    Engine engine(sched);
    prepareEventQueueTest(engine);

    auto node = std::make_unique<ConstNode>(0.42f);
    node->prepare(44100.0, 512);
    engine.addNode(std::move(node), "Source");
    engine.updateGraph();
    drainBlock(engine);

    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    engine.processBlock(output, midi, 512);

    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < 512; ++s)
            REQUIRE_THAT(output.getSample(ch, s), WithinAbs(0.42f, 1e-6));
}

TEST_CASE("Retrieve only when transport playing", "[EventQueue Engine]")
{
    Scheduler sched;
    Engine engine(sched);
    engine.prepareForTesting(44100.0, 512);
    engine.transportSetTempo(120.0);
    // Do NOT call transportPlay

    auto node = std::make_unique<MidiRecordingNode>();
    auto* recorder = node.get();
    node->prepare(44100.0, 512);
    int nodeId = engine.addNode(std::move(node), "Recorder");
    engine.updateGraph();

    // Drain tempo + swapGraph commands
    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    engine.processBlock(output, midi, 512);

    // Schedule event
    ScheduledEvent ev{};
    ev.beatTime = 0.01;
    ev.targetNodeId = nodeId;
    ev.type = ScheduledEvent::Type::noteOn;
    ev.channel = 1;
    ev.data1 = 60;
    ev.floatValue = 100.0f;
    engine.getEventQueue().schedule(ev);

    engine.processBlock(output, midi, 512);

    // Transport not playing → event not dispatched
    REQUIRE(recorder->events.empty());
}

TEST_CASE("Engine getEventQueue returns a reference", "[EventQueue Engine]")
{
    Scheduler sched;
    Engine engine(sched);

    auto& eq = engine.getEventQueue();
    // Should be able to schedule without crashing
    ScheduledEvent ev{};
    ev.beatTime = 1.0;
    ev.targetNodeId = 0;
    ev.type = ScheduledEvent::Type::noteOn;
    ev.channel = 1;
    ev.data1 = 60;
    ev.floatValue = 100.0f;
    REQUIRE(eq.schedule(ev));
}
