#include "core/Graph.h"
#include "core/Logger.h"

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace squeeze {

int Graph::addNode(Node* node)
{
    int id = nextNodeId_++;
    nodes_[id] = node;
    SQ_LOG("addNode: id=%d", id);
    return id;
}

bool Graph::removeNode(int nodeId)
{
    SQ_LOG("removeNode: id=%d", nodeId);
    auto it = nodes_.find(nodeId);
    if (it == nodes_.end())
        return false;

    // Remove all connections involving this node
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
            [nodeId](const Connection& c) {
                return c.source.nodeId == nodeId || c.dest.nodeId == nodeId;
            }),
        connections_.end()
    );

    nodes_.erase(it);
    return true;
}

Node* Graph::getNode(int nodeId) const
{
    auto it = nodes_.find(nodeId);
    return (it != nodes_.end()) ? it->second : nullptr;
}

int Graph::getNodeCount() const
{
    return static_cast<int>(nodes_.size());
}

int Graph::connect(PortAddress source, PortAddress dest, int midiChannel)
{
    lastError_.clear();

    // Validate midiChannel range
    if (midiChannel < 0 || midiChannel > 16)
    {
        lastError_ = "MIDI channel must be 0-16 (0 = all channels)";
        return -1;
    }

    // Validate nodes exist
    Node* srcNode = getNode(source.nodeId);
    if (!srcNode) {
        lastError_ = "Source node not found";
        return -1;
    }

    Node* dstNode = getNode(dest.nodeId);
    if (!dstNode) {
        lastError_ = "Destination node not found";
        return -1;
    }

    // Find ports
    auto srcPorts = srcNode->getOutputPorts();
    const PortDescriptor* srcPort = nullptr;
    for (const auto& p : srcPorts) {
        if (p.name == source.portName && p.direction == PortDirection::output) {
            srcPort = &p;
            break;
        }
    }
    if (!srcPort) {
        lastError_ = "Source port '" + source.portName + "' not found";
        return -1;
    }

    auto dstPorts = dstNode->getInputPorts();
    const PortDescriptor* dstPort = nullptr;
    for (const auto& p : dstPorts) {
        if (p.name == dest.portName && p.direction == PortDirection::input) {
            dstPort = &p;
            break;
        }
    }
    if (!dstPort) {
        lastError_ = "Destination port '" + dest.portName + "' not found";
        return -1;
    }

    // Check compatibility
    if (!canConnect(*srcPort, *dstPort)) {
        lastError_ = "Ports are incompatible (signal type or channel mismatch)";
        return -1;
    }

    // Check input port not already connected (audio only — MIDI allows fan-in)
    if (dstPort->signalType == SignalType::audio) {
        for (const auto& c : connections_) {
            if (c.dest == dest) {
                lastError_ = "Audio input port already has a connection";
                return -1;
            }
        }
    }

    // Check for cycles
    if (wouldCreateCycle(source.nodeId, dest.nodeId)) {
        lastError_ = "Connection would create a cycle";
        return -1;
    }

    int id = nextConnectionId_++;
    connections_.push_back({id, source, dest, midiChannel});
    SQ_LOG("connect: %d:%s -> %d:%s (conn=%d, ch=%d)",
           source.nodeId, source.portName.c_str(),
           dest.nodeId, dest.portName.c_str(), id, midiChannel);
    return id;
}

bool Graph::disconnect(int connectionId)
{
    SQ_LOG("disconnect: conn=%d", connectionId);
    auto it = std::find_if(connections_.begin(), connections_.end(),
        [connectionId](const Connection& c) { return c.id == connectionId; });

    if (it == connections_.end())
        return false;

    connections_.erase(it);
    return true;
}

bool Graph::wouldCreateCycle(int srcNodeId, int dstNodeId) const
{
    if (srcNodeId == dstNodeId)
        return true;

    // BFS from dstNodeId following existing connections. If we can reach
    // srcNodeId, adding srcNodeId -> dstNodeId would create a cycle.
    std::unordered_set<int> visited;
    std::queue<int> frontier;
    frontier.push(dstNodeId);

    while (!frontier.empty())
    {
        int current = frontier.front();
        frontier.pop();

        if (current == srcNodeId)
            return true;

        if (visited.count(current))
            continue;
        visited.insert(current);

        // Follow outgoing connections from current
        for (const auto& c : connections_)
        {
            if (c.source.nodeId == current)
                frontier.push(c.dest.nodeId);
        }
    }

    return false;
}

std::vector<int> Graph::getExecutionOrder() const
{
    // Kahn's algorithm for topological sort

    // Build adjacency list and in-degree count
    std::unordered_map<int, std::vector<int>> adj;
    std::unordered_map<int, int> inDegree;

    for (const auto& [id, _] : nodes_)
    {
        adj[id];  // ensure entry exists
        inDegree[id] = 0;
    }

    for (const auto& c : connections_)
    {
        adj[c.source.nodeId].push_back(c.dest.nodeId);
        inDegree[c.dest.nodeId]++;
    }

    // Start with nodes that have no incoming edges
    std::queue<int> ready;
    for (const auto& [id, deg] : inDegree)
    {
        if (deg == 0)
            ready.push(id);
    }

    std::vector<int> order;
    order.reserve(nodes_.size());

    while (!ready.empty())
    {
        int id = ready.front();
        ready.pop();
        order.push_back(id);

        for (int neighbor : adj[id])
        {
            if (--inDegree[neighbor] == 0)
                ready.push(neighbor);
        }
    }

    return order;
}

std::vector<Connection> Graph::getConnections() const
{
    return connections_;
}

std::vector<Connection> Graph::getConnectionsForNode(int nodeId) const
{
    std::vector<Connection> result;
    for (const auto& c : connections_)
    {
        if (c.source.nodeId == nodeId || c.dest.nodeId == nodeId)
            result.push_back(c);
    }
    return result;
}

std::string Graph::getLastError() const
{
    return lastError_;
}

} // namespace squeeze
