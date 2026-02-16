#pragma once

#include "core/Node.h"
#include "core/Port.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace squeeze {

struct Connection {
    int id;
    PortAddress source;
    PortAddress dest;
};

class Graph {
public:
    Graph() = default;
    ~Graph() = default;

    // Non-copyable, non-movable
    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;
    Graph(Graph&&) = delete;
    Graph& operator=(Graph&&) = delete;

    // --- Node management ---
    bool addNode(int nodeId, Node* node);
    bool removeNode(int nodeId);
    Node* getNode(int nodeId) const;
    int getNodeCount() const;
    bool hasNode(int nodeId) const;

    // --- Connection management ---
    int connect(const PortAddress& source, const PortAddress& dest,
                std::string& error);
    bool disconnect(int connectionId);

    // --- Queries ---
    std::vector<int> getExecutionOrder() const;
    std::vector<Connection> getConnections() const;
    std::vector<Connection> getConnectionsForNode(int nodeId) const;

private:
    bool wouldCreateCycle(int srcNodeId, int dstNodeId) const;

    std::unordered_map<int, Node*> nodes_;
    std::vector<Connection> connections_;
    int nextConnectionId_ = 0;
};

} // namespace squeeze
