# Modulation Specification

## Overview

Per-sample parameter modulation using audio-rate signals routed through the existing graph infrastructure. Modulation sources are ordinary nodes with audio outputs. Their outputs can be routed to parameters on destination nodes via a new connection type. The graph handles dependency ordering; the render loop fills per-sample parameter buffers before calling `process()` on the destination node.

## Responsibilities

- Route audio-rate signals from modulation source outputs to destination node parameters
- Maintain per-sample parameter buffers on nodes for modulated parameters
- Fill buffers with base value + accumulated modulation before each `process()` call
- Clamp final modulated values to the parameter's normalized range [0.0, 1.0]
- Support fan-out (one source → many parameters) and fan-in (many sources → one parameter)
- Integrate mod connections into the graph's cycle detection and topological sort

## Data Structures

### ModConnection

```cpp
struct ModConnection {
    int id;
    int sourceNodeId;
    std::string sourcePort;          // audio output port on source node
    int destNodeId;
    int destParamIndex;               // parameter index on destination node
    std::atomic<float> depth{0.0f};   // scaling factor, typically -1.0 to 1.0

    ModConnection(int id, int srcId, std::string srcPort,
                  int dstId, int dstParam, float d)
        : id(id), sourceNodeId(srcId), sourcePort(std::move(srcPort)),
          destNodeId(dstId), destParamIndex(dstParam) { depth.store(d); }

    // Non-copyable, non-movable (atomic member)
    ModConnection(const ModConnection&) = delete;
    ModConnection& operator=(const ModConnection&) = delete;
};
```

Owned by Engine as `std::unordered_map<int, std::unique_ptr<ModConnection>>`. The `depth` field is `std::atomic<float>` so it can be written from the control thread and read from the audio thread without a snapshot rebuild. The snapshot stores a pointer to the atomic, not a copy of the value.

Because `std::atomic` is non-copyable, `ModConnection` itself is non-copyable. APIs that return route info to the caller use a plain data struct instead:

```cpp
struct ModRouteInfo {
    int id;
    int sourceNodeId;
    std::string sourcePort;
    int destNodeId;
    int destParamIndex;
    float depth;   // snapshot of current value
};
```

**Deferred deletion** follows the same pattern as nodes, but requires a two-phase sequence because the snapshot holds a raw pointer to `ModConnection::depth`:

1. Rebuild snapshot without the removed route (audio thread stops reading the depth pointer)
2. Wait for the old snapshot to be garbage-collected (control thread detects the audio thread has swapped to the new snapshot)
3. Queue the `ModConnection` into `pendingModConnectionDeletions_` for destruction

The old snapshot and old ModConnection are destroyed together on the next control-thread GC pass after the audio thread adopts the new snapshot.

### ParameterDescriptor Extension

```cpp
struct ParameterDescriptor {
    // ... existing fields ...
    bool modulatable = false;  // true if this parameter accepts per-sample modulation
};
```

- Boolean and discrete parameters: `modulatable = false`
- Continuous parameters: `modulatable = true` (node decides per-parameter)
- VST3 plugin parameters: `modulatable = true` (delivered as breakpoints, not per-sample)

## Node Interface Changes

### Mod Buffers (protected, on Node base class)

```cpp
class Node {
protected:
    std::vector<std::vector<float>> modBuffers_;    // [paramIndex][sample]
    std::vector<uint8_t> paramModActive_;             // [paramIndex] 1 if modulated this block

    /// Read a parameter value for a given sample.
    /// If modulation is active, returns the per-sample modulated value.
    /// If not, returns the scalar base value from getParameter().
    float readParam(int paramIndex, int sampleIndex) const;

public:
    /// Allocate mod buffers for all modulatable parameters.
    /// Called by Engine after prepare(), before any process() calls.
    void prepareModulation(int blockSize);

    /// Access mod buffer for render loop to fill. Returns nullptr if param is not modulatable.
    float* getModBuffer(int paramIndex);

    /// Set whether a parameter has active modulation this block.
    void setModActive(int paramIndex, bool active);
};
```

**`readParam()` implementation:**

```cpp
float Node::readParam(int paramIndex, int sampleIndex) const {
    if (paramIndex < (int)paramModActive_.size() && paramModActive_[paramIndex])
        return modBuffers_[paramIndex][sampleIndex];
    return getParameter(paramIndex);
}
```

