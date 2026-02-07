# Audio Engine Architecture

## Overview

A modern, modular audio engine for hosting VST/AU plugins and routing audio through programmable graphs. Designed with explicit connections (not anonymous buses), automatic execution ordering, embedded Lua scripting, and push-based client communication.

This document defines the high-level architecture, component responsibilities, and boundaries. It serves as the north star for implementation—individual component specifications derive from this document.

## Design Principles

1. **Connections are the API, topology is derived.** Users declare data flow between nodes; the engine computes execution order automatically.

2. **Explicit over implicit.** Named, typed ports with explicit connections. No global buses. Errors caught at connection time, not runtime.

3. **Push, not poll.** Clients subscribe to state changes. The server broadcasts updates at appropriate cadences.

4. **Embedded scripting.** Lua runs in-process for configuration, debugging, and live interaction. Network protocols exist for remote clients.

5. **Realtime safety.** The audio thread never blocks, allocates, or waits. All mutations flow through lock-free queues.

6. **Modular and testable.** Each component has clear boundaries, can be tested in isolation, and mocked at interfaces.

---

## System Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Client Layer                                │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                  │
│  │  Lua REPL   │  │  WebSocket  │  │  OSC Client │                  │
│  │  (local)    │  │  (remote)   │  │  (compat)   │                  │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘                  │
└─────────┼────────────────┼────────────────┼─────────────────────────┘
          │                │                │
          └────────────────┼────────────────┘
                           │
          ┌────────────────▼────────────────┐
          │          Server API             │
          │  (commands, queries, subscriptions)
          └────────────────┬────────────────┘
                           │
┌──────────────────────────┼──────────────────────────────────────────┐
│                          │          Control Thread                  │
│  ┌───────────────────────▼───────────────────────────────────────┐  │
│  │                      Lua VM                                   │  │
│  │  (configuration, scripting, subscription logic)               │  │
│  └───────────────────────┬───────────────────────────────────────┘  │
│                          │                                          │
│  ┌───────────────────────▼───────────────────────────────────────┐  │
│  │                    Scheduler                                  │  │
│  │  (queues commands for audio thread, lock-free SPSC)           │  │
│  └───────────────────────┬───────────────────────────────────────┘  │
└──────────────────────────┼──────────────────────────────────────────┘
                           │ lock-free queue
┌──────────────────────────┼──────────────────────────────────────────┐
│                          │          Audio Thread                    │
│  ┌───────────────────────▼───────────────────────────────────────┐  │
│  │                      Engine                                   │  │
│  │  (owns audio callback, orchestrates processing)               │  │
│  │                                                               │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐            │  │
│  │  │  Transport  │  │    Graph    │  │   Meters    │            │  │
│  │  │  (time,     │  │  (nodes,    │  │  (levels,   │            │  │
│  │  │   tempo,    │  │   conns,    │  │   scopes)   │            │  │
│  │  │   position) │  │   order)    │  │             │            │  │
│  │  └─────────────┘  └──────┬──────┘  └─────────────┘            │  │
│  │                          │                                    │  │
│  │         ┌────────────────┼────────────────┐                   │  │
│  │         │                │                │                   │  │
│  │    ┌────▼────┐     ┌─────▼─────┐    ┌─────▼─────┐             │  │
│  │    │  Node   │     │   Node    │    │   Node    │             │  │
│  │    │ (gain)  │────▶│  (VST)    │───▶│  (mixer)  │             │  │
│  │    └─────────┘     └───────────┘    └───────────┘             │  │
│  │                                                               │  │
│  └───────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
                           │
          ┌────────────────▼────────────────┐
          │     JUCE AudioDeviceManager     │
          │     (platform audio I/O)        │
          └─────────────────────────────────┘
