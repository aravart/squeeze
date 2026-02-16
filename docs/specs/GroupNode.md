# GroupNode Specification

## Responsibilities
- Implement the `Node` interface while owning an internal `Graph` of child nodes
- Provide a node management API that mirrors Engine (add, remove, connect, disconnect)
- Run the internal graph in topological order during `process()`
- Export selected internal ports as group-level ports via `exportInput()` / `exportOutput()`
- Manage internal buffer allocation and audio/MIDI routing between child nodes
- Propagate `prepare()` / `release()` to all internal nodes

## Overview

GroupNode is the composition primitive. From the parent graph's perspective, it is just another Node with ports and parameters. Internally, it owns a complete subgraph — nodes, connections, topological sort, buffer routing — mirroring the Engine's graph management API.

This enables composite node types (channel strips, drum kits, mixer buses, effects racks) to be built from the same primitives used at the top level, without requiring dedicated node classes for each pattern.

```
Parent graph sees:              GroupNode internals:

                                ┌──────────────────────────────┐
 src ──→ ┌──────────┐          │  "in" → GainNode_1 ──→╮      │
         │ GroupNode │──→ dst  │         GainNode_2 ──→├→ out  │
 src ──→ │          │          │         GainNode_3 ──→╯      │
         └──────────┘          └──────────────────────────────┘
  2 exported inputs              Internal graph with connections
  1 exported output              and its own topological sort
```

## Interface

```cpp
namespace squeeze {

class GroupNode : public Node {
public:
    GroupNode(const std::string& name, IdAllocator& idAllocator);
    ~GroupNode() override;

    // --- Node management (mirrors Engine) ---
    int addNode(std::unique_ptr<Node> node, const std::string& name);
    bool removeNode(int nodeId);
    Node* getNode(int nodeId) const;
    std::string getNodeName(int nodeId) const;

    // --- Internal connections (mirrors Engine) ---
    int connect(int srcNode, const std::string& srcPort,
                int dstNode, const std::string& dstPort,
                std::string& error);
    bool disconnect(int connectionId);
    std::vector<Connection> getConnections() const;

    // --- Port export (GroupNode-specific) ---
    bool exportInput(int internalNodeId, const std::string& internalPort,
                     const std::string& externalName, std::string& error);
    bool exportOutput(int internalNodeId, const std::string& internalPort,
                      const std::string& externalName, std::string& error);
    bool unexportPort(const std::string& externalName);

    // --- Query ---
    int getNodeCount() const;
    std::vector<int> getExecutionOrder() const;

    // --- Node interface ---
    void prepare(double sampleRate, int blockSize) override;
    void process(ProcessContext& context) override;
    void release() override;
    std::vector<PortDescriptor> getInputPorts() const override;
    std::vector<PortDescriptor> getOutputPorts() const override;

    // GroupNode's own parameters (e.g. master gain, mute)
    std::vector<ParameterDescriptor> getParameterDescriptors() const override;
    float getParameter(const std::string& name) const override;
    void setParameter(const std::string& name, float value) override;
    std::string getParameterText(const std::string& name) const override;
};

} // namespace squeeze
```

## Node Management

GroupNode mirrors Engine's node management API:

| Engine | GroupNode | Notes |
|--------|-----------|-------|
| `addNode(node, name)` | `addNode(node, name)` | Returns node ID |
| `removeNode(id)` | `removeNode(id)` | Auto-disconnects, auto-unexports |
| `getNode(id)` | `getNode(id)` | Returns raw pointer |
| `connect(src, srcPort, dst, dstPort)` | `connect(src, srcPort, dst, dstPort)` | Returns connection ID |
| `disconnect(connId)` | `disconnect(connId)` | |

### Node IDs

All node IDs are **globally unique** — the Engine provides a single ID allocator shared across all levels. GroupNode receives a reference to this allocator at construction (passed by Engine). When a node is added to a GroupNode, it receives a globally unique ID. This means any node (top-level or deeply nested) can be addressed directly by its ID for parameter access:

```c
sq_set_param(engine, any_node_id, "cutoff", 0.5);  // works regardless of nesting depth
```

The `sq_group_*` functions provide scoped access for structural operations (add, remove, connect, export), but parameter get/set works directly on any node by its global ID.

### Nesting

GroupNode supports nesting — a GroupNode can contain other GroupNodes. The same API works at every level. Global IDs ensure any node can be reached without path navigation.

## Port Export

GroupNode's external ports (visible to the parent graph) are determined entirely by export declarations. No ports exist until something is exported.

### `exportInput(internalNodeId, internalPort, externalName)`

Makes an internal node's **input** port visible as a group-level **input** port.

