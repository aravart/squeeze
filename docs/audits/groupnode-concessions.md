# GroupNode Concessions Audit

**Date:** 2026-02-17
**Scope:** Analysis of design concessions made throughout specs, architecture, and API surface to accommodate GroupNode (Phase 3, tier 12 — not yet implemented).

---

## Overview

GroupNode is a hierarchical composition primitive that allows nesting subgraphs inside a single node. It is architecturally central — the specs and architecture treat it as a first-class structural element — yet the tracker discussion classifies it as tier 4 "Nice to have." This audit catalogs every concession made to support GroupNode and evaluates what the system would look like without it.

---

## Concessions

### 1. Global IdAllocator (instead of local sequential IDs)

**Where:** `ARCHITECTURE.md` (lines 63–64), `specs/Engine.md` (lines 5, 103), `specs/GroupNode.md` (lines 92–100), `specs/Graph.md` (lines 80–82, 173, 201–202)

**What:** Engine owns a single globally-shared `IdAllocator`. All node IDs are unique across every nesting level so that any node — regardless of depth — can be addressed by a single flat ID:

```c
sq_set_param(engine, any_node_id, "cutoff", 0.5);  // works at any depth
```

GroupNode receives a reference to this allocator at construction.

**Without GroupNode:** Graph could assign local sequential IDs with a simple `nextNodeId_++` counter (which is what the current Engine implementation actually does). No shared allocator, no cross-graph uniqueness constraint.

**Affected components:** Engine, Graph

---

### 2. Graph as a Reusable, Non-Owning Component

**Where:** `specs/Graph.md` (lines 8, 13–14, 60–61, 196–202), `ARCHITECTURE.md` (lines 135–138)

**What:** Graph is designed as a standalone topology data structure usable by both Engine (top-level) and GroupNode (subgraph). It deliberately does not own nodes — it holds raw pointers — so that either Engine or GroupNode can manage node lifetimes. The spec states: "Both Engine and GroupNode own a Graph instance. The API is identical at both levels."

