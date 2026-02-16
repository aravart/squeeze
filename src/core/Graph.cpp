#include "core/Graph.h"
#include "core/Logger.h"

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace squeeze {

// ═══════════════════════════════════════════════════════════════════
// Node management
// ═══════════════════════════════════════════════════════════════════

bool Graph::addNode(int nodeId, Node* node)
{
    if (!node)
    {
        SQ_WARN("addNode: null node pointer for id=%d", nodeId);
        return false;
    }
    if (nodes_.count(nodeId))
    {
        SQ_WARN("addNode: duplicate id=%d", nodeId);
        return false;
    }
    SQ_DEBUG("addNode: id=%d", nodeId);
    nodes_[nodeId] = node;
    return true;
}

bool Graph::removeNode(int nodeId)
{
    auto it = nodes_.find(nodeId);
    if (it == nodes_.end())
    {
        SQ_WARN("removeNode: id=%d not found", nodeId);
        return false;
    }
    SQ_DEBUG("removeNode: id=%d (removing %d connections)",
             nodeId, static_cast<int>(connections_.size()));

    // Cascade-remove all connections involving this node
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
            [nodeId](const Connection& c) {
                return c.source.nodeId == nodeId || c.dest.nodeId == nodeId;
            }),
        connections_.end());

    nodes_.erase(it);
    return true;
}

Node* Graph::getNode(int nodeId) const
{
    auto it = nodes_.find(nodeId);
    if (it == nodes_.end()) return nullptr;
    return it->second;
}

int Graph::getNodeCount() const
{
    return static_cast<int>(nodes_.size());
}

bool Graph::hasNode(int nodeId) const
{
    return nodes_.count(nodeId) > 0;
}

// ═══════════════════════════════════════════════════════════════════
// Connection management
// ═══════════════════════════════════════════════════════════════════

int Graph::connect(const PortAddress& source, const PortAddress& dest,
                   std::string& error)
{
    SQ_DEBUG("connect: %d:%s -> %d:%s",
             source.nodeId, source.portName.c_str(),
             dest.nodeId, dest.portName.c_str());

    // 1. Source node exists
    auto srcIt = nodes_.find(source.nodeId);
    if (srcIt == nodes_.end())
    {
        error = "source node " + std::to_string(source.nodeId) + " not found";
        SQ_WARN("connect failed: %s", error.c_str());
        return -1;
    }

    // 2. Destination node exists
    auto dstIt = nodes_.find(dest.nodeId);
    if (dstIt == nodes_.end())
    {
        error = "destination node " + std::to_string(dest.nodeId) + " not found";
        SQ_WARN("connect failed: %s", error.c_str());
        return -1;
    }

    // 3. Source port exists as output
    Node* srcNode = srcIt->second;
    auto srcPorts = srcNode->getOutputPorts();
    const PortDescriptor* srcPort = nullptr;
    for (const auto& p : srcPorts)
    {
        if (p.name == source.portName)
        {
            srcPort = &p;
            break;
        }
    }
    if (!srcPort)
    {
        error = "source port '" + source.portName + "' not found on node " +
                std::to_string(source.nodeId);
        SQ_WARN("connect failed: %s", error.c_str());
        return -1;
    }

    // 4. Destination port exists as input
    Node* dstNode = dstIt->second;
    auto dstPorts = dstNode->getInputPorts();
    const PortDescriptor* dstPort = nullptr;
    for (const auto& p : dstPorts)
    {
        if (p.name == dest.portName)
        {
            dstPort = &p;
            break;
        }
    }
    if (!dstPort)
    {
        error = "destination port '" + dest.portName + "' not found on node " +
                std::to_string(dest.nodeId);
        SQ_WARN("connect failed: %s", error.c_str());
        return -1;
    }

    // 5. canConnect (signal type compatibility)
    if (!canConnect(*srcPort, *dstPort))
    {
        error = "incompatible ports: cannot connect '" + source.portName +
                "' to '" + dest.portName + "'";
        SQ_WARN("connect failed: %s", error.c_str());
        return -1;
    }

    // 6. Cycle detection
    if (wouldCreateCycle(source.nodeId, dest.nodeId))
    {
        error = "connection would create a cycle";
        SQ_WARN("connect failed: %s", error.c_str());
        return -1;
    }

    int connId = nextConnectionId_++;
    connections_.push_back({connId, source, dest});
    SQ_DEBUG("connect: created connection id=%d", connId);
    return connId;
}

bool Graph::disconnect(int connectionId)
{
    auto it = std::find_if(connections_.begin(), connections_.end(),
        [connectionId](const Connection& c) { return c.id == connectionId; });

    if (it == connections_.end())
    {
        SQ_DEBUG("disconnect: connection id=%d not found", connectionId);
        return false;
    }

    SQ_DEBUG("disconnect: removing connection id=%d (%d:%s -> %d:%s)",
             connectionId,
             it->source.nodeId, it->source.portName.c_str(),
             it->dest.nodeId, it->dest.portName.c_str());
    connections_.erase(it);
    return true;
}

// ═══════════════════════════════════════════════════════════════════
// Queries
// ═══════════════════════════════════════════════════════════════════

std::vector<int> Graph::getExecutionOrder() const
{
    // Kahn's algorithm
    std::unordered_map<int, int> inDegree;
    std::unordered_map<int, std::vector<int>> adjacency;

    // Initialize all nodes with in-degree 0
    for (const auto& pair : nodes_)
    {
        inDegree[pair.first] = 0;
        adjacency[pair.first] = {};
    }

    // Build adjacency and in-degree from connections
    for (const auto& conn : connections_)
    {
        adjacency[conn.source.nodeId].push_back(conn.dest.nodeId);
        inDegree[conn.dest.nodeId]++;
    }

    // Seed queue with in-degree 0 nodes
    std::queue<int> ready;
    for (const auto& pair : inDegree)
    {
        if (pair.second == 0)
            ready.push(pair.first);
    }

    std::vector<int> order;
    order.reserve(nodes_.size());

    while (!ready.empty())
    {
        int current = ready.front();
        ready.pop();
        order.push_back(current);

        for (int neighbor : adjacency[current])
        {
            inDegree[neighbor]--;
            if (inDegree[neighbor] == 0)
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
    for (const auto& conn : connections_)
    {
        if (conn.source.nodeId == nodeId || conn.dest.nodeId == nodeId)
            result.push_back(conn);
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════
// Cycle detection
// ═══════════════════════════════════════════════════════════════════

bool Graph::wouldCreateCycle(int srcNodeId, int dstNodeId) const
{
    // Self-loop check
    if (srcNodeId == dstNodeId) return true;

    // BFS from dstNodeId following outgoing connections.
    // If we can reach srcNodeId, adding src→dst would close a cycle.
    std::queue<int> frontier;
    std::unordered_set<int> visited;
    frontier.push(dstNodeId);
    visited.insert(dstNodeId);

    while (!frontier.empty())
    {
        int current = frontier.front();
        frontier.pop();

        for (const auto& conn : connections_)
        {
            if (conn.source.nodeId == current)
            {
                if (conn.dest.nodeId == srcNodeId)
                    return true;
                if (visited.find(conn.dest.nodeId) == visited.end())
                {
                    visited.insert(conn.dest.nodeId);
                    frontier.push(conn.dest.nodeId);
                }
            }
        }
    }

    return false;
}

} // namespace squeeze
