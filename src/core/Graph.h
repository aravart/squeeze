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
    int midiChannel = 0;  // 0 = all channels, 1-16 = specific channel
};

class Graph {
public:
    int addNode(Node* node);
    bool removeNode(int nodeId);
    Node* getNode(int nodeId) const;

    int connect(PortAddress source, PortAddress dest, int midiChannel = 0);
    bool disconnect(int connectionId);

    std::vector<int> getExecutionOrder() const;
    std::vector<Connection> getConnections() const;
    std::vector<Connection> getConnectionsForNode(int nodeId) const;
    int getNodeCount() const;
    std::string getLastError() const;

private:
    const PortDescriptor* findPort(Node* node, PortDirection direction,
                                   const std::string& name) const;
    bool wouldCreateCycle(int srcNodeId, int dstNodeId) const;

    std::unordered_map<int, Node*> nodes_;
    std::vector<Connection> connections_;
    int nextNodeId_ = 0;
    int nextConnectionId_ = 0;
    std::string lastError_;
};

} // namespace squeeze