- The internal node must exist and have the named input port
- `externalName` must be unique among the group's current external ports
- The exported port inherits the internal port's `SignalType` and `channels`
- The group's `getInputPorts()` updates to include this port

When the parent graph connects to this exported input, audio/MIDI data flows from the external source into the internal node's input.

### `exportOutput(internalNodeId, internalPort, externalName)`

Makes an internal node's **output** port visible as a group-level **output** port.

- Same validation rules as `exportInput`
- The group's `getOutputPorts()` updates to include this port

When the parent graph reads from this exported output, it receives the internal node's output.

### `unexportPort(externalName)`

Removes an exported port. The internal node and its port remain — only the external visibility is removed.

### Cascading topology changes

Unexporting a port (or removing an internal node that has exported ports) changes the group's external port list. The **Engine** is responsible for detecting this and auto-disconnecting any parent-graph connections that reference the removed external port (logged at warn level). This cascade crosses the encapsulation boundary by design — the Engine monitors GroupNode port changes during any structural mutation and cleans up the parent graph accordingly.

### Dynamic ports

Exporting and unexporting changes the group's port list at runtime (control thread only). This is the primary use case for dynamic ports. Like all structural changes, export/unexport invalidates the GroupNode for audio processing until `prepare()` is called (see **Structural change invalidation** under Invariants).

## Processing

### `prepare(sampleRate, blockSize)`

1. Propagate `prepare()` to all internal nodes
2. Compute internal topological execution order
3. Pre-allocate internal audio/MIDI buffers for routing between internal nodes

### `process(context)`

RT-safe. Runs the internal graph for the given `numSamples`:

1. **Map external inputs** — copy data from `context.inputAudio` / `context.inputMidi` into the internal buffers of nodes with exported input ports
2. **Execute internal nodes** in topological order — for each node:
   - Route audio from upstream internal connections into the node's input buffer (with fan-in summing)
   - Route MIDI from upstream internal connections into the node's MIDI input buffer (merged)
   - Call `node->process(internalContext)`
3. **Map external outputs** — copy data from internal nodes with exported output ports into `context.outputAudio` / `context.outputMidi`

**No auto-summing.** Internal nodes whose audio outputs are not consumed by any internal connection and are not exported are simply unused — their output is discarded silently. This matches the Engine's routing model: all routing is explicit at every level. If you want audio to reach an external output, connect it to an exported node or export it directly. Unconnected outputs are warned about at info level.

### `release()`

Propagate `release()` to all internal nodes. Free internal buffers.

## GroupNode's Own Parameters

GroupNode can have its own parameters (e.g., master gain, mute, solo) independent of internal node parameters. These are set/queried via the standard Node parameter interface using the group's own node ID:

```c
sq_set_param(engine, group_id, "master_gain", 0.8);  // group's own param
sq_set_param(engine, eq_id, "frequency", 0.5);        // internal node's param (global ID)
```

## Invariants

### Structural change invalidation

**Any structural change to a GroupNode invalidates it for audio processing.** Structural changes are: `addNode()`, `removeNode()`, `connect()`, `disconnect()`, `exportInput()`, `exportOutput()`, `unexportPort()`. After any of these, `prepare()` must be called before the next `process()`. Calling `process()` on a structurally-modified GroupNode that has not been re-prepared is undefined behavior.

In practice, the Engine enforces this: every structural mutation triggers a snapshot rebuild, which calls `prepare()` on all nodes (including the GroupNode) before the new snapshot is swapped to the audio thread. The audio thread never sees an unprepared GroupNode.

### Other invariants

- GroupNode implements the full `Node` interface — the parent graph treats it as opaque
- Internal topological sort is recomputed after any structural change
- All internal processing is RT-safe (same rules as top-level nodes)
- Internal buffer allocation happens in `prepare()`, not in `process()`
- Removing an internal node auto-unexports any of its exported ports and auto-disconnects all internal connections involving that node
- Engine detects external port changes from unexport/removal and auto-disconnects affected parent-graph connections (cascade)
- An internal port can be exported at most once (one external name per internal port)
- Global ID uniqueness: no two nodes anywhere in the system share an ID
- No auto-summing — all routing is explicit (consistent with Engine)

## Error Conditions

- `addNode()` with null node: returns -1
- `removeNode()` with unknown ID: returns false
- `connect()` referencing nonexistent node or port: returns -1, sets error
- `connect()` creating a cycle in the internal graph: returns -1, sets error
- `exportInput()` on an output port (or vice versa): returns false, sets error
- `exportInput()` with duplicate external name: returns false, sets error
- `exportInput()` on a nonexistent internal node/port: returns false, sets error
- `unexportPort()` with unknown external name: returns false

## Does NOT Handle

