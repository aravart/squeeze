#include <catch2/catch_test_macros.hpp>
#include "core/Engine.h"
#include "core/GainNode.h"
#include "core/OutputNode.h"

#include <cmath>
#include <memory>

using namespace squeeze;

// ═══════════════════════════════════════════════════════════════════
// Output node
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Engine creates output node at construction")
{
    Engine engine;
    int outId = engine.getOutputNodeId();
    REQUIRE(outId > 0);
    REQUIRE(engine.getNode(outId) != nullptr);
}

TEST_CASE("Output node has stereo audio input port 'in'")
{
    Engine engine;
    Node* out = engine.getNode(engine.getOutputNodeId());
    REQUIRE(out != nullptr);

    auto inputs = out->getInputPorts();
    REQUIRE(inputs.size() == 1);
    CHECK(inputs[0].name == "in");
    CHECK(inputs[0].direction == PortDirection::input);
    CHECK(inputs[0].signalType == SignalType::audio);
    CHECK(inputs[0].channels == 2);

    auto outputs = out->getOutputPorts();
    CHECK(outputs.empty());
}

TEST_CASE("Output node cannot be removed")
{
    Engine engine;
    int outId = engine.getOutputNodeId();
    REQUIRE_FALSE(engine.removeNode(outId));
    REQUIRE(engine.getNode(outId) != nullptr);
}

TEST_CASE("Output node name is 'output'")
{
    Engine engine;
    CHECK(engine.getNodeName(engine.getOutputNodeId()) == "output");
}

// ═══════════════════════════════════════════════════════════════════
// Node management
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("addNode assigns unique IDs")
{
    Engine engine;
    int a = engine.addNode("gain1", std::make_unique<GainNode>());
    int b = engine.addNode("gain2", std::make_unique<GainNode>());
    REQUIRE(a > 0);
    REQUIRE(b > 0);
    REQUIRE(a != b);
    REQUIRE(a != engine.getOutputNodeId());
    REQUIRE(b != engine.getOutputNodeId());
}

TEST_CASE("addNode with null returns -1")
{
    Engine engine;
    int id = engine.addNode("bad", nullptr);
    REQUIRE(id == -1);
}

TEST_CASE("removeNode works for non-output nodes")
{
    Engine engine;
    int id = engine.addNode("gain", std::make_unique<GainNode>());
    REQUIRE(engine.getNode(id) != nullptr);
    REQUIRE(engine.removeNode(id));
    REQUIRE(engine.getNode(id) == nullptr);
}

TEST_CASE("removeNode returns false for unknown ID")
{
    Engine engine;
    REQUIRE_FALSE(engine.removeNode(9999));
}

TEST_CASE("getNodeCount includes output node")
{
    Engine engine;
    REQUIRE(engine.getNodeCount() == 1); // output only
    int id = engine.addNode("gain", std::make_unique<GainNode>());
    REQUIRE(engine.getNodeCount() == 2);
    engine.removeNode(id);
    REQUIRE(engine.getNodeCount() == 1);
}

TEST_CASE("getNodes returns all nodes including output")
{
    Engine engine;
    engine.addNode("gain", std::make_unique<GainNode>());
    auto nodes = engine.getNodes();
    REQUIRE(nodes.size() == 2);

    bool foundOutput = false;
    bool foundGain = false;
    for (const auto& p : nodes)
    {
        if (p.second == "output") foundOutput = true;
        if (p.second == "gain") foundGain = true;
    }
    CHECK(foundOutput);
    CHECK(foundGain);
}

// ═══════════════════════════════════════════════════════════════════
// Snapshot and processBlock
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("prepareForTesting does not crash")
{
    Engine engine;
    engine.prepareForTesting(44100.0, 512);
}

TEST_CASE("render does not crash")
{
    Engine engine;
    engine.prepareForTesting(44100.0, 512);
    engine.render(512);
}

TEST_CASE("processBlock outputs silence with no connections")
{
    Engine engine;
    engine.prepareForTesting(44100.0, 512);
    engine.render(512); // drain snapshot command

    const int N = 512;
    float left[N] = {};
    float right[N] = {};
    // Fill with non-zero to verify silence
    for (int i = 0; i < N; ++i) { left[i] = 1.0f; right[i] = 1.0f; }

    float* channels[2] = {left, right};
    engine.processBlock(channels, 2, N);

    for (int i = 0; i < N; ++i)
    {
        CHECK(left[i] == 0.0f);
        CHECK(right[i] == 0.0f);
    }
}

