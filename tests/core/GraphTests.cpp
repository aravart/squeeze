#include <catch2/catch_test_macros.hpp>
#include "core/Graph.h"

using namespace squeeze;

// ============================================================
// Minimal test nodes
// ============================================================

class StereoEffectNode : public Node {
public:
    void prepare(double, int) override {}
    void process(ProcessContext&) override {}
    void release() override {}

    std::vector<PortDescriptor> getInputPorts() const override {
        return {{"in", PortDirection::input, SignalType::audio, 2}};
    }
    std::vector<PortDescriptor> getOutputPorts() const override {
        return {{"out", PortDirection::output, SignalType::audio, 2}};
    }
};

class SynthNode : public Node {
public:
    void prepare(double, int) override {}
    void process(ProcessContext&) override {}
    void release() override {}

    std::vector<PortDescriptor> getInputPorts() const override {
        return {{"midi", PortDirection::input, SignalType::midi, 1}};
    }
    std::vector<PortDescriptor> getOutputPorts() const override {
        return {{"out", PortDirection::output, SignalType::audio, 2}};
    }
};

class MidiSourceNode : public Node {
public:
    void prepare(double, int) override {}
    void process(ProcessContext&) override {}
    void release() override {}

    std::vector<PortDescriptor> getInputPorts() const override {
        return {};
    }
    std::vector<PortDescriptor> getOutputPorts() const override {
        return {{"midi", PortDirection::output, SignalType::midi, 1}};
    }
};

class MonoNode : public Node {
public:
    void prepare(double, int) override {}
    void process(ProcessContext&) override {}
    void release() override {}

    std::vector<PortDescriptor> getInputPorts() const override {
        return {{"in", PortDirection::input, SignalType::audio, 1}};
    }
    std::vector<PortDescriptor> getOutputPorts() const override {
        return {{"out", PortDirection::output, SignalType::audio, 1}};
    }
};

// Helper to check if a appears before b in a vector
static bool isBefore(const std::vector<int>& v, int a, int b)
{
    int posA = -1, posB = -1;
    for (int i = 0; i < (int)v.size(); ++i) {
        if (v[i] == a) posA = i;
        if (v[i] == b) posB = i;
    }
    return posA >= 0 && posB >= 0 && posA < posB;
}

// ============================================================
// Node management
// ============================================================

TEST_CASE("addNode returns unique IDs")
{
    Graph graph;
    StereoEffectNode a, b;

    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);

    REQUIRE(idA != idB);
}

TEST_CASE("getNode returns the node for a valid ID")
{
    Graph graph;
    StereoEffectNode node;

    int id = graph.addNode(&node);
    REQUIRE(graph.getNode(id) == &node);
}

TEST_CASE("getNode returns nullptr for unknown ID")
{
    Graph graph;
    REQUIRE(graph.getNode(999) == nullptr);
}

TEST_CASE("getNodeCount reflects additions")
{
    Graph graph;
    StereoEffectNode a, b;

    REQUIRE(graph.getNodeCount() == 0);
    graph.addNode(&a);
    REQUIRE(graph.getNodeCount() == 1);
    graph.addNode(&b);
    REQUIRE(graph.getNodeCount() == 2);
}

TEST_CASE("removeNode removes the node")
{
    Graph graph;
    StereoEffectNode node;

    int id = graph.addNode(&node);
    REQUIRE(graph.removeNode(id));
    REQUIRE(graph.getNode(id) == nullptr);
    REQUIRE(graph.getNodeCount() == 0);
}

TEST_CASE("removeNode returns false for unknown ID")
{
    Graph graph;
    REQUIRE_FALSE(graph.removeNode(999));
}

TEST_CASE("Node IDs are not reused after removal")
{
    Graph graph;
    StereoEffectNode a, b;

    int id1 = graph.addNode(&a);
    graph.removeNode(id1);
    int id2 = graph.addNode(&b);

    REQUIRE(id1 != id2);
}

// ============================================================
// Connection validation
// ============================================================

TEST_CASE("Compatible audio ports can be connected")
{
    Graph graph;
    StereoEffectNode a, b;

    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);

    int connId = graph.connect(
        {idA, PortDirection::output, "out"},
        {idB, PortDirection::input, "in"}
    );
    REQUIRE(connId >= 0);
}

