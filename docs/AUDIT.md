# Documentation & Architecture Audit

_2026-02-11_

## Inventory

### Implemented & Spec'd (21)

| Component | Spec | Tests | Notes |
|-----------|------|-------|-------|
| Port | Port.md | PortTests.cpp | No dependencies. Audio channel mismatch allowed. |
| Node | Node.md | NodeTests.cpp | Depends on Port |
| Graph | Graph.md | GraphTests.cpp | Depends on Node, Port. Audio-only connections. |
| Scheduler | Scheduler.md | SchedulerTests.cpp | SPSC queue to audio thread |
| Engine | Engine.md | EngineTests.cpp | Owns Graph, PluginCache, MidiRouter, Buffers, Transport, EventQueue |
| Transport | Transport.md | TransportTests.cpp | Playback state, musical position, AudioPlayHead, looping |
| Buffer | Buffer.md | BufferTests.cpp | Factory, Engine integration, deferred deletion |
| EventQueue | EventQueue.md | EventQueueTests.cpp | Beat-timestamped scheduling, sample-accurate dispatch, Engine+Lua integrated |
| PluginNode | PluginNode.md | PluginNodeTests.cpp | Includes PluginCache. Sidechain buffer sizing fixed. |
| LuaBindings | LuaBindings.md | LuaBindingsTests.cpp | Thin delegate over Engine |
| Logger | Logger.md | LoggerTests.cpp | off/warn/debug/trace levels |
| Parameters | Parameters.md | — | Phase 1: index-based, descriptors, text. Tested via Engine/SamplerNode. |
| PerfMonitor | PerfMonitor.md | PerfMonitorTests.cpp | SeqLock publish, xrun detection, MIDI device stats |
| PluginEditorWindow | PluginEditorWindow.md | — | App-layer GUI hosting |
| MidiRouter | MidiRouter.md | MidiRouterTests.cpp | Replaced MidiInputNode. Channel filtering. |
| SamplerVoice | SamplerVoice.md | SamplerVoiceTests.cpp | DSP core, 71 tests |
| VoiceAllocator | VoiceAllocator.md | VoiceAllocatorTests.cpp | Mono mode only (Phase 1) |
| SamplerNode | SamplerNode.md | SamplerNodeTests.cpp | 32 params, sub-block MIDI splitting |
| ConcurrencyModel | ConcurrencyModel.md | — | Architectural decision doc (controlMutex_ pattern) |
| SampleAccurateDispatch | SampleAccurateDispatch.md | — | Design doc for Engine sub-block splitting |
| SharedState | SharedState.md | — | Spec only (cross-thread observable state) |

### Spec'd But Not Built (3)

| Component | Spec | Notes |
|-----------|------|-------|
| Modulation | Modulation.md | Audio-rate parameter modulation routing framework |
| RecorderNode | RecorderNode.md | Record to Buffer, one-shot/loop modes |
| SharedState | SharedState.md | Cross-thread observable state for UI reads |

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
- Observable state pattern for UI (likely depends on SharedState)

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
- Time signature changes mid-timeline
- Host sync beyond AudioPlayHead

#### Disk Streaming
SamplerVoice loads entire samples into memory. No spec for streaming large files from disk. Mentioned as future work in the SamplerNode goals doc.

---

## Recent Fixes (2026-02-11)

These issues were discovered during Transit 2 demo script development:

- **Audio port channel matching relaxed**: `canConnect` no longer requires exact channel count match for audio ports (only MIDI). Fixes connections between nodes with mismatched channel counts (e.g., 2ch SamplerNode → 4ch Transit 2).
- **Plugin buffer sizing for sidechain inputs**: `buildSnapshot` now allocates `max(inputChannels, outputChannels)` per node instead of just output channels. Fixes silence with plugins that have more inputs than outputs (e.g., Transit 2: 4in/2out).
- **Same-beat event ordering**: EventQueue now sorts same-offset events by type priority: noteOff → cc → paramChange → noteOn. Prevents note-off from killing a just-triggered note-on at the same beat.

---

## Priority Assessment

### High — Blocks core workflows

| Component | Status | Why |
|-----------|--------|-----|
| Modulation framework | Spec ready | Blocks expressive parameter control |
| Poly/legato voice modes | Stub exists | Blocks polyphonic sampler use |
| SharedState | Spec ready | Blocks safe cross-thread reads for UI |

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

- All 24 specs follow the template from CLAUDE.md consistently
- Thread safety is rigorously documented across all components
- "Does NOT Handle" sections prevent scope creep effectively
- Cross-references between specs are accurate and consistent
- No orphaned TODOs or incomplete sections in any spec
- RT-safety constraints are repeated appropriately at component boundaries
- EventQueue spec updated with type priority sort ordering and invariant

---

## Summary

The core is solid: 21 components built with rigorous specs, 589 test cases, 123,942 assertions passing. Milestone 1 (VST plugins as nodes with MIDI routing) is complete. Beat-synced event scheduling with sample-accurate dispatch is fully operational.

The three biggest gaps toward feature-complete:

1. **Modulation pipeline** — framework spec exists, needs implementation + source node specs (LFO, EnvGen)
2. **Polyphonic voice allocation** — stubs exist in VoiceAllocator, needs Phase 2 implementation
3. **State persistence** — needs spec and implementation for session save/recall

Everything else is either deferred infrastructure (server APIs, SharedState) or incremental extensions to existing components (disk streaming, meters, transport automation).