- Transport (uses parent Engine's transport)
- CommandQueue / command queuing (parent Engine handles atomicity)
- EventScheduler / sample-accurate automation (parent Engine handles sub-block splitting)
- Deferred deletion (parent Engine's snapshot swap handles this)
- Audio device management
- controlMutex_ (shares parent Engine's lock)
- PerfMonitor (parent Engine monitors overall performance)

## Dependencies

- Node (implements Node interface)
- Graph (owns internal Graph instance)
- Engine (provides global ID allocator; handles topology cascade detection)
- Port (PortDescriptor, PortAddress, Connection)

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| `addNode()` / `removeNode()` | Control | Called with Engine's controlMutex_ held |
| `connect()` / `disconnect()` | Control | Called with Engine's controlMutex_ held |
| `exportInput()` / `exportOutput()` / `unexportPort()` | Control | Called with Engine's controlMutex_ held |
| `getNode()` / `getConnections()` / `getExecutionOrder()` | Control | Query methods |
| `prepare()` / `release()` | Control | Propagates to internal nodes |
| `process()` | Audio | RT-safe; runs internal graph |
| `getInputPorts()` / `getOutputPorts()` | Control | Dynamic; audio thread reads via snapshot |

## C ABI

```c
// Creation
int sq_add_group(SqEngine engine, const char* name, char** error);

// Node management within a group (mirrors top-level API)
int  sq_group_add_plugin(SqEngine engine, int group_id, const char* plugin_name, char** error);
int  sq_group_add_sampler(SqEngine engine, int group_id, const char* name, int max_voices, char** error);
int  sq_group_add_group(SqEngine engine, int group_id, const char* name, char** error);
bool sq_group_remove_node(SqEngine engine, int group_id, int node_id);

// Internal connections
int  sq_group_connect(SqEngine engine, int group_id,
                      int src_node, const char* src_port,
                      int dst_node, const char* dst_port, char** error);
bool sq_group_disconnect(SqEngine engine, int group_id, int conn_id);

// Port export
bool sq_group_export_input(SqEngine engine, int group_id,
                           int internal_node, const char* internal_port,
                           const char* external_name, char** error);
bool sq_group_export_output(SqEngine engine, int group_id,
                            int internal_node, const char* internal_port,
                            const char* external_name, char** error);
bool sq_group_unexport(SqEngine engine, int group_id,
                       const char* external_name);

// Query
SqIdNameList sq_group_nodes(SqEngine engine, int group_id);
SqConnectionList sq_group_connections(SqEngine engine, int group_id);
```

Parameter access on internal nodes uses the standard `sq_set_param` / `sq_get_param` with the internal node's global ID — no group-specific parameter functions needed.

## Python API

```python
# Create a channel strip as a GroupNode
strip = engine.add_group("strip_1")

# Add nodes inside the group (same pattern as engine-level)
eq = engine.group_add_plugin(strip, "TDR Nova")
comp = engine.group_add_plugin(strip, "TDR Kotelnikov")

# Connect internal nodes
engine.group_connect(strip, eq, "out", comp, "in")

# Export ports to make the group usable from outside
engine.group_export_input(strip, eq, "in", "in")
engine.group_export_output(strip, comp, "out", "out")

# From the parent graph, strip is just another node
engine.connect(synth, "out", strip, "in")
engine.connect(strip, "out", master, "in")

# Set parameters on internal nodes directly (global IDs)
engine.set_param(eq, "frequency", 0.5)
engine.set_param(comp, "threshold", 0.3)

# Set the group's own parameters
engine.set_param(strip, "mute", 0.0)
```

## Example: Drum Kit

```python
kit = engine.add_group("drums")

kick  = engine.group_add_sampler(kit, "kick", 4)
snare = engine.group_add_sampler(kit, "snare", 4)
hat   = engine.group_add_sampler(kit, "hihat", 4)

# Export each sampler's MIDI input for external routing
engine.group_export_input(kit, kick,  "midi_in", "kick_midi")
engine.group_export_input(kit, snare, "midi_in", "snare_midi")
engine.group_export_input(kit, hat,   "midi_in", "hat_midi")

# Export audio outputs (all routing is explicit — no auto-summing)
engine.group_export_output(kit, kick,  "out", "kick_out")
engine.group_export_output(kit, snare, "out", "snare_out")
engine.group_export_output(kit, hat,   "out", "hat_out")

# From the parent graph
engine.connect(midi_router, "ch10", kit, "kick_midi")
engine.connect(kit, "kick_out",  mixer, "in")
engine.connect(kit, "snare_out", mixer, "in")   # fan-in sums at mixer input
engine.connect(kit, "hat_out",   mixer, "in")
```