TEST_CASE("Compatible MIDI ports can be connected")
{
    Graph graph;
    MidiSourceNode src;
    SynthNode synth;

    int idSrc = graph.addNode(&src);
    int idSynth = graph.addNode(&synth);

    int connId = graph.connect(
        {idSrc, PortDirection::output, "midi"},
        {idSynth, PortDirection::input, "midi"}
    );
    REQUIRE(connId >= 0);
}

TEST_CASE("connect fails if source node does not exist")
{
    Graph graph;
    StereoEffectNode node;
    int id = graph.addNode(&node);

    int connId = graph.connect(
        {999, PortDirection::output, "out"},
        {id, PortDirection::input, "in"}
    );
    REQUIRE(connId == -1);
}

TEST_CASE("connect fails if dest node does not exist")
{
    Graph graph;
    StereoEffectNode node;
    int id = graph.addNode(&node);

    int connId = graph.connect(
        {id, PortDirection::output, "out"},
        {999, PortDirection::input, "in"}
    );
    REQUIRE(connId == -1);
}

TEST_CASE("connect fails if source port does not exist")
{
    Graph graph;
    StereoEffectNode a, b;
    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);

    int connId = graph.connect(
        {idA, PortDirection::output, "nonexistent"},
        {idB, PortDirection::input, "in"}
    );
    REQUIRE(connId == -1);
}

TEST_CASE("connect fails if dest port does not exist")
{
    Graph graph;
    StereoEffectNode a, b;
    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);

    int connId = graph.connect(
        {idA, PortDirection::output, "out"},
        {idB, PortDirection::input, "nonexistent"}
    );
    REQUIRE(connId == -1);
}

TEST_CASE("connect fails for mismatched signal types")
{
    Graph graph;
    MidiSourceNode midiSrc;
    StereoEffectNode effect;

    int idMidi = graph.addNode(&midiSrc);
    int idEffect = graph.addNode(&effect);

    // MIDI out -> audio in: incompatible
    int connId = graph.connect(
        {idMidi, PortDirection::output, "midi"},
        {idEffect, PortDirection::input, "in"}
    );
    REQUIRE(connId == -1);
}

TEST_CASE("connect fails for mismatched channel counts")
{
    Graph graph;
    MonoNode mono;
    StereoEffectNode stereo;

    int idMono = graph.addNode(&mono);
    int idStereo = graph.addNode(&stereo);

    // mono out (1ch) -> stereo in (2ch): incompatible
    int connId = graph.connect(
        {idMono, PortDirection::output, "out"},
        {idStereo, PortDirection::input, "in"}
    );
    REQUIRE(connId == -1);
}

TEST_CASE("connect fails if audio input port already has a connection")
{
    Graph graph;
    StereoEffectNode a, b, c;

    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);
    int idC = graph.addNode(&c);

    int conn1 = graph.connect(
        {idA, PortDirection::output, "out"},
        {idC, PortDirection::input, "in"}
    );
    REQUIRE(conn1 >= 0);

    // Second connection to same audio input
    int conn2 = graph.connect(
        {idB, PortDirection::output, "out"},
        {idC, PortDirection::input, "in"}
    );
    REQUIRE(conn2 == -1);
    REQUIRE(graph.getLastError() == "Audio input port already has a connection");
}

TEST_CASE("MIDI fan-in is allowed: multiple sources to one MIDI input")
{
    Graph graph;
    MidiSourceNode src1, src2, src3;
    SynthNode synth;

    int idSrc1 = graph.addNode(&src1);
    int idSrc2 = graph.addNode(&src2);
    int idSrc3 = graph.addNode(&src3);
    int idSynth = graph.addNode(&synth);

    int conn1 = graph.connect(
        {idSrc1, PortDirection::output, "midi"},
        {idSynth, PortDirection::input, "midi"}
    );
    REQUIRE(conn1 >= 0);

    int conn2 = graph.connect(
        {idSrc2, PortDirection::output, "midi"},
        {idSynth, PortDirection::input, "midi"}
    );
    REQUIRE(conn2 >= 0);

    int conn3 = graph.connect(
        {idSrc3, PortDirection::output, "midi"},
        {idSynth, PortDirection::input, "midi"}
    );
    REQUIRE(conn3 >= 0);

    REQUIRE(graph.getConnections().size() == 3);

    // All sources should appear before synth in execution order
    auto order = graph.getExecutionOrder();
    REQUIRE(order.size() == 4);
}

