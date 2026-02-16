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
    nodes_[id] = {name, std::move(node)};
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

} // namespace squeeze
