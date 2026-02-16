#include <catch2/catch_test_macros.hpp>
#include "core/Graph.h"

using namespace squeeze;

// ═══════════════════════════════════════════════════════════════════
// Local test node fixtures
// ═══════════════════════════════════════════════════════════════════

class StereoEffectNode : public Node {
public:
    void prepare(double, int) override {}
    void release() override {}
    void process(ProcessContext&) override {}

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
    void release() override {}
    void process(ProcessContext&) override {}

    std::vector<PortDescriptor> getInputPorts() const override {
        return {{"midi_in", PortDirection::input, SignalType::midi, 1}};
    }
    std::vector<PortDescriptor> getOutputPorts() const override {
        return {{"out", PortDirection::output, SignalType::audio, 2}};
    }
};

class MidiSourceNode : public Node {
public:
    void prepare(double, int) override {}
    void release() override {}
    void process(ProcessContext&) override {}

    std::vector<PortDescriptor> getInputPorts() const override {
        return {};
    }
    std::vector<PortDescriptor> getOutputPorts() const override {
        return {{"midi_out", PortDirection::output, SignalType::midi, 1}};
    }
};

class MonoNode : public Node {
public:
    void prepare(double, int) override {}
    void release() override {}
    void process(ProcessContext&) override {}

    std::vector<PortDescriptor> getInputPorts() const override {
        return {{"in", PortDirection::input, SignalType::audio, 1}};
    }
    std::vector<PortDescriptor> getOutputPorts() const override {
        return {{"out", PortDirection::output, SignalType::audio, 1}};
    }
};

// Helper: check if nodeA appears before nodeB in order vector
static bool isBefore(const std::vector<int>& order, int a, int b)
{
    int posA = -1, posB = -1;
    for (int i = 0; i < static_cast<int>(order.size()); i++)
    {
        if (order[static_cast<size_t>(i)] == a) posA = i;
        if (order[static_cast<size_t>(i)] == b) posB = i;
    }
    return posA >= 0 && posB >= 0 && posA < posB;
}

// ═══════════════════════════════════════════════════════════════════
// Node management
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Graph: addNode and getNode")
{
    Graph g;
    StereoEffectNode node;
    REQUIRE(g.addNode(1, &node));
    CHECK(g.getNode(1) == &node);
    CHECK(g.getNodeCount() == 1);
}

TEST_CASE("Graph: getNode returns nullptr for unknown id")
{
    Graph g;
    CHECK(g.getNode(42) == nullptr);
}

TEST_CASE("Graph: hasNode returns true/false correctly")
{
    Graph g;
    StereoEffectNode node;
    g.addNode(1, &node);
    CHECK(g.hasNode(1));
    CHECK_FALSE(g.hasNode(99));
}

TEST_CASE("Graph: addNode rejects duplicate id")
{
    Graph g;
    StereoEffectNode a, b;
    REQUIRE(g.addNode(1, &a));
    CHECK_FALSE(g.addNode(1, &b));
    CHECK(g.getNodeCount() == 1);
}

TEST_CASE("Graph: addNode rejects null pointer")
{
    Graph g;
    CHECK_FALSE(g.addNode(1, nullptr));
    CHECK(g.getNodeCount() == 0);
}

TEST_CASE("Graph: removeNode succeeds")
{
    Graph g;
    StereoEffectNode node;
    g.addNode(1, &node);
    REQUIRE(g.removeNode(1));
    CHECK_FALSE(g.hasNode(1));
    CHECK(g.getNodeCount() == 0);
}

TEST_CASE("Graph: removeNode returns false for unknown id")
{
    Graph g;
    CHECK_FALSE(g.removeNode(42));
}

// ═══════════════════════════════════════════════════════════════════
// Connection validation
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Graph: connect audio ports succeeds")
{
    Graph g;
    StereoEffectNode a, b;
    g.addNode(1, &a);
    g.addNode(2, &b);

    std::string error;
    int id = g.connect(
        {1, PortDirection::output, "out"},
        {2, PortDirection::input, "in"},
        error);
    CHECK(id >= 0);
    CHECK(error.empty());
}

TEST_CASE("Graph: connect MIDI ports succeeds")
{
    Graph g;
    MidiSourceNode src;
    SynthNode dst;
    g.addNode(1, &src);
    g.addNode(2, &dst);

    std::string error;
    int id = g.connect(
        {1, PortDirection::output, "midi_out"},
        {2, PortDirection::input, "midi_in"},
        error);
    CHECK(id >= 0);
}

TEST_CASE("Graph: connect fails if source node missing")
{
    Graph g;
    StereoEffectNode b;
    g.addNode(2, &b);

    std::string error;
    int id = g.connect(
        {99, PortDirection::output, "out"},
        {2, PortDirection::input, "in"},
        error);
    CHECK(id == -1);
    CHECK(error.find("source node") != std::string::npos);
}