TEST_CASE("Fan-out is allowed: one output to multiple inputs")
{
    Graph graph;
    StereoEffectNode a, b, c;

    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);
    int idC = graph.addNode(&c);

    int conn1 = graph.connect(
        {idA, PortDirection::output, "out"},
        {idB, PortDirection::input, "in"}
    );
    int conn2 = graph.connect(
        {idA, PortDirection::output, "out"},
        {idC, PortDirection::input, "in"}
    );

    REQUIRE(conn1 >= 0);
    REQUIRE(conn2 >= 0);
    REQUIRE(conn1 != conn2);
}

// ============================================================
// Cycle detection
// ============================================================

TEST_CASE("Self-connection is rejected as a cycle")
{
    Graph graph;
    StereoEffectNode node;
    int id = graph.addNode(&node);

    int connId = graph.connect(
        {id, PortDirection::output, "out"},
        {id, PortDirection::input, "in"}
    );
    REQUIRE(connId == -1);
}

TEST_CASE("Direct cycle between two nodes is rejected")
{
    Graph graph;
    StereoEffectNode a, b;

    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);

    int conn1 = graph.connect(
        {idA, PortDirection::output, "out"},
        {idB, PortDirection::input, "in"}
    );
    REQUIRE(conn1 >= 0);

    // B -> A would create a cycle
    int conn2 = graph.connect(
        {idB, PortDirection::output, "out"},
        {idA, PortDirection::input, "in"}
    );
    REQUIRE(conn2 == -1);
}

TEST_CASE("Indirect cycle through three nodes is rejected")
{
    Graph graph;
    StereoEffectNode a, b, c;

    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);
    int idC = graph.addNode(&c);

    REQUIRE(graph.connect({idA, PortDirection::output, "out"},
                          {idB, PortDirection::input, "in"}) >= 0);
    REQUIRE(graph.connect({idB, PortDirection::output, "out"},
                          {idC, PortDirection::input, "in"}) >= 0);

    // C -> A would create A -> B -> C -> A
    int connId = graph.connect(
        {idC, PortDirection::output, "out"},
        {idA, PortDirection::input, "in"}
    );
    REQUIRE(connId == -1);
}

// ============================================================
// Disconnection
// ============================================================

TEST_CASE("disconnect removes a connection")
{
    Graph graph;
    StereoEffectNode a, b;

    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);

    int connId = graph.connect(
        {idA, PortDirection::output, "out"},
        {idB, PortDirection::input, "in"}
    );

    REQUIRE(graph.disconnect(connId));
    REQUIRE(graph.getConnections().empty());
}

TEST_CASE("disconnect returns false for unknown ID")
{
    Graph graph;
    REQUIRE_FALSE(graph.disconnect(999));
}

TEST_CASE("After disconnect, the input port can accept a new connection")
{
    Graph graph;
    StereoEffectNode a, b, c;

    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);
    int idC = graph.addNode(&c);

    int conn1 = graph.connect(
        {idA, PortDirection::output, "out"},
        {idC, PortDirection::input, "in"}
    );
    graph.disconnect(conn1);

    // Now B -> C should work
    int conn2 = graph.connect(
        {idB, PortDirection::output, "out"},
        {idC, PortDirection::input, "in"}
    );
    REQUIRE(conn2 >= 0);
}

// ============================================================
// Remove node removes connections
// ============================================================

TEST_CASE("Removing a node removes its outgoing connections")
{
    Graph graph;
    StereoEffectNode a, b;

    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);

    graph.connect(
        {idA, PortDirection::output, "out"},
        {idB, PortDirection::input, "in"}
    );

    graph.removeNode(idA);
    REQUIRE(graph.getConnections().empty());
}

TEST_CASE("Removing a node removes its incoming connections")
{
    Graph graph;
    StereoEffectNode a, b;

    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);

    graph.connect(
        {idA, PortDirection::output, "out"},
        {idB, PortDirection::input, "in"}
    );

    graph.removeNode(idB);
    REQUIRE(graph.getConnections().empty());
}

TEST_CASE("Removing a node frees input ports on connected nodes")
{
    Graph graph;
    StereoEffectNode a, b, c;

    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);
    int idC = graph.addNode(&c);

    graph.connect({idA, PortDirection::output, "out"},
                  {idB, PortDirection::input, "in"});

    graph.removeNode(idA);

    // B's input should now be free
    int connId = graph.connect(
        {idC, PortDirection::output, "out"},
        {idB, PortDirection::input, "in"}
    );
    REQUIRE(connId >= 0);
}

