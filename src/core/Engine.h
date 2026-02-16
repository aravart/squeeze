#pragma once

#include "core/Node.h"

#include <memory>
#include <string>
#include <unordered_map>

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

private:
    struct NodeEntry {
        std::string name;
        std::unique_ptr<Node> node;
    };

    std::unordered_map<int, NodeEntry> nodes_;
    int nextNodeId_ = 1;
};

} // namespace squeeze