Graph's "Does NOT Handle" section explicitly calls out:
- Node ownership or lifetime (caller — Engine or GroupNode)
- ID allocation (caller provides IDs from Engine's IdAllocator)

**Without GroupNode:** Graph could be an Engine-internal detail, tightly coupled, owning its nodes directly. No need for the non-owning pointer design or the caller-provided-ID contract.

**Affected components:** Graph

---

### 3. Dynamic Ports on the Node Interface

**Where:** `ARCHITECTURE.md` (lines 331–332), `specs/Node.md` (lines 89–93, 171), `specs/Port.md` (lines 88, 107)

**What:** `getInputPorts()` and `getOutputPorts()` return `std::vector<PortDescriptor>` — a dynamically-sized, mutable return. The Node spec notes: "Most nodes declare fixed ports at construction. GroupNode supports dynamic ports via exportPort() / unexportPort(), so its return values can change over time."

The Port spec's invariant section states: "A node's port list is typically fixed at construction, but **may change at runtime** (control thread only). GroupNode uses this to dynamically export/unexport internal ports."

The thread safety model is complicated: Node spec annotates `getInputPorts()`/`getOutputPorts()` as "Mutable for GroupNode; audio thread reads only via snapshot."

**Without GroupNode:** Ports would be declared at construction and immutable. They could be returned as `const` references or stored in fixed-size arrays. The thread safety model would be simpler — no need to distinguish "control thread writes" from "audio thread reads via snapshot" for port queries.

**Affected components:** Node, Port, thread safety model

---

### 4. Cascading Topology Change Detection

**Where:** `specs/Engine.md` (lines 16, 121, 349–351), `specs/GroupNode.md` (lines 134–140), `ARCHITECTURE.md` (lines 325–326)

**What:** Engine has a `checkForCascadingChanges()` private method. When a GroupNode's external ports change (via `unexportPort()` or removal of an internal node with exported ports), Engine must detect this and auto-disconnect parent-graph connections referencing removed ports.

The GroupNode spec elaborates: "Unexporting a port (or removing an internal node that has exported ports) changes the group's external port list. The Engine is responsible for detecting this and auto-disconnecting any parent-graph connections that reference the removed external port. This cascade crosses the encapsulation boundary by design."

**Without GroupNode:** No "port unexport" concept, no cascading changes from internal mutations to external connections. Port removal only happens on node removal (already handled by Graph's auto-disconnect on `removeNode()`). Engine needs no `checkForCascadingChanges()` logic.

**Affected components:** Engine

---

### 5. Flat Global Parameter Access Across Nesting

**Where:** `specs/GroupNode.md` (lines 96–100, 167–174, 269), `specs/Engine.md` (lines 62–66)

**What:** Engine's parameter functions (`getParameter`, `setParameter`, etc.) accept a node ID and look up the node directly regardless of nesting depth:

```c
sq_set_param(engine, group_id, "master_gain", 0.8);  // group's own param
sq_set_param(engine, eq_id, "frequency", 0.5);        // nested node (global ID)
```

Engine must maintain a flat map of all nodes across all nesting levels, or recursively traverse into GroupNodes to find nodes by ID.

**Without GroupNode:** `getNode(id)` is a simple O(1) map lookup in one flat map of top-level nodes. No hierarchy traversal or global registry.

**Affected components:** Engine (parameter API, node storage)

---

### 6. Doubled C ABI Surface (`sq_group_*` mirrors)

**Where:** `specs/GroupNode.md` (lines 238–267), `specs/Graph.md` C ABI section

**What:** Every topology operation has a `sq_group_*` mirror:

| Engine | GroupNode |
|--------|-----------|
| `sq_add_plugin` | `sq_group_add_plugin` |
| `sq_add_sampler` | `sq_group_add_sampler` |
| `sq_add_group` | `sq_group_add_group` |
| `sq_connect` | `sq_group_connect` |
| `sq_disconnect` | `sq_group_disconnect` |
| `sq_remove_node` | `sq_group_remove_node` |
| `sq_connections` | `sq_group_connections` |
| `sq_nodes` | `sq_group_nodes` |

This doubles the C ABI surface for graph management. It also doubles the low-level Python wrapper methods and the high-level Python API.

**Without GroupNode:** One set of topology functions. Python wrapper is half the size for structural operations.

**Affected components:** `squeeze_ffi.h`, `squeeze_ffi.cpp`, `_low_level.py`, high-level Python modules

---

### 7. "Explicit Routing at Every Level" Principle

**Where:** `ARCHITECTURE.md` (lines 318–326), `specs/GroupNode.md` (line 161)

**What:** The architecture's "no auto-summing" rule is extended to GroupNode: "This applies uniformly to Engine and GroupNode — all routing is explicit at every level." GroupNode spec reinforces: "No auto-summing. Internal nodes whose audio outputs are not consumed by any internal connection and are not exported are simply unused."

**Without GroupNode:** The principle would still exist for the top-level graph, but wouldn't need to be stated as a recursive invariant. "At every level" simplifies to "at the one level."

**Affected components:** Architecture-wide design principle

---

### 8. Port Export/Unexport Mechanism

**Where:** `specs/GroupNode.md` (lines 108–141), `specs/Port.md` (lines 88, 127–131)

**What:** The `exportInput()` / `exportOutput()` / `unexportPort()` API exists solely for GroupNode. It makes internal ports visible to the parent graph. Port naming conventions include numbered inputs for groups (`"in_1"`, `"in_2"`) specifically for GroupNode mixer/bus patterns.

```cpp
mixerGroup.exportPort(newGainNode, "in", "in_3");
graph.connect(synthId, "out", mixerGroup.getId(), "in_3");
```

**Without GroupNode:** No export mechanism. All ports are fixed at construction. Port naming doesn't need group-specific conventions.

**Affected components:** Port, GroupNode

---

### 9. Structural Change Invalidation Pattern

**Where:** `specs/GroupNode.md` (lines 179–183), `specs/MidiSplitterNode.md` (lines 134–135)

**What:** GroupNode introduces the invariant: "Any structural change invalidates [the node] for audio processing — `prepare()` must be called before the next `process()`." This pattern propagates to other nodes; MidiSplitterNode explicitly borrows it: "Adding or removing outputs is a structural change — same invariant as GroupNode."

**Without GroupNode:** Nodes with fixed ports don't need per-node re-preparation after topology changes. Engine's snapshot rebuild handles all buffer reallocation. The invalidation pattern would only exist implicitly in Engine's snapshot mechanism, not as an explicit per-node invariant that spreads to other node types.

**Affected components:** GroupNode, MidiSplitterNode, any future dynamic-port nodes

---

### 10. GroupNode as Universal Composition Primitive (No Dedicated Composite Nodes)

**Where:** `ARCHITECTURE.md` (lines 138, 327–329), `specs/GroupNode.md` (lines 11–16, 300–324), `specs/MidiSplitterNode.md` (lines 230–263)

**What:** The architecture states: "GroupNode is a core graph primitive — composite node types (mixers, channel strips, kits) are GroupNode configurations, not separate node classes." Rather than building MixerNode, ChannelStripNode, DrumKitNode, the system relies on GroupNode as the universal composition mechanism.

The MidiSplitterNode spec's canonical example is a drum kit built as a GroupNode containing a MidiSplitterNode + three SamplerNodes (lines 230–263).

**Without GroupNode:** Each composite pattern would be its own node class with hardcoded behavior. More classes but each one is simpler. Trade-off: per-pattern implementation effort vs. one universal (but complex) composition mechanism.

**Affected components:** Architecture, node type hierarchy, MidiSplitterNode

---

### 11. PDC Must Handle Recursive Subgraph Latency

**Where:** `specs/PDC.md` (lines 19, 57–59, 335–336, 347–348)

**What:** The PDC spec accounts for GroupNode: "GroupNode returns its max internal path latency (computed the same way Engine computes total latency — walk internal execution order, propagate cumulative latencies, return the max path latency to any exported output node)."

GroupNode must override `getLatencySamples()` to recursively compute its internal graph's maximum path latency. The thread safety table confirms: "GroupNode::getLatencySamples() — Control — Walks internal graph (control thread only)."

**Without GroupNode:** `getLatencySamples()` would only need to be overridden by PluginNode (returning its reported latency). No recursive graph walking. PDC algorithm processes one flat graph only.

**Affected components:** Node (`getLatencySamples()` virtual), PDC algorithm, GroupNode

---

### 12. Engine Infrastructure Must Span Hierarchy Levels

**Where:** `specs/GroupNode.md` (lines 207–215)

**What:** GroupNode's "Does NOT Handle" section lists everything delegated to the parent Engine:

- Transport (uses parent Engine's transport)
- CommandQueue / command queuing (parent Engine handles atomicity)
- EventScheduler / sample-accurate automation (parent Engine handles sub-block splitting)
- Deferred deletion (parent Engine's snapshot swap handles this)
- `controlMutex_` (shares parent Engine's lock)
- PerfMonitor (parent Engine monitors overall performance)

Engine must be designed so all these services work transparently across nesting levels. Sub-block parameter splitting must reach nodes inside GroupNodes. Deferred deletion must handle nested node lifetimes.

**Without GroupNode:** All Engine infrastructure serves one flat level. No consideration for how sub-block splitting interacts with nested processing, or how deferred deletion handles nested lifetimes.

**Affected components:** Engine, Transport, CommandQueue, EventScheduler, PerfMonitor

---

### 13. MidiSplitterNode Designed for GroupNode Use Cases

**Where:** `specs/MidiSplitterNode.md` (lines 8, 10–11, 230–263)

**What:** MidiSplitterNode's spec states: "It is the primary mechanism for MIDI fan-out inside GroupNodes (e.g., routing one MIDI input to multiple samplers in a drum kit)." The canonical example is a drum kit GroupNode. It also has a GroupNode-specific C ABI function: `sq_group_add_midi_splitter()`.

MidiSplitterNode uses dynamic ports, borrowing the pattern established for GroupNode.

**Without GroupNode:** MIDI fan-out can be handled by MidiRouter's per-route note filtering, which already exists. MidiSplitterNode's graph-level splitting role is primarily needed inside GroupNodes where MidiRouter doesn't operate. Without GroupNode, MidiRouter alone could handle all MIDI fan-out. MidiSplitterNode might still be useful as a simpler "copy MIDI to N outputs" node, but its design wouldn't need dynamic ports or the structural-change invalidation pattern.

**Affected components:** MidiSplitterNode

---

### 14. Dedicated Build Phase for GroupNode

**Where:** `ARCHITECTURE.md` (lines 132–138)

**What:** GroupNode has its own phase (Phase 3, tier 12) positioned after the POC milestone and before Transport/EventScheduler. The architecture justifies: "Built early to ensure the subgraph model is solid before adding more features on top." GroupNode is the only tier in Phase 3.

**Without GroupNode:** Phase 3 disappears. Development proceeds directly from the plugin-playback POC (Phase 2) to Transport & Scheduling (Phase 4).

**Affected components:** Development timeline

---

### 15. Priority Tension Between Architecture and Tracker

**Where:** `docs/discussion/tracker.md` (lines 55–57), `ARCHITECTURE.md` Phase 3

**What:** The tracker discussion classifies GroupNode as tier 4 "Nice to have": "Channel strips, sub-mixes, effects racks. Not blocking, but makes complex routing manageable." The architecture document places it in Phase 3 — before Transport. These two documents disagree on GroupNode's urgency.

**Affected components:** Planning, prioritization

---

## Impact Summary

| # | Concession | Components Affected | Severity |
|---|---|---|---|
| 1 | Global IdAllocator | Engine, Graph | Medium |
| 2 | Graph as reusable non-owning layer | Graph | High |
| 3 | Dynamic ports on Node interface | Node, Port, thread model | High |
| 4 | Cascading topology change detection | Engine | Medium |
| 5 | Flat global parameter access across nesting | Engine params API | Medium |
| 6 | Doubled C ABI surface | FFI, Python wrappers | High |
| 7 | "Explicit routing at every level" principle | Architecture-wide | Low |
| 8 | Port export/unexport mechanism | Port, GroupNode | Medium |
| 9 | Structural change invalidation pattern | GroupNode, MidiSplitterNode | Low |
| 10 | Universal composition primitive | Architecture, node hierarchy | High |
| 11 | Recursive PDC for subgraph latency | Node, PDC | Medium |
| 12 | Engine infrastructure spans hierarchy | Engine, Transport, Scheduler | High |
| 13 | MidiSplitterNode designed for GroupNode | MidiSplitterNode | Low |
| 14 | Dedicated build phase | Development timeline | Low |
| 15 | Priority tension | Planning | Low |

---

## What Would Be Gained by Dropping GroupNode

| Area | Simplification |
|------|---------------|
| **Graph** | Becomes Engine-internal, owns nodes, no shared IdAllocator |
| **Node** | Ports immutable after construction, simpler thread model |
| **C ABI** | ~10 fewer `sq_group_*` functions, half the topology API |
| **Python** | Proportionally smaller wrapper surface |
| **Engine** | No cascade detection, no hierarchy-aware parameter lookup, simpler PDC |
| **Build order** | Skip Phase 3, go straight to Transport |
| **MidiSplitterNode** | Possibly unnecessary (MidiRouter covers the use case) |

---

## What Would Be Lost by Dropping GroupNode

- **Composable sub-graphs** — channel strips, effects racks, mixer buses, drum kits as reusable units
- **"One primitive, many patterns" philosophy** — without GroupNode, composite patterns require dedicated node classes (MixerNode, ChannelStripNode, DrumKitNode, etc.)
- **Hierarchical organization** — complex sessions with 50+ nodes become harder to manage as a flat graph
- **Encapsulation** — no way to hide internal routing details behind a clean external port interface

---

## Current Implementation Status

The concessions exist **primarily at the spec/architecture level**. The actual implementation has not yet incorporated most of them:

- Engine uses a simple `nextNodeId_` counter (not an IdAllocator)
- No `checkForCascadingChanges()` method exists
- `getLatencySamples()` does not yet exist on Node
- No `sq_group_*` functions exist in the C ABI
- Ports are currently immutable in practice

This means **now is the ideal time** to decide whether GroupNode stays or goes — the implementation cost of the concessions has not yet been paid.