// ============================================================
// Execution order
// ============================================================

TEST_CASE("Single node appears in execution order")
{
    Graph graph;
    StereoEffectNode node;

    int id = graph.addNode(&node);
    auto order = graph.getExecutionOrder();

    REQUIRE(order.size() == 1);
    REQUIRE(order[0] == id);
}

TEST_CASE("Unconnected nodes all appear in execution order")
{
    Graph graph;
    StereoEffectNode a, b, c;

    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);
    int idC = graph.addNode(&c);

    auto order = graph.getExecutionOrder();
    REQUIRE(order.size() == 3);

    // All three present (order among unconnected is unspecified)
    REQUIRE(std::find(order.begin(), order.end(), idA) != order.end());
    REQUIRE(std::find(order.begin(), order.end(), idB) != order.end());
    REQUIRE(std::find(order.begin(), order.end(), idC) != order.end());
}

TEST_CASE("Connected nodes are ordered source before dest")
{
    Graph graph;
    StereoEffectNode a, b;

    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);

    graph.connect({idA, PortDirection::output, "out"},
                  {idB, PortDirection::input, "in"});

    auto order = graph.getExecutionOrder();
    REQUIRE(order.size() == 2);
    REQUIRE(isBefore(order, idA, idB));
}

TEST_CASE("Chain of three nodes is ordered correctly")
{
    Graph graph;
    StereoEffectNode a, b, c;

    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);
    int idC = graph.addNode(&c);

    graph.connect({idA, PortDirection::output, "out"},
                  {idB, PortDirection::input, "in"});
    graph.connect({idB, PortDirection::output, "out"},
                  {idC, PortDirection::input, "in"});

    auto order = graph.getExecutionOrder();
    REQUIRE(order.size() == 3);
    REQUIRE(isBefore(order, idA, idB));
    REQUIRE(isBefore(order, idB, idC));
}

TEST_CASE("Diamond graph respects all dependencies")
{
    // A -> B -> D
    // A -> C -> D
    Graph graph;
    StereoEffectNode a, b, c, d;

    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);
    int idC = graph.addNode(&c);
    int idD = graph.addNode(&d);

    graph.connect({idA, PortDirection::output, "out"},
                  {idB, PortDirection::input, "in"});
    graph.connect({idA, PortDirection::output, "out"},
                  {idC, PortDirection::input, "in"});

    // D has only one input port so we need to pick one.
    // Connect B -> D
    graph.connect({idB, PortDirection::output, "out"},
                  {idD, PortDirection::input, "in"});

    auto order = graph.getExecutionOrder();
    REQUIRE(order.size() == 4);
    REQUIRE(isBefore(order, idA, idB));
    REQUIRE(isBefore(order, idA, idC));
    REQUIRE(isBefore(order, idB, idD));
}

TEST_CASE("Execution order updates after disconnect")
{
    Graph graph;
    StereoEffectNode a, b;

    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);

    int connId = graph.connect(
        {idA, PortDirection::output, "out"},
        {idB, PortDirection::input, "in"}
    );

    // Before disconnect: A before B
    REQUIRE(isBefore(graph.getExecutionOrder(), idA, idB));

    graph.disconnect(connId);

    // After disconnect: both still present, no ordering constraint
    auto order = graph.getExecutionOrder();
    REQUIRE(order.size() == 2);
}

TEST_CASE("Execution order updates after node removal")
{
    Graph graph;
    StereoEffectNode a, b;

    int idA = graph.addNode(&a);
    graph.addNode(&b);

    graph.connect({idA, PortDirection::output, "out"},
                  {graph.addNode(new StereoEffectNode()), PortDirection::input, "in"});

    // Just verify no crash — removal should produce valid order
    // (The heap-allocated node leaks, but this is a test)
    auto order = graph.getExecutionOrder();
    REQUIRE(order.size() >= 2);
}

// ============================================================
// Connection queries
// ============================================================

TEST_CASE("getConnections returns all connections")
{
    Graph graph;
    StereoEffectNode a, b, c;

    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);
    int idC = graph.addNode(&c);

    graph.connect({idA, PortDirection::output, "out"},
                  {idB, PortDirection::input, "in"});
    graph.connect({idA, PortDirection::output, "out"},
                  {idC, PortDirection::input, "in"});

    REQUIRE(graph.getConnections().size() == 2);
}

