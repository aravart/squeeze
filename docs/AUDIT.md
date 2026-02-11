# Documentation & Architecture Audit

_2026-02-10_

## Inventory

### Implemented & Spec'd (18)

| Component | Spec | Tests | Notes |
|-----------|------|-------|-------|
| Port | Port.md | PortTests.cpp | No dependencies |
| Node | Node.md | NodeTests.cpp | Depends on Port |
| Graph | Graph.md | GraphTests.cpp | Depends on Node, Port |
| Scheduler | Scheduler.md | SchedulerTests.cpp | SPSC queue to audio thread |
| Engine | Engine.md | EngineTests.cpp | Owns Graph, PluginCache, MidiRouter, Buffers |
| Transport | Transport.md | — | Basic playback + looping |
| PluginNode | PluginNode.md | PluginNodeTests.cpp | Includes PluginCache |
| LuaBindings | LuaBindings.md | LuaBindingsTests.cpp | Thin delegate over Engine |
| Logger | Logger.md | LoggerTests.cpp | warn/debug/trace levels |
| Parameters | Parameters.md | — | Phase 1: index-based, descriptors, text |
| PerfMonitor | PerfMonitor.md | PerfMonitorTests.cpp | SeqLock publish, xrun detection |
| PluginEditorWindow | PluginEditorWindow.md | — | App-layer GUI hosting |
| MidiRouter | MidiRouter.md | MidiRouterTests.cpp | Replaced MidiInputNode |
| SamplerVoice | SamplerVoice.md | SamplerVoiceTests.cpp | DSP core, 71 tests |
| VoiceAllocator | VoiceAllocator.md | VoiceAllocatorTests.cpp | Mono mode only (Phase 1) |
| SamplerNode | SamplerNode.md | SamplerNodeTests.cpp | 32 params, sub-block MIDI |
| ConcurrencyModel | ConcurrencyModel.md | — | Architectural decision doc |
| SampleAccurateDispatch | SampleAccurateDispatch.md | — | Design doc for EventQueue |

### Spec'd But Not Built (3)

| Component | Spec | Notes |
|-----------|------|-------|
| EventQueue | EventQueue.md | Beat-timestamped scheduling, sample-accurate dispatch |
| Modulation | Modulation.md | Audio-rate parameter modulation routing (untracked) |
| RecorderNode | RecorderNode.md | Record to Buffer, one-shot/loop modes (untracked) |

### Implemented But Not Spec'd (1)

| Component | Notes |
|-----------|-------|
| **Buffer** | Factory (`loadFromFile`, `createEmpty`), Engine integration, atomic writePosition, deferred deletion. Referenced by 5+ specs. Needs `docs/specs/Buffer.md`. |

### Deprecated (1)

| Component | Notes |
|-----------|-------|
| MidiInputNode | Replaced by MidiRouter. Spec kept for history. |

---

## Missing / Underspecified Components

### No Spec, No Implementation

#### Meters
Mentioned in ARCHITECTURE.md build order (#7) but nothing exists. Would cover:
- Tap/untap API on nodes
- Peak/RMS level computation
- Scope buffers for waveform display
- Observable state pattern for UI

#### Modulation Source Nodes (LFO, Envelope Generator, Step Sequencer)
The Modulation spec defines the routing framework but explicitly defers source nodes to "separate specs." At minimum an LFO node and an envelope generator node are needed to make modulation useful.

#### State Persistence / Session Save
No spec for serializing/deserializing engine state: node graph topology, parameter values, buffer references, MIDI routes, modulation routing. Required for any real session workflow.

#### ServerAPI / WebSocketServer / OSCServer
In the ARCHITECTURE.md build order (#11-13) but no specs exist. These form the remote-control layer for external clients.

#### Undo/Redo
Not mentioned in any spec. Needed for interactive workflows where users build patches incrementally.

### Partially Specified / Phase 2 Stubs

#### VoiceAllocator Poly/Legato Modes
Spec and implementation have Phase 2 stubs for polyphonic and legato allocation. Currently only mono mode works. Polyphonic mode is essential for a usable sampler.

#### Transport Automation / Tempo Maps
Transport spec covers basic playback and looping. No spec for:
- Tempo ramps / automation
- Time signature changes
- Host sync beyond AudioPlayHead

#### Disk Streaming
SamplerVoice loads entire samples into memory. No spec for streaming large files from disk. Mentioned as future work in the SamplerNode goals doc.

---

## Priority Assessment

### High — Blocks core workflows

| Component | Status | Why |
|-----------|--------|-----|
| Buffer spec (backfill) | Implemented, no spec | Technical debt; 5+ specs reference it |
| EventQueue | Spec ready | Blocks tempo-synced sequencing |
| Modulation framework | Spec ready | Blocks expressive parameter control |
| Poly/legato voice modes | Stub exists | Blocks polyphonic sampler use |

### Medium — Blocks important use cases

| Component | Status | Why |
|-----------|--------|-----|
| LFO / EnvGen nodes | No spec | Makes modulation framework actually usable |
| Meters | No spec | Blocks mixing and monitoring workflows |
| RecorderNode | Spec ready | Blocks live sampling / looping |
| State persistence | No spec | Blocks saving and recalling sessions |

### Low — Extensions and infrastructure

| Component | Status | Why |
|-----------|--------|-----|
| Disk streaming | No spec | Only needed for large sample libraries |
| Transport automation | No spec | Only for complex arrangements |
| ServerAPI / WS / OSC | No spec | Remote control layer |
| Undo/redo | No spec | Quality-of-life for interactive use |

---

## Spec Quality Notes

- All 22 specs follow the template from CLAUDE.md consistently
- Thread safety is rigorously documented across all components
- "Does NOT Handle" sections prevent scope creep effectively
- Cross-references between specs are accurate and consistent
- No orphaned TODOs or incomplete sections in any spec
- RT-safety constraints are repeated appropriately at component boundaries

---

## Summary

The core is solid: 18 components built with rigorous specs, 471 test cases, 116K assertions passing. The three biggest gaps toward feature-complete:

1. **EventQueue + Modulation pipeline** — specs exist, ready to build
2. **Modulation source nodes** (LFO, EnvGen) — need specs
3. **State persistence** — needs spec

Everything else is either deferred infrastructure (server APIs) or incremental extensions to existing components (poly voices, disk streaming, meters).