```

---

## Components

### Port

**Responsibility:** Represents a connection point on a node.

**Properties:**
- Direction: input or output
- Signal type: audio or control
- Channel count: 1 (mono), 2 (stereo), or N
- Name: human-readable identifier

**Does NOT handle:**
- Actual audio data (that's buffers managed by Engine)
- Connection logic (that's Graph)

---

### Node

**Responsibility:** Abstract interface for anything that processes audio.

**Interface:**
- `prepare(sampleRate, blockSize)` — called before processing begins
- `process(inputBuffers, outputBuffers, numSamples, context)` — called each block
- `release()` — called when removed from graph
- `getInputPorts()` / `getOutputPorts()` — declare I/O
- `getParameter(name)` / `setParameter(name, value)` — runtime parameter access

**Invariants:**
- `process()` is realtime-safe: no allocation, no blocking, no unbounded work
- Port configuration is static after construction (no dynamic port creation)

**Does NOT handle:**
- Its position in execution order (that's Graph)
- Buffer allocation (that's Engine)

**Implementations:**
- `GainNode`, `MixerNode`, `PassthroughNode` — basic DSP
- `PluginNode` — wraps VST3/AU via JUCE
- `MeterNode` — computes levels, writes to observable state

---

### Graph

**Responsibility:** Manages nodes and connections, computes execution order.

**Interface:**
- `addNode(node) -> nodeId`
- `removeNode(nodeId)`
- `connect(sourcePort, destPort) -> connectionId | error`
- `disconnect(connectionId)`
- `getExecutionOrder() -> ordered list of nodeIds`
- `getNode(nodeId) -> node`
- `getConnections() -> list of connections`

**Invariants:**
- Execution order is always valid (topologically sorted)
- Cycles are rejected at connection time
- Removing a node removes its connections
- Type/channel mismatches are rejected at connection time

**Does NOT handle:**
- Audio processing (that's Engine)
- Thread safety for mutations (that's Scheduler)
- Buffer allocation (that's Engine)

---

### Scheduler

**Responsibility:** Safely passes commands from control thread to audio thread.

**Interface:**
- `schedule(command)` — queue a command (control thread)
- `processPending()` — execute queued commands (audio thread)

**Command types:**
- `AddNode { node }`
- `RemoveNode { nodeId }`
- `Connect { sourcePort, destPort }`
- `Disconnect { connectionId }`
- `SetParameter { nodeId, paramName, value }`
- `TransportPlay`, `TransportStop`, `TransportSeek { position }`

**Invariants:**
- Lock-free: no mutexes, no blocking
- Commands execute in order
- Failed commands are reported back (via separate feedback queue)

**Does NOT handle:**
- What commands mean (that's Engine/Graph)
- Scheduling at specific times (that's a higher-level concern)

---

### Transport

**Responsibility:** Manages playback state, position, and tempo.

**State:**
- `state`: stopped, playing, paused
- `positionSamples`: current position in samples (ground truth)
- `positionBeats`: derived from samples + tempo
- `tempo`: BPM
- `timeSignature`: { numerator, denominator }

**Interface:**
- `play()`, `stop()`, `pause()`
- `seek(positionSamples)` or `seekBeats(beat)`
- `setTempo(bpm)`
- `advance(numSamples)` — called each block to update position

**Invariants:**
- Position always advances by exactly `numSamples` when playing
- Beat position is derived, never set directly

**Does NOT handle:**
- Tempo automation / tempo maps (future extension)
- MIDI clock output (separate component)

---

### Engine

**Responsibility:** Owns the audio callback, orchestrates all processing.

**Interface:**
- `setGraph(graph)`
- `getTransport() -> transport`
- `getScheduler() -> scheduler`
- `start()`, `stop()`
- `processBlock(numSamples)` — called by JUCE audio callback

**Per-block responsibilities:**
1. Process pending commands from Scheduler
2. Advance Transport
3. Allocate/assign intermediate buffers
4. Process nodes in Graph's execution order
5. Update Meters
6. Write output to device buffer

**Invariants:**
- `processBlock` is realtime-safe
- Buffer allocation happens once at prepare time, not per-block
- Node processing respects execution order from Graph

**Does NOT handle:**
- Graph topology (that's Graph)
- Command queueing (that's Scheduler)
- Platform audio I/O (that's JUCE)

---

### PluginNode

**Responsibility:** Wraps a VST3/AU plugin as a Node.

**Additional interface:**
- `loadPlugin(path)` or `loadPlugin(pluginDescription)`
- `getPluginParameters() -> list of parameter descriptors`
- `showEditor()` / `hideEditor()` — optional, for debugging

**MIDI handling:**
- Receives MIDI via a control input port or separate MIDI input
- MIDI events include sample offset for sample-accurate timing

**Invariants:**
- Plugin state (presets) can be serialized/deserialized
- Plugin processing happens within `process()`, respecting realtime constraints

**Does NOT handle:**
- Plugin scanning/discovery (separate utility)
- GUI hosting in production (headless design)

---

### Meters

**Responsibility:** Computes observable audio metrics.

**Metrics:**
- Peak levels per bus/node output
- RMS levels
- Scope buffers (ring buffers of recent samples)

**Interface:**
- `tap(nodeId, portId)` — start metering a port
- `untap(nodeId, portId)`
- `getLevels() -> map of nodeId/portId to {peak, rms}`
- `getScopeBuffer(nodeId, portId) -> recent samples`

**Update model:**
- Levels computed every block on audio thread
- Written to lock-free observable state
- Control thread reads at its own cadence

**Does NOT handle:**
- What to do with the data (that's client/subscription layer)

---

### Lua VM

**Responsibility:** Embedded scripting for configuration and live interaction.

**Exposed API:**
- All Server API functions (addNode, connect, transport control, etc.)
- Query functions (getNodes, getConnections, getParameters)
- Subscription registration
- Utility functions (load audio file, plugin scanning)

**Execution model:**
- Runs on control thread, never audio thread
- Commands to audio thread go through Scheduler
- Can register callbacks for events (block processed, parameter changed)

**Does NOT handle:**
- Network communication (separate layer)
- Audio processing

---

### Server API

**Responsibility:** Unified interface for all clients (Lua, WebSocket, OSC).

**Command categories:**
- Graph mutation (add/remove nodes, connect/disconnect)
- Parameter control (set/get parameters)
- Transport control (play, stop, seek, tempo)
- Queries (list nodes, get connections, get state)
- Subscriptions (subscribe to state changes)

**Subscription topics:**
- `/transport` — position, tempo, state
- `/node/{id}/params` — parameter changes
- `/node/{id}/levels` — metering
- `/health` — CPU, underruns
- `/graph` — topology changes

**Does NOT handle:**
- Wire protocol details (that's transport-specific: WebSocket, OSC, etc.)

---

## Data Flow Examples

### Adding a Node

```
Client                    Control Thread              Audio Thread
  │                             │                           │
  │  addNode(GainNode)          │                           │
  │────────────────────────────▶│                           │
  │                             │                           │
  │                             │  schedule(AddNode{...})   │
  │                             │──────────────────────────▶│
  │                             │                           │
  │                             │    (next audio block)     │
  │                             │                           │
  │                             │                           │  processPending()
  │                             │                           │  graph.addNode(...)
  │                             │                           │  recompute order
  │                             │                           │
  │                             │◀──────────────────────────│  ack via feedback queue
  │                             │                           │
  │◀────────────────────────────│  nodeId (or error)        │
  │                             │                           │
