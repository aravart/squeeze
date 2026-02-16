# Graph Specification

## Responsibilities
- Store nodes (non-owning raw pointers) and connections
- Validate connections: port existence, signal type compatibility, cycle detection
- Compute topological execution order (Kahn's algorithm)
- Cascade-remove connections when a node is removed
- Serve as the topology layer for both Engine (top-level) and GroupNode (internal subgraph)

## Overview

Graph is the topology data structure. It knows which nodes exist and how they are connected, validates that connections are legal, and computes the order in which nodes must be processed. It does **not** own nodes, allocate buffers, or process audio.

Both Engine and GroupNode own a `Graph` instance. The API is identical at both levels — the same Graph class handles top-level and nested topologies.

Graph holds **non-owning raw pointers** to nodes. The caller (Engine or GroupNode) is the true owner (`std::unique_ptr<Node>`). This ownership split is deliberate: Graph manages topology, the caller manages lifetime.

## Interface

```cpp
namespace squeeze {

struct Connection {
    int id;
    PortAddress source;   // must reference an output port
    PortAddress dest;     // must reference an input port
};

class Graph {
public:
    Graph() = default;
    ~Graph() = default;

    // Non-copyable, non-movable (owned by Engine/GroupNode)
    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;

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
    // No nextNodeId_ — IDs are assigned by the caller (Engine's IdAllocator)
};

} // namespace squeeze
```

## Connection

The `Connection` struct is defined alongside Graph (in `Graph.h`). It pairs two `PortAddress` values with a unique connection ID.

| Field | Type | Description |
|-------|------|-------------|
| `id` | `int` | Unique within this Graph instance, monotonically increasing, never reused |
| `source` | `PortAddress` | Must reference an output port on an existing node |
| `dest` | `PortAddress` | Must reference an input port on an existing node |

## Node Management

### `addNode(nodeId, node)`

Registers a node with a caller-provided ID. Graph does **not** allocate IDs — the caller (Engine or GroupNode) obtains globally unique IDs from Engine's `IdAllocator` before calling `addNode`.

- Returns `true` on success
- Returns `false` if `nodeId` is already in use or `node` is null
- Graph stores a raw pointer; it does not take ownership

### `removeNode(nodeId)`

Removes the node and **all connections** involving it (both incoming and outgoing).

- Returns `true` on success
- Returns `false` if `nodeId` is not found
- Connections are removed via `std::remove_if` on the connections vector — O(n) in connection count
- The caller is responsible for freeing the node after removal

### `getNode(nodeId)`

Returns the raw `Node*` pointer, or `nullptr` if not found.

### `hasNode(nodeId)`

Returns `true` if the node exists in this Graph.

## Connection Management

### `connect(source, dest, error)`

Creates a connection between two ports. Returns a connection ID on success, `-1` on failure with `error` set.

**Validation order:**

1. Source node exists in this Graph
2. Destination node exists in this Graph
3. Source port exists on the source node (name match, direction == output)
4. Destination port exists on the destination node (name match, direction == input)
5. `canConnect(srcPort, dstPort)` — signal types must match (from Port spec)
6. `wouldCreateCycle(srcNodeId, dstNodeId)` — BFS reachability check

If all checks pass, a new `Connection` is appended with a monotonically increasing ID.

**What is NOT checked:** Audio fan-in is **not** rejected. Multiple audio sources to one input port are allowed — the Engine sums them. This is a v2 change from v1.

### `disconnect(connectionId)`

Removes the connection with the given ID.

- Returns `true` on success
- Returns `false` if the connection ID is not found
- After disconnection, the freed input port is immediately available for new connections

## Cycle Detection

### `wouldCreateCycle(srcNodeId, dstNodeId)`

BFS from `dstNodeId` following existing outgoing connections. If `srcNodeId` is reachable from `dstNodeId`, adding `src → dst` would close a cycle.

```
// Before adding edge A → B, check: can we reach A starting from B?
if (srcNodeId == dstNodeId) return true;  // self-loop

frontier = {dstNodeId}
visited = {dstNodeId}
while frontier not empty:
    current = dequeue
    for each connection where source.nodeId == current:
        if dest.nodeId == srcNodeId: return true  // cycle!
        if dest.nodeId not in visited:
            add to visited and frontier
return false
```

Self-loops (`srcNodeId == dstNodeId`) are rejected as a special case before the BFS.

## Topological Sort

### `getExecutionOrder()`

Returns node IDs in a valid topological processing order using Kahn's algorithm:

1. Build adjacency list and in-degree map from `connections_`
2. Seed a queue with all nodes having in-degree 0
3. Dequeue, append to result, decrement neighbors' in-degrees, enqueue any that reach 0
4. Nodes with no connections (isolated nodes) have in-degree 0 and appear in the result

All registered nodes appear in the output. The relative order among nodes with no ordering constraints is unspecified (but deterministic for a given graph state due to iteration order).

The sort is **recomputed on every call** — no caching or dirty-flag. This is acceptable because `getExecutionOrder()` is called once per structural change (in `buildSnapshot()`), not per audio block.

**Cycles cannot exist** at query time because `connect()` rejects them. If the invariant somehow breaks, nodes in the cycle would be omitted from the output (they never reach in-degree 0).

## Invariants

- Node IDs are provided by the caller and must be unique within this Graph
- Connection IDs are monotonically increasing and never reused within this Graph
- No cycles — enforced at `connect()` time via BFS
- Removing a node removes all its connections (cascade)
- All connections reference nodes that exist in this Graph
- `getExecutionOrder()` returns all registered nodes
- Graph does not own nodes — raw pointers only
- Graph does not allocate or manage audio/MIDI buffers
- Graph is not thread-safe — all access must be serialized by the caller

## Error Conditions

- `addNode()` with null node: returns `false`
- `addNode()` with duplicate ID: returns `false`
- `removeNode()` with unknown ID: returns `false`
- `connect()` with nonexistent source node: returns `-1`, sets error
- `connect()` with nonexistent destination node: returns `-1`, sets error
- `connect()` with nonexistent source port: returns `-1`, sets error
- `connect()` with nonexistent destination port: returns `-1`, sets error
- `connect()` with incompatible signal types: returns `-1`, sets error
- `connect()` that would create a cycle: returns `-1`, sets error
- `disconnect()` with unknown connection ID: returns `false`

## Does NOT Handle

- Node ownership or lifetime (caller — Engine or GroupNode)
- Buffer allocation (Engine)
- Audio/MIDI processing (Engine)
- Thread safety (caller serializes via `controlMutex_`)
- ID allocation (caller provides IDs from Engine's `IdAllocator`)
- Port change detection (caller checks after structural mutations)
- Snapshot building (Engine)

## Dependencies

- Node (calls `getInputPorts()`, `getOutputPorts()` for validation)
- Port (`PortDescriptor`, `PortAddress`, `canConnect()`, `isValid()`)

## Thread Safety

Graph is **not thread-safe**. All methods are called from the control thread, serialized by Engine's `controlMutex_` (or GroupNode's parent Engine `controlMutex_`). The audio thread never touches Graph — it reads the immutable `GraphSnapshot` built from Graph data.

| Method | Thread | Notes |
|--------|--------|-------|
| `addNode()` / `removeNode()` | Control | Called with `controlMutex_` held |
| `connect()` / `disconnect()` | Control | Called with `controlMutex_` held |
| `getExecutionOrder()` | Control | Called during snapshot rebuild |
| `getConnections()` / `getConnectionsForNode()` | Control | Query |
| `getNode()` / `hasNode()` / `getNodeCount()` | Control | Query |

## C ABI

Graph has no direct C ABI surface. It is an internal data structure used by Engine and GroupNode. Topology operations are exposed through Engine-level and GroupNode-level C functions:

```c
// Top-level (Engine)
int  sq_connect(SqEngine engine, int src_node, const char* src_port,
                int dst_node, const char* dst_port, char** error);
bool sq_disconnect(SqEngine engine, int conn_id);
SqConnectionList sq_connections(SqEngine engine);

// GroupNode-level
int  sq_group_connect(SqEngine engine, int group_id,
                      int src_node, const char* src_port,
                      int dst_node, const char* dst_port, char** error);
bool sq_group_disconnect(SqEngine engine, int group_id, int conn_id);
SqConnectionList sq_group_connections(SqEngine engine, int group_id);
```

## Example Usage

```cpp
// Engine creates a Graph and adds nodes with pre-allocated IDs
Graph graph;
auto gain = std::make_unique<GainNode>();
auto comp = std::make_unique<CompressorNode>();

int gainId = idAllocator.next();  // e.g. 1
int compId = idAllocator.next();  // e.g. 2

graph.addNode(gainId, gain.get());
graph.addNode(compId, comp.get());

// Connect gain output to compressor input
std::string error;
int connId = graph.connect(
    {gainId, PortDirection::output, "out"},
    {compId, PortDirection::input, "in"},
    error);
assert(connId >= 0);

// Get processing order
auto order = graph.getExecutionOrder();
// order == {gainId, compId} — gain before compressor

// Cycle detection
int bad = graph.connect(
    {compId, PortDirection::output, "out"},
    {gainId, PortDirection::input, "in"},
    error);
assert(bad == -1);
assert(error.find("cycle") != std::string::npos);

// Remove node cascades connections
graph.removeNode(gainId);
assert(graph.getConnections().empty());
assert(graph.getNodeCount() == 1);
```