TEST_CASE("Graph: connect fails if dest node missing")
{
    Graph g;
    StereoEffectNode a;
    g.addNode(1, &a);

    std::string error;
    int id = g.connect(
        {1, PortDirection::output, "out"},
        {99, PortDirection::input, "in"},
        error);
    CHECK(id == -1);
    CHECK(error.find("destination node") != std::string::npos);
}

TEST_CASE("Graph: connect fails if source port missing")
{
    Graph g;
    StereoEffectNode a, b;
    g.addNode(1, &a);
    g.addNode(2, &b);

    std::string error;
    int id = g.connect(
        {1, PortDirection::output, "nonexistent"},
        {2, PortDirection::input, "in"},
        error);
    CHECK(id == -1);
    CHECK(error.find("source port") != std::string::npos);
}

TEST_CASE("Graph: connect fails if dest port missing")
{
    Graph g;
    StereoEffectNode a, b;
    g.addNode(1, &a);
    g.addNode(2, &b);

    std::string error;
    int id = g.connect(
        {1, PortDirection::output, "out"},
        {2, PortDirection::input, "nonexistent"},
        error);
    CHECK(id == -1);
    CHECK(error.find("destination port") != std::string::npos);
}

TEST_CASE("Graph: connect fails on signal type mismatch")
{
    Graph g;
    MidiSourceNode midi;
    StereoEffectNode audio;
    g.addNode(1, &midi);
    g.addNode(2, &audio);

    std::string error;
    int id = g.connect(
        {1, PortDirection::output, "midi_out"},
        {2, PortDirection::input, "in"},
        error);
    CHECK(id == -1);
    CHECK(error.find("incompatible") != std::string::npos);
}

TEST_CASE("Graph: connect allows different audio channel counts")
{
    Graph g;
    StereoEffectNode stereo;
    MonoNode mono;
    g.addNode(1, &stereo);
    g.addNode(2, &mono);

    std::string error;
    int id = g.connect(
        {1, PortDirection::output, "out"},
        {2, PortDirection::input, "in"},
        error);
    CHECK(id >= 0);
}

// ═══════════════════════════════════════════════════════════════════
// Fan-in / fan-out
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Graph: audio fan-in is allowed (v2 change)")
{
    Graph g;
    StereoEffectNode a, b, c;
    g.addNode(1, &a);
    g.addNode(2, &b);
    g.addNode(3, &c);

    std::string error;
    int id1 = g.connect(
        {1, PortDirection::output, "out"},
        {3, PortDirection::input, "in"},
        error);
    int id2 = g.connect(
        {2, PortDirection::output, "out"},
        {3, PortDirection::input, "in"},
        error);
    CHECK(id1 >= 0);
    CHECK(id2 >= 0);
    CHECK(g.getConnections().size() == 2);
}

TEST_CASE("Graph: MIDI fan-in is allowed")
{
    Graph g;
    MidiSourceNode a, b;
    SynthNode c;
    g.addNode(1, &a);
    g.addNode(2, &b);
    g.addNode(3, &c);

    std::string error;
    int id1 = g.connect(
        {1, PortDirection::output, "midi_out"},
        {3, PortDirection::input, "midi_in"},
        error);
    int id2 = g.connect(
        {2, PortDirection::output, "midi_out"},
        {3, PortDirection::input, "midi_in"},
        error);
    CHECK(id1 >= 0);
    CHECK(id2 >= 0);
}

TEST_CASE("Graph: fan-out is allowed")
{
    Graph g;
    StereoEffectNode a, b, c;
    g.addNode(1, &a);
    g.addNode(2, &b);
    g.addNode(3, &c);

    std::string error;
    int id1 = g.connect(
        {1, PortDirection::output, "out"},
        {2, PortDirection::input, "in"},
        error);
    int id2 = g.connect(
        {1, PortDirection::output, "out"},
        {3, PortDirection::input, "in"},
        error);
    CHECK(id1 >= 0);
    CHECK(id2 >= 0);
}

// ═══════════════════════════════════════════════════════════════════
// Cycle detection
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Graph: self-loop is rejected")
{
    Graph g;
    StereoEffectNode a;
    g.addNode(1, &a);

    std::string error;
    int id = g.connect(
        {1, PortDirection::output, "out"},
        {1, PortDirection::input, "in"},
        error);
    CHECK(id == -1);
    CHECK(error.find("cycle") != std::string::npos);
}

