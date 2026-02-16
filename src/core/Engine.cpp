#include "core/Engine.h"
#include "core/Logger.h"

namespace squeeze {

Engine::Engine() {}

Engine::~Engine() {}

std::string Engine::getVersion() const
{
    return "0.2.0";
}

int Engine::addNode(const std::string& name, std::unique_ptr<Node> node)
{
    int id = nextNodeId_++;
    SQ_DEBUG("addNode: id=%d name=%s", id, name.c_str());
    Node* raw = node.get();
    nodes_[id] = {name, std::move(node)};
    graph_.addNode(id, raw);
    return id;
}

bool Engine::removeNode(int nodeId)
{
    auto it = nodes_.find(nodeId);
    if (it == nodes_.end())
    {
        SQ_DEBUG("removeNode: id=%d not found", nodeId);
        return false;
    }
    SQ_DEBUG("removeNode: id=%d name=%s", nodeId, it->second.name.c_str());
    graph_.removeNode(nodeId);
    nodes_.erase(it);
    return true;
}

Node* Engine::getNode(int nodeId) const
{
    auto it = nodes_.find(nodeId);
    if (it == nodes_.end()) return nullptr;
    return it->second.node.get();
}

std::string Engine::getNodeName(int nodeId) const
{
    auto it = nodes_.find(nodeId);
    if (it == nodes_.end()) return "";
    return it->second.name;
}

int Engine::connect(int srcNode, const std::string& srcPort,
                    int dstNode, const std::string& dstPort,
                    std::string& error)
{
    PortAddress src{srcNode, PortDirection::output, srcPort};
    PortAddress dst{dstNode, PortDirection::input, dstPort};
    return graph_.connect(src, dst, error);
}

bool Engine::disconnect(int connectionId)
{
    return graph_.disconnect(connectionId);
}

std::vector<Connection> Engine::getConnections() const
{
    return graph_.getConnections();
}

} // namespace squeeze