The bounds check guards against calls before `prepareModulation()` or with out-of-range indices — falls back to the scalar value. This is the primary interface for mod-aware nodes. Nodes that don't use modulation continue to call `getParameter()` — no change required.

**`uint8_t` rationale**: `std::vector<bool>` is bit-packed and returns a proxy object from `operator[]`, which is not cache-friendly and not safe to use in performance-critical per-sample code on the audio thread. `uint8_t` provides direct, predictable access.

### prepareModulation()

Called by Engine on the control thread after `prepare()`, and again whenever the block size changes (Engine calls `prepareModulation(newBlockSize)` as part of re-preparation). Allocates buffers for all modulatable parameters based on `getParameterDescriptors()`:

```cpp
void Node::prepareModulation(int blockSize) {
    auto descs = getParameterDescriptors();
    int maxIndex = 0;
    for (auto& d : descs)
        if (d.index >= maxIndex) maxIndex = d.index + 1;

    modBuffers_.resize(maxIndex);
    paramModActive_.resize(maxIndex, 0);

    for (auto& d : descs) {
        if (d.modulatable)
            modBuffers_[d.index].resize(blockSize, 0.0f);
    }
}
```

Note: if parameter indices are sparse (e.g., 0, 1, 30), the outer vectors are sized to `maxIndex` with empty inner vectors for non-modulatable slots. This wastes a few pointers but keeps index-based lookup O(1). The inner vectors are only allocated for modulatable parameters.

## Graph Changes

### New Data Structure

