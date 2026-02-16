#pragma once

#include "core/Graph.h"
#include "core/Node.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace squeeze {

class Engine {
public:
    Engine();
    ~Engine();

    /// Returns the engine version string.
    std::string getVersion() const;

    // --- Node management ---
    int addNode(const std::string& name, std::unique_ptr<Node> node);
    bool removeNode(int nodeId);
    Node* getNode(int nodeId) const;
    std::string getNodeName(int nodeId) const;

    // --- Connection management ---
    int connect(int srcNode, const std::string& srcPort,
                int dstNode, const std::string& dstPort,
                std::string& error);
    bool disconnect(int connectionId);
    std::vector<Connection> getConnections() const;

private:
    struct NodeEntry {
        std::string name;
        std::unique_ptr<Node> node;
    };

    std::unordered_map<int, NodeEntry> nodes_;
    Graph graph_;
    int nextNodeId_ = 1;
};

} // namespace squeeze