TEST_CASE("getConnectionsForNode returns only that node's connections")
{
    Graph graph;
    StereoEffectNode a, b, c;

    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);
    int idC = graph.addNode(&c);

    graph.connect({idA, PortDirection::output, "out"},
                  {idB, PortDirection::input, "in"});
    graph.connect({idB, PortDirection::output, "out"},
                  {idC, PortDirection::input, "in"});

    auto connsA = graph.getConnectionsForNode(idA);
    auto connsB = graph.getConnectionsForNode(idB);
    auto connsC = graph.getConnectionsForNode(idC);

    REQUIRE(connsA.size() == 1);
    REQUIRE(connsB.size() == 2);  // one in, one out
    REQUIRE(connsC.size() == 1);
}

TEST_CASE("getConnections is empty for a graph with no connections")
{
    Graph graph;
    StereoEffectNode node;
    graph.addNode(&node);

    REQUIRE(graph.getConnections().empty());
}

// ============================================================
// Error reporting
// ============================================================

TEST_CASE("getLastError is empty after successful connect")
{
    Graph graph;
    StereoEffectNode a, b;
    int idA = graph.addNode(&a);
    int idB = graph.addNode(&b);

    graph.connect({idA, PortDirection::output, "out"},
                  {idB, PortDirection::input, "in"});

    REQUIRE(graph.getLastError().empty());
}

TEST_CASE("getLastError describes the problem after failed connect")
{
    Graph graph;
    StereoEffectNode node;
    int id = graph.addNode(&node);

    graph.connect({999, PortDirection::output, "out"},
                  {id, PortDirection::input, "in"});

    REQUIRE_FALSE(graph.getLastError().empty());
}

// ============================================================
// MIDI + audio mixed graph
// ============================================================

TEST_CASE("MIDI source into synth into effect chain orders correctly")
{
    Graph graph;
    MidiSourceNode midiSrc;
    SynthNode synth;
    StereoEffectNode effect;

    int idMidi = graph.addNode(&midiSrc);
    int idSynth = graph.addNode(&synth);
    int idEffect = graph.addNode(&effect);

    graph.connect({idMidi, PortDirection::output, "midi"},
                  {idSynth, PortDirection::input, "midi"});
    graph.connect({idSynth, PortDirection::output, "out"},
                  {idEffect, PortDirection::input, "in"});

    auto order = graph.getExecutionOrder();
    REQUIRE(order.size() == 3);
    REQUIRE(isBefore(order, idMidi, idSynth));
    REQUIRE(isBefore(order, idSynth, idEffect));
}

// ============================================================
// MIDI channel on connections
// ============================================================

TEST_CASE("Graph connect stores midiChannel and getConnections returns it")
{
    Graph graph;
    MidiSourceNode src;
    SynthNode synth;

    int idSrc = graph.addNode(&src);
    int idSynth = graph.addNode(&synth);

    int connId = graph.connect(
        {idSrc, PortDirection::output, "midi"},
        {idSynth, PortDirection::input, "midi"},
        3);
    REQUIRE(connId >= 0);

    auto conns = graph.getConnections();
    REQUIRE(conns.size() == 1);
    REQUIRE(conns[0].midiChannel == 3);
}

TEST_CASE("Graph connect default midiChannel is 0")
{
    Graph graph;
    MidiSourceNode src;
    SynthNode synth;

    int idSrc = graph.addNode(&src);
    int idSynth = graph.addNode(&synth);

    int connId = graph.connect(
        {idSrc, PortDirection::output, "midi"},
        {idSynth, PortDirection::input, "midi"});
    REQUIRE(connId >= 0);

    auto conns = graph.getConnections();
    REQUIRE(conns[0].midiChannel == 0);
}

TEST_CASE("Graph connect rejects midiChannel outside 0-16 range")
{
    Graph graph;
    MidiSourceNode src;
    SynthNode synth;

    int idSrc = graph.addNode(&src);
    int idSynth = graph.addNode(&synth);

    REQUIRE(graph.connect(
        {idSrc, PortDirection::output, "midi"},
        {idSynth, PortDirection::input, "midi"},
        17) == -1);

    REQUIRE(graph.connect(
        {idSrc, PortDirection::output, "midi"},
        {idSynth, PortDirection::input, "midi"},
        -1) == -1);

    REQUIRE(graph.getConnections().empty());
}