Graph stores mod connections as topology-only edges (no depth — that lives on Engine's `ModConnection`):

```cpp
struct ModEdge {
    int id;
    int sourceNodeId;
    std::string sourcePort;    // audio output port name
    int destNodeId;
    int destParamIndex;
};
```

Stored in `std::vector<ModEdge> modEdges_` with a `nextModEdgeId_` counter. Mod edge IDs are in a separate namespace from audio connection IDs (no collision risk — Engine uses the mod edge ID as the `ModConnection::id`).

### New Methods

```cpp
int Graph::modConnect(int sourceNodeId, const std::string& sourcePort,
                      int destNodeId, int destParamIndex);

bool Graph::modDisconnect(int modEdgeId);

std::vector<ModEdge> Graph::getModEdges() const;
std::vector<int> Graph::getModEdgeIds() const;
```

`modConnect()` assigns the next `modEdgeId`, stores the `ModEdge`, and returns the ID. Engine uses this same ID as the `ModConnection::id`, so both layers share a single ID space for mod routes.

### removeNode()

`Graph::removeNode()` removes all mod edges where the node is source or destination, same `std::erase_if` pattern as audio connections.

### Validation (modConnect)

`modConnect()` validates:

1. Source node exists
2. Source port exists and is `SignalType::audio`, `PortDirection::output`
3. Destination node exists
4. Destination param index is valid (within `getParameterDescriptors()`)
5. Destination param is `modulatable == true`
6. Connection would not create a cycle (uses same DAG as audio connections)

Returns connection ID on success, -1 on failure (with `lastError_` set).

### Cycle Detection

`wouldCreateCycle()` considers BOTH audio connections and mod connections when checking for cycles. Both are edges in the same dependency graph. A mod connection from node A to node B means "A must be processed before B," just like an audio connection.

### Topological Sort

`getExecutionOrder()` builds its adjacency list from both audio and mod connections. The sort guarantees that any node providing modulation is processed before any node that consumes it.

### Fan-in / Fan-out

- **Fan-out**: One source port can have multiple mod connections to different destination parameters (or the same parameter on different nodes). No restriction.
- **Fan-in**: Multiple mod connections can target the same `(destNodeId, destParamIndex)`. Their contributions are summed (see Combine Semantics).
- **Mixed**: A source node's audio output can simultaneously feed audio connections AND mod connections. The output buffer is shared (read-only by all consumers).

## GraphSnapshot Changes

```cpp
struct GraphSnapshot {
    struct ModRoute {
        int destParamIndex;
        int sourceSlotIndex;              // index into slots[] for the source node
        int sourceChannel;                // 0 = mono/left (always 0 for Phase 1)
        const std::atomic<float>* depth;  // points to ModConnection's atomic depth
    };

    struct NodeSlot {
        Node* node;
        int nodeId;
        int audioSourceIndex;
        bool isAudioLeaf;
        std::vector<ModRoute> modRoutes;  // mod connections targeting this slot
    };

    // ... existing fields unchanged ...
};
```

`buildSnapshot()` compiles mod edges into per-slot `modRoutes` vectors, translating node IDs to slot indices. For each mod edge, it looks up the corresponding `ModConnection` in Engine's `ownedModConnections_` to obtain the `depth` pointer. The snapshot does not copy the depth value, so changes take effect immediately without a snapshot rebuild.

**Source channel**: In Phase 1, `sourceChannel` is always 0. If the source node has a stereo output port, only the left channel is used for modulation. The Lua/Engine API has no channel parameter. Phase 2 may add channel selection.

## Render Loop Changes

The node processing loop (Step 4 of `processBlock()`) gains modulation buffer filling before each `process()` call:

```
for each slot i in execution order:

    // --- NEW: Fill modulation buffers ---

    // 1. Reset all mod-active flags to false
    for each modulatable param p on slot.node:
        slot.node->setModActive(p, false)

    // 2. For params with mod connections: initialize buffer with base value
    for each unique destParamIndex in slot.modRoutes:
        float base = slot.node->getParameter(destParamIndex)
        float* buf = slot.node->getModBuffer(destParamIndex)
        fill buf[0..numSamples-1] with base
        slot.node->setModActive(destParamIndex, true)

    // 3. Accumulate modulation signals
    for each route in slot.modRoutes:
        float d = route.depth->load(relaxed)       // hoist atomic read: once per route per block
        const float* src = snap.audioOutputs[route.sourceSlotIndex]
                               .getReadPointer(route.sourceChannel)
        float* dst = slot.node->getModBuffer(route.destParamIndex)
        for s in 0..numSamples-1:
            dst[s] += src[s] * d                   // plain float — vectorizable

    // 4. Clamp to [0.0, 1.0]
    for each unique destParamIndex in slot.modRoutes:
        float* buf = slot.node->getModBuffer(destParamIndex)
        for s in 0..numSamples-1:
            buf[s] = clamp(buf[s], 0.0, 1.0)

    // --- Existing: resolve audio input, call process() ---
    ...
    slot.node->process(ctx)
```

**Performance**: Only parameters with active mod connections are buffered. Unmodulated parameters skip all buffer operations. The depth atomic is loaded once per route per block (not per sample), so the inner multiply-accumulate loop uses plain floats and is vectorizable by the compiler.

## Combine Semantics

Additive. All modulation contributions are summed:

```
final[sample] = clamp(base + sum(source_i[sample] * depth_i), 0.0, 1.0)
```

- `base`: the scalar value set via `setParameter()` (normalized 0.0–1.0)
- `source_i[sample]`: the i-th modulation source's output at this sample
- `depth_i`: the i-th mod connection's depth value
- Clamped to [0.0, 1.0] after summation

**Modulation source conventions** (not enforced, but expected):
- Bipolar sources (LFOs): output -1.0 to +1.0
- Unipolar sources (envelopes): output 0.0 to 1.0

**Example**: Base = 0.5, bipolar LFO with depth 0.3 → sweeps 0.2 to 0.8.

**Replace semantics** are achieved by setting base = 0.0 and depth = 1.0 on a unipolar source.

**Design note on clamping**: Additive semantics with clamping mean that high base values combined with positive modulation will saturate. For example, base = 0.8 with a unipolar envelope (0→1) at depth 0.5 produces a range of 0.8→1.0 (not 0.8→1.3) — the top 60% of the envelope is lost to clamping. Users must set the base value low enough to accommodate the modulation range. This is the standard behavior in most modular synths and is documented here so the trade-off is understood. The Phase 2 multiply combine mode offers an alternative that scales around the base value.

## VST3 Adapter (PluginNode)

PluginNode continuous parameters are `modulatable = true`. Boolean and discrete (choice/enum) plugin parameters are `modulatable = false`. PluginNode sets this automatically based on `ParameterDescriptor::numSteps`: 0 (continuous) → modulatable, >0 (discrete/boolean) → not modulatable.

PluginNode cannot consume per-sample mod buffers directly — the hosted plugin controls its own DSP. When a PluginNode parameter has active modulation, `PluginNode::process()`:

1. Reads the mod buffer (filled by the render loop, same as any node)
2. Sets the plugin parameter at the beginning of the block using the first sample's value

**Phase 1 limitation**: This is effectively block-rate modulation. At 44.1 kHz / 512-sample blocks, the update rate is ~86 Hz. Fast modulation sources (e.g., audio-rate FM) will produce audible stairstepping on plugin parameters. This is acceptable for typical LFO-rate modulation. Phase 2 addresses this with breakpoint extraction.

Phase 2: Scan the mod buffer for inflection points, extract multiple breakpoints per block, and deliver via JUCE's `IParameterChanges` / `beginParameterChangeGesture` API for sub-block accuracy.

## Engine API

```cpp
// Modulation routing (control thread, serialized by controlMutex_)
int modRoute(int sourceNodeId, const std::string& sourcePort,
             int destNodeId, int destParamIndex,
             float depth, std::string& errorMessage);

int modRoute(int sourceNodeId, const std::string& sourcePort,
             int destNodeId, const std::string& destParamName,
             float depth, std::string& errorMessage);

bool modUnroute(int modConnectionId);

bool setModDepth(int modConnectionId, float depth);

std::vector<ModRouteInfo> getModRoutes() const;
```

`modRoute()` creates a persistent `ModConnection` (owned by Engine as `unique_ptr` in `ownedModConnections_`), delegates to `Graph::modConnect()`, then calls `updateGraph()` to rebuild the snapshot with the new mod routing. The `ModConnection::id` matches the `ModEdge::id` assigned by Graph.

`setModDepth()` writes directly to the `ModConnection::depth` atomic. No snapshot rebuild — the audio thread reads the new value on the next block via the snapshot's pointer. This makes depth changes as cheap as parameter changes.

`getModRoutes()` returns `vector<ModRouteInfo>` (plain data, no atomics) by snapshotting each `ModConnection`'s current depth value.

### Lua Bindings

```lua
-- Route modulation: LFO output → synth filter cutoff, depth 0.5
local conn_id = sq.mod_route(lfo.id, "out", synth.id, "filter_cutoff", 0.5)

-- Remove route
sq.mod_unroute(conn_id)

-- Change depth
sq.set_mod_depth(conn_id, 0.3)

-- List all mod routes (returns ModRouteInfo snapshots)
sq.mod_routes()  -- returns table of {id, src_id, src_port, dst_id, dst_param, depth}
```

### Node Removal

When a node is removed via `Engine::removeNode()`:
- `Graph::removeNode()` removes all mod edges where the node is source or destination (same `erase_if` pattern as audio connections)
- Engine removes the corresponding `ModConnection` objects via deferred deletion (two-phase: rebuild snapshot first, then queue for destruction after old snapshot is GC'd)
- Same overall pattern as audio connection cleanup

## Invariants

1. Mod connections are edges in the same DAG as audio connections — no cycles permitted
2. Modulation sources are always processed before their destinations (topological order)
3. Mod buffers are filled BEFORE `process()` is called on the destination node
4. Final modulated parameter values are clamped to [0.0, 1.0]
5. Multiple mod sources to the same parameter are summed (commutative, order-independent)
6. `getParameter()` always returns the base value (scalar), not the modulated value
7. Nodes without modulation-aware code continue to work unchanged (read scalar via `getParameter()`)
8. No allocation on the audio thread — all buffers pre-allocated in `prepareModulation()`
9. A modulation source's audio output can simultaneously feed audio connections and mod connections
10. `prepareModulation()` is called after every `prepare()` and on block size changes — mod buffers are always correctly sized
11. Mod edge IDs (Graph) and ModConnection IDs (Engine) share the same value — one ID identifies a mod route across both layers

## Error Conditions

| Condition | Behavior |
|-----------|----------|
| Source node doesn't exist | `modConnect` returns -1, error message set |
| Source port doesn't exist or isn't audio output | Returns -1, error message |
| Dest node doesn't exist | Returns -1, error message |
| Dest param index invalid | Returns -1, error message |
| Dest param not modulatable | Returns -1, error message |
| Connection would create cycle | Returns -1, error message |
| Mod connection ID not found (unroute/setDepth) | Returns false |

## Thread Safety

| Operation | Thread | Notes |
|-----------|--------|-------|
| `Graph::modConnect/modDisconnect` | Control | Serialized by Engine's controlMutex_ |
| `Engine::modRoute/modUnroute` | Control | Acquires controlMutex_, rebuilds snapshot |
| `Engine::setModDepth` | Control | Writes to `ModConnection::depth` atomic — no snapshot rebuild |
| `buildSnapshot` (compile mod routes) | Control | Part of snapshot build |
| Fill mod buffers | Audio | Writes to pre-allocated Node buffers |
| Read `ModConnection::depth` | Audio | Via `route.depth->load(relaxed)` in render loop |
| `Node::readParam()` | Audio | Reads mod buffers or scalar |
| `prepareModulation()` | Control | Called after prepare(), and on block size change |

**Buffer pointer safety**: The render loop writes to `Node::modBuffers_` and then calls `process()` which reads them — both on the audio thread, sequential within the same block. No concurrent access.

**Depth value delivery**: Depth is an `std::atomic<float>` on the persistent `ModConnection` object owned by Engine. The control thread writes via `store(relaxed)`, the audio thread reads via `load(relaxed)` through a pointer in the snapshot's `ModRoute`. No snapshot rebuild needed for depth changes — the new value takes effect on the next audio block. Relaxed ordering is sufficient because depth is an independent value with no ordering dependencies on other state.

## Does NOT Handle

- **Specific modulation sources**: LFO, envelope, automation curve nodes are separate specs
- **Multiply / ring-mod combine modes**: Phase 2
- **Per-sample depth modulation**: depth is constant per block (single atomic read)
- **Modulation of non-parameter targets** (e.g., modulating buffer position, tempo)
- **MIDI-to-modulation conversion**: separate component if needed
- **Parameter smoothing**: nodes handle their own internal smoothing if desired
- **Modulation UI/visualization**: application layer concern
- **Source channel selection**: Phase 1 always uses channel 0 of the source output port
- **SamplerNode per-sample modulation**: SamplerNode currently shares a `SamplerParams` struct by reference with `VoiceAllocator`/`SamplerVoice`, which read params once per render block, not per sample. Making SamplerNode modulation-aware requires either (a) passing mod buffers down to the voice render loop, or (b) splitting blocks at param change points (similar to the MIDI sub-block split pattern). This is deferred to a follow-up task. SamplerNode parameters will be `modulatable = false` in Phase 1.

## Dependencies

- `Node` (base class — extended with mod buffer infrastructure)
- `Graph` (extended with mod connections, cycle detection, topological sort)
- `GraphSnapshot` (extended with per-slot mod routing)
- `Engine` (render loop changes, API surface)
- No new external dependencies

## Example Usage

### Mod-aware Node (process loop)

```cpp
void RecorderNode::process(ProcessContext& ctx) {
    // Pass-through
    for (int ch = 0; ch < ctx.outputAudio.getNumChannels(); ++ch)
        ctx.outputAudio.copyFrom(ch, 0, ctx.inputAudio, ch, 0, ctx.numSamples);

    if (!buffer_ || !recEnabled_) return;

    for (int s = 0; s < ctx.numSamples; ++s) {
        // Per-sample modulated gain — uses buffer if modulated, scalar if not
        float gain = readParam(PARAM_INPUT_GAIN, s);
        float mappedGain = gain * 2.0f;  // normalized → physical

        for (int ch = 0; ch < buffer_->getNumChannels(); ++ch) {
            float sample = ctx.inputAudio.getSample(std::min(ch, ctx.inputAudio.getNumChannels() - 1), s);
            buffer_->getWritePointer(ch)[writePos_] = sample * mappedGain;
        }
        writePos_++;
        // ... handle buffer end ...
    }
}
```

### Lua

```lua
-- Create an LFO node
local lfo = sq.add_lfo("tremolo", { shape = "sine", rate = 4.0 })

-- Create a sampler
local smp = sq.add_sampler("pad")
smp:set_buffer(buf_id)

-- Modulate the sampler's volume with the LFO
local mod = sq.mod_route(lfo.id, "out", smp.id, "volume", 0.3)

-- Change modulation depth
sq.set_mod_depth(mod, 0.5)

-- Remove modulation
sq.mod_unroute(mod)
```

## Phase 2 Candidates

- **Multiply combine mode**: `final = base * (1 + sum(signal_i * depth_i))`
- **Per-sample depth modulation**: depth itself driven by another mod source
- **VST3 breakpoint extraction**: scan mod buffer for inflection points, deliver as `IParameterChanges`
- **Modulation source built-ins**: LFO, envelope follower, slew limiter, sample-and-hold
- **Automation curve node**: reads breakpoint data from a timeline, outputs per-sample values
- **Macro controls**: one parameter driving multiple mod routes with different depths
- **Mod matrix UI**: visual routing of modulation sources to parameters
- **Source channel selection**: allow choosing which channel of a stereo output port to use as the mod signal
- **SamplerNode modulation**: pass mod buffers into VoiceAllocator/SamplerVoice render loop, or split blocks at parameter change points
