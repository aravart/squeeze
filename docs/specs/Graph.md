# Graph Specification

## Overview

Graph manages the topology of nodes and connections on the control thread. It validates connections, detects cycles, and computes a topological execution order. Graph does not process audio — it produces the ordering and connection information that Engine uses.

## Responsibilities

- Store nodes (by pointer, does not own them)
- Store connections between node ports
- Validate connections at creation time (type safety, port existence, no cycles)
- Compute topological execution order
- Remove a node's connections when the node is removed

## Connection

```cpp
struct Connection {
    int id;
    PortAddress source;  // must be output
    PortAddress dest;    // must be input
};
```

## Interface

```cpp
class Graph {
public:
    // Node management
    int addNode(Node* node);                         // returns nodeId
    bool removeNode(int nodeId);                     // returns false if not found
    Node* getNode(int nodeId) const;                 // returns nullptr if not found

    // Connection management
    int connect(PortAddress source, PortAddress dest);  // returns connectionId, -1 on error
    bool disconnect(int connectionId);                  // returns false if not found

    // Queries
    std::vector<int> getExecutionOrder() const;
    std::vector<Connection> getConnections() const;
    std::vector<Connection> getConnectionsForNode(int nodeId) const;
    int getNodeCount() const;
    std::string getLastError() const;
};
```

## Invariants

- Execution order is always a valid topological sort of the connection DAG
- Cycles are rejected at connection time
- Removing a node removes all its connections
- A connection requires: source node exists, dest node exists, source port exists on source node, dest port exists on dest node, ports pass `canConnect()`
- Each input port accepts at most one connection (no fan-in for this milestone)
- An output port may connect to multiple inputs (fan-out is allowed)
- Node IDs are unique and never reused within a Graph instance
- Connection IDs are unique and never reused within a Graph instance

## Error Conditions

All errors are reported via `getLastError()` after a failed operation.

- `connect()` returns -1 if:
  - Source node does not exist
  - Destination node does not exist
  - Source port not found on source node
  - Destination port not found on destination node
  - Ports are incompatible (signal type or channel mismatch)
  - Connection would create a cycle
  - Destination input port already has a connection
- `removeNode()` returns false if node ID not found
- `disconnect()` returns false if connection ID not found
- `getNode()` returns nullptr if node ID not found

## Does NOT Handle

- Audio processing (Engine)
- Buffer allocation (Engine)
- Thread safety — Graph is only used from the control thread
- Node ownership or lifetime (caller's responsibility)

## Dependencies

- Port (`PortDescriptor`, `PortAddress`, `canConnect()`)
- Node (for port queries via `getInputPorts()` / `getOutputPorts()`)

## Thread Safety

Graph is not thread-safe. It is used exclusively from the control thread. Engine takes a snapshot of the execution order and connection info for the audio thread.

## Example Usage

```cpp
Graph graph;

// Add nodes
int synthId = graph.addNode(synthPlugin);
int reverbId = graph.addNode(reverbPlugin);

// Connect synth audio out -> reverb audio in
int connId = graph.connect(
    {synthId, PortDirection::output, "out"},
    {reverbId, PortDirection::input, "in"}
);

// Get processing order
auto order = graph.getExecutionOrder();
// order: [synthId, reverbId] — synth before reverb

// Remove reverb (also removes the connection)
graph.removeNode(reverbId);
```