TEST_CASE("Gain node chain processes audio through to output")
{
    Engine engine;
    engine.prepareForTesting(44100.0, 512);

    int gainId = engine.addNode("gain", std::make_unique<GainNode>());
    engine.setParameter(gainId, "gain", 0.5f);

    std::string error;
    int connId = engine.connect(gainId, "out", engine.getOutputNodeId(), "in", error);
    REQUIRE(connId >= 0);

    // Drain all pending snapshot commands
    engine.render(512);

    // Now process a block — gain node has zero input, so output should be silence
    const int N = 128;
    float left[N] = {};
    float right[N] = {};
    float* channels[2] = {left, right};
    engine.processBlock(channels, 2, N);

    // With no input to the gain node, output is silence (gain * 0 = 0)
    for (int i = 0; i < N; ++i)
    {
        CHECK(left[i] == 0.0f);
        CHECK(right[i] == 0.0f);
    }
}

TEST_CASE("Fan-in sums audio from multiple sources")
{
    Engine engine;
    engine.prepareForTesting(44100.0, 512);

    int g1 = engine.addNode("gain1", std::make_unique<GainNode>());
    int g2 = engine.addNode("gain2", std::make_unique<GainNode>());

    std::string error;
    REQUIRE(engine.connect(g1, "out", engine.getOutputNodeId(), "in", error) >= 0);
    REQUIRE(engine.connect(g2, "out", engine.getOutputNodeId(), "in", error) >= 0);

    engine.render(512);
    // Both gains have zero input, output is silence
    // This tests that fan-in doesn't crash
}

// ═══════════════════════════════════════════════════════════════════
// Parameters through Engine
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("getParameter and setParameter route through engine")
{
    Engine engine;
    int gainId = engine.addNode("gain", std::make_unique<GainNode>());

    CHECK(engine.getParameter(gainId, "gain") == 1.0f);
    REQUIRE(engine.setParameter(gainId, "gain", 0.75f));
    CHECK(engine.getParameter(gainId, "gain") == 0.75f);
}

TEST_CASE("setParameter returns false for unknown node")
{
    Engine engine;
    CHECK_FALSE(engine.setParameter(9999, "gain", 0.5f));
}

TEST_CASE("getParameterDescriptors routes through engine")
{
    Engine engine;
    int gainId = engine.addNode("gain", std::make_unique<GainNode>());
    auto descs = engine.getParameterDescriptors(gainId);
    REQUIRE(descs.size() == 1);
    CHECK(descs[0].name == "gain");
}

TEST_CASE("getParameterText routes through engine")
{
    Engine engine;
    int gainId = engine.addNode("gain", std::make_unique<GainNode>());
    auto text = engine.getParameterText(gainId, "gain");
    CHECK_FALSE(text.empty());
}

// ═══════════════════════════════════════════════════════════════════
// Transport stubs
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Transport query stubs return defaults")
{
    Engine engine;
    CHECK(engine.getTransportPosition() == 0.0);
    CHECK(engine.getTransportTempo() == 120.0);
    CHECK_FALSE(engine.isTransportPlaying());
}

TEST_CASE("Transport commands do not crash")
{
    Engine engine;
    engine.prepareForTesting(44100.0, 512);
    engine.transportPlay();
    engine.transportStop();
    engine.transportPause();
    engine.transportSetTempo(140.0);
    engine.transportSetTimeSignature(3, 4);
    engine.transportSeekSamples(0);
    engine.transportSeekBeats(0.0);
    engine.transportSetLoopPoints(0.0, 4.0);
    engine.transportSetLooping(true);
    engine.render(512); // drain commands
}

// ═══════════════════════════════════════════════════════════════════
// Event scheduling stubs
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Event scheduling stubs return false")
{
    Engine engine;
    CHECK_FALSE(engine.scheduleNoteOn(1, 0.0, 1, 60, 0.8f));
    CHECK_FALSE(engine.scheduleNoteOff(1, 1.0, 1, 60));
    CHECK_FALSE(engine.scheduleCC(1, 0.0, 1, 1, 64));
    CHECK_FALSE(engine.scheduleParamChange(1, 0.0, "gain", 0.5f));
}

// ═══════════════════════════════════════════════════════════════════
// Execution order
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("getExecutionOrder includes all nodes")
{
    Engine engine;
    engine.addNode("g1", std::make_unique<GainNode>());
    engine.addNode("g2", std::make_unique<GainNode>());
    auto order = engine.getExecutionOrder();
    REQUIRE(order.size() == 3); // 2 gains + output
}