```

### Processing a Block

```
Engine.processBlock(512):
  1. scheduler.processPending()
       - execute any queued graph mutations
       - execute parameter changes

  2. transport.advance(512)
       - update position

  3. for each nodeId in graph.getExecutionOrder():
       - resolve input buffers (from connections or silence)
       - node.process(inputs, outputs, 512, context)
       - store outputs for downstream nodes

  4. meters.update()
       - compute peak/RMS from tapped nodes

  5. copy final outputs to device buffer
```

---

## Thread Model

| Thread | Responsibilities | Locks Engine Mutex? | Blocking Allowed |
|--------|------------------|---------------------|------------------|
| Audio | Engine.processBlock, Node.process | **Never** | NO |
| Control / REPL | Lua VM, Engine control-plane calls | Yes | Yes |
| Control / OSC | OSC message handling, Engine calls | Yes | Yes |
| Control / WebSocket | WebSocket request handling, Engine calls | Yes | Yes |
| MIDI callback | MidiInputNode SPSC write | **Never** | No |
| GUI / Message thread | Plugin editor windows | **Never** | Yes |
| Audio setup | audioDeviceAboutToStart | Yes | Yes |

**Communication:**
- Control → Audio: lock-free SPSC queue (Scheduler). Engine's `controlMutex_` ensures only one control thread calls `sendCommand()` at a time, preserving the SPSC single-producer invariant.
- Audio → Control: lock-free SPSC queue (feedback, meter data)
- Network → Engine: direct method calls, serialized by Engine's `controlMutex_`

**Lock ordering:** Engine `controlMutex_` must never be held when acquiring `MessageManagerLock`. See `docs/specs/ConcurrencyModel.md`.

---

## File Structure

```
audio-engine/
├── docs/
│   ├── ARCHITECTURE.md          # this document
│   └── specs/
│       ├── Port.md
│       ├── Node.md
│       ├── Graph.md
│       ├── Scheduler.md
│       ├── Transport.md
│       ├── Engine.md
│       ├── PluginNode.md
│       ├── Meters.md
│       └── ServerAPI.md
├── src/
│   ├── core/
│   │   ├── Port.h / Port.cpp
│   │   ├── Node.h / Node.cpp
│   │   ├── Graph.h / Graph.cpp
│   │   ├── Scheduler.h / Scheduler.cpp
│   │   ├── Transport.h / Transport.cpp
│   │   └── Engine.h / Engine.cpp
│   ├── nodes/
│   │   ├── GainNode.h / GainNode.cpp
│   │   ├── MixerNode.h / MixerNode.cpp
│   │   ├── PassthroughNode.h / PassthroughNode.cpp
│   │   └── PluginNode.h / PluginNode.cpp
│   ├── meters/
│   │   └── Meters.h / Meters.cpp
│   ├── scripting/
│   │   └── LuaBindings.h / LuaBindings.cpp
│   └── server/
│       ├── ServerAPI.h / ServerAPI.cpp
│       ├── WebSocketServer.h / WebSocketServer.cpp
│       └── OSCServer.h / OSCServer.cpp
├── tests/
│   ├── core/
│   │   ├── PortTests.cpp
│   │   ├── NodeTests.cpp
│   │   ├── GraphTests.cpp
│   │   ├── SchedulerTests.cpp
│   │   ├── TransportTests.cpp
│   │   └── EngineTests.cpp
│   ├── nodes/
│   │   └── ...
│   └── integration/
│       └── ...
├── scripts/
│   └── examples/          # example Lua scripts
├── CLAUDE.md              # AI assistant guidelines
└── CMakeLists.txt
```

---

## Dependencies

| Dependency | Purpose | License |
|------------|---------|---------|
| JUCE | Audio I/O, plugin hosting | GPLv3 / Commercial |
| LuaJIT | Embedded scripting | MIT |
| readerwriterqueue | Lock-free SPSC queue | BSD |
| Catch2 or GoogleTest | Testing | BSD / BSD |
| nlohmann/json | JSON for WebSocket protocol | MIT |
| libwebsockets or similar | WebSocket server | MIT / LGPL |

---

## Decisions Log

*Record significant architectural decisions here as the project evolves.*

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-02-07 | Engine control-plane mutex (Option B) | Each gateway calls Engine directly; Engine serializes with `std::mutex`. Simpler than a shared command queue, doesn't constrain gateway async semantics, negligible contention. |

---

## Future Extensions

Not in initial scope, but the architecture should not preclude:

- **Sample-accurate parameter automation**: Commands with sample offsets
- **Tempo maps**: Multiple tempo changes over time
- **MIDI routing**: MIDI as first-class signal type between nodes
- **Audio file players**: Nodes that stream from disk
- **Recording**: Capture node outputs to disk
- **Undo/redo**: Command history with inverse operations
- **State snapshots**: Save/restore entire graph state
- **Parallel processing**: Execute independent graph branches on multiple cores