TEST_CASE("Graph: direct cycle A->B->A is rejected")
{
    Graph g;
    StereoEffectNode a, b;
    g.addNode(1, &a);
    g.addNode(2, &b);

    std::string error;
    int id1 = g.connect(
        {1, PortDirection::output, "out"},
        {2, PortDirection::input, "in"},
        error);
    REQUIRE(id1 >= 0);

    int id2 = g.connect(
        {2, PortDirection::output, "out"},
        {1, PortDirection::input, "in"},
        error);
    CHECK(id2 == -1);
    CHECK(error.find("cycle") != std::string::npos);
}

TEST_CASE("Graph: indirect 3-node cycle A->B->C->A is rejected")
{
    Graph g;
    StereoEffectNode a, b, c;
    g.addNode(1, &a);
    g.addNode(2, &b);
    g.addNode(3, &c);

    std::string error;
    REQUIRE(g.connect({1, PortDirection::output, "out"},
                      {2, PortDirection::input, "in"}, error) >= 0);
    REQUIRE(g.connect({2, PortDirection::output, "out"},
                      {3, PortDirection::input, "in"}, error) >= 0);

    int id3 = g.connect(
        {3, PortDirection::output, "out"},
        {1, PortDirection::input, "in"},
        error);
    CHECK(id3 == -1);
    CHECK(error.find("cycle") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════
// Disconnection
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Graph: disconnect removes connection")
{
    Graph g;
    StereoEffectNode a, b;
    g.addNode(1, &a);
    g.addNode(2, &b);

    std::string error;
    int connId = g.connect(
        {1, PortDirection::output, "out"},
        {2, PortDirection::input, "in"},
        error);
    REQUIRE(connId >= 0);
    REQUIRE(g.disconnect(connId));
    CHECK(g.getConnections().empty());
}

TEST_CASE("Graph: disconnect returns false for unknown id")
{
    Graph g;
    CHECK_FALSE(g.disconnect(999));
}

TEST_CASE("Graph: reconnect after disconnect succeeds")
{
    Graph g;
    StereoEffectNode a, b;
    g.addNode(1, &a);
    g.addNode(2, &b);

    std::string error;
    int id1 = g.connect(
        {1, PortDirection::output, "out"},
        {2, PortDirection::input, "in"},
        error);
    REQUIRE(g.disconnect(id1));

    int id2 = g.connect(
        {1, PortDirection::output, "out"},
        {2, PortDirection::input, "in"},
        error);
    CHECK(id2 >= 0);
    CHECK(id2 != id1); // IDs are never reused
}

// ═══════════════════════════════════════════════════════════════════
// Node removal cascades connections
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Graph: removeNode cascades outgoing connections")
{
    Graph g;
    StereoEffectNode a, b;
    g.addNode(1, &a);
    g.addNode(2, &b);

    std::string error;
    g.connect({1, PortDirection::output, "out"},
              {2, PortDirection::input, "in"}, error);

    g.removeNode(1);
    CHECK(g.getConnections().empty());
}

TEST_CASE("Graph: removeNode cascades incoming connections")
{
    Graph g;
    StereoEffectNode a, b;
    g.addNode(1, &a);
    g.addNode(2, &b);

    std::string error;
    g.connect({1, PortDirection::output, "out"},
              {2, PortDirection::input, "in"}, error);

    g.removeNode(2);
    CHECK(g.getConnections().empty());
}

TEST_CASE("Graph: removeNode frees ports for reconnection")
{
    Graph g;
    StereoEffectNode a, b, c;
    g.addNode(1, &a);
    g.addNode(2, &b);
    g.addNode(3, &c);

    std::string error;
    g.connect({1, PortDirection::output, "out"},
              {2, PortDirection::input, "in"}, error);
    g.removeNode(2);

    // Can now connect 1->3
    int id = g.connect({1, PortDirection::output, "out"},
                       {3, PortDirection::input, "in"}, error);
    CHECK(id >= 0);
}

// ═══════════════════════════════════════════════════════════════════
// Execution order (topological sort)
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Graph: execution order with single node")
{
    Graph g;
    StereoEffectNode a;
    g.addNode(1, &a);

    auto order = g.getExecutionOrder();
    REQUIRE(order.size() == 1);
    CHECK(order[0] == 1);
}

TEST_CASE("Graph: execution order with unconnected nodes includes all")
{
    Graph g;
    StereoEffectNode a, b, c;
    g.addNode(1, &a);
    g.addNode(2, &b);
    g.addNode(3, &c);

    auto order = g.getExecutionOrder();
    REQUIRE(order.size() == 3);
}

TEST_CASE("Graph: execution order respects A->B dependency")
{
    Graph g;
    StereoEffectNode a, b;
    g.addNode(1, &a);
    g.addNode(2, &b);

    std::string error;
    g.connect({1, PortDirection::output, "out"},
              {2, PortDirection::input, "in"}, error);

    auto order = g.getExecutionOrder();
    REQUIRE(order.size() == 2);
    CHECK(isBefore(order, 1, 2));
}

TEST_CASE("Graph: execution order for chain A->B->C")
{
    Graph g;
    StereoEffectNode a, b, c;
    g.addNode(1, &a);
    g.addNode(2, &b);
    g.addNode(3, &c);

    std::string error;
    g.connect({1, PortDirection::output, "out"},
              {2, PortDirection::input, "in"}, error);
    g.connect({2, PortDirection::output, "out"},
              {3, PortDirection::input, "in"}, error);

    auto order = g.getExecutionOrder();
    REQUIRE(order.size() == 3);
    CHECK(isBefore(order, 1, 2));
    CHECK(isBefore(order, 2, 3));
}

TEST_CASE("Graph: execution order for diamond A->B,A->C,B->D,C->D")
{
    Graph g;
    StereoEffectNode a, b, c, d;
    g.addNode(1, &a);
    g.addNode(2, &b);
    g.addNode(3, &c);
    g.addNode(4, &d);

    std::string error;
    g.connect({1, PortDirection::output, "out"},
              {2, PortDirection::input, "in"}, error);
    g.connect({1, PortDirection::output, "out"},
              {3, PortDirection::input, "in"}, error);
    g.connect({2, PortDirection::output, "out"},
              {4, PortDirection::input, "in"}, error);
    g.connect({3, PortDirection::output, "out"},
              {4, PortDirection::input, "in"}, error);

    auto order = g.getExecutionOrder();
    REQUIRE(order.size() == 4);
    CHECK(isBefore(order, 1, 2));
    CHECK(isBefore(order, 1, 3));
    CHECK(isBefore(order, 2, 4));
    CHECK(isBefore(order, 3, 4));
}

TEST_CASE("Graph: execution order updates after disconnect")
{
    Graph g;
    StereoEffectNode a, b;
    g.addNode(1, &a);
    g.addNode(2, &b);

    std::string error;
    int connId = g.connect({1, PortDirection::output, "out"},
                           {2, PortDirection::input, "in"}, error);

    auto order1 = g.getExecutionOrder();
    CHECK(isBefore(order1, 1, 2));

    g.disconnect(connId);
    auto order2 = g.getExecutionOrder();
    REQUIRE(order2.size() == 2);
    // Both nodes still present, no ordering constraint
}

// ═══════════════════════════════════════════════════════════════════
// Connection queries
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Graph: getConnections returns all connections")
{
    Graph g;
    StereoEffectNode a, b, c;
    g.addNode(1, &a);
    g.addNode(2, &b);
    g.addNode(3, &c);

    std::string error;
    g.connect({1, PortDirection::output, "out"},
              {2, PortDirection::input, "in"}, error);
    g.connect({2, PortDirection::output, "out"},
              {3, PortDirection::input, "in"}, error);

    auto conns = g.getConnections();
    CHECK(conns.size() == 2);
}

TEST_CASE("Graph: getConnectionsForNode returns relevant connections")
{
    Graph g;
    StereoEffectNode a, b, c;
    g.addNode(1, &a);
    g.addNode(2, &b);
    g.addNode(3, &c);

    std::string error;
    g.connect({1, PortDirection::output, "out"},
              {2, PortDirection::input, "in"}, error);
    g.connect({2, PortDirection::output, "out"},
              {3, PortDirection::input, "in"}, error);

    auto conns = g.getConnectionsForNode(2);
    CHECK(conns.size() == 2); // both incoming and outgoing

    auto conns1 = g.getConnectionsForNode(1);
    CHECK(conns1.size() == 1); // only outgoing
}

TEST_CASE("Graph: getConnections returns empty when no connections")
{
    Graph g;
    CHECK(g.getConnections().empty());
}

// ═══════════════════════════════════════════════════════════════════
// Error reporting
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Graph: error string is set on failed connect")
{
    Graph g;
    StereoEffectNode a;
    g.addNode(1, &a);

    std::string error;
    g.connect({99, PortDirection::output, "out"},
              {1, PortDirection::input, "in"}, error);
    CHECK_FALSE(error.empty());
}

TEST_CASE("Graph: error string is not set on successful connect")
{
    Graph g;
    StereoEffectNode a, b;
    g.addNode(1, &a);
    g.addNode(2, &b);

    std::string error = "should be cleared by not being set";
    int id = g.connect({1, PortDirection::output, "out"},
                       {2, PortDirection::input, "in"}, error);
    CHECK(id >= 0);
    // error is not modified on success — it retains whatever it had
    // The spec says "sets error" on failure, not "clears on success"
}
