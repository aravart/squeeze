# Squeeze v2 — Architecture

## Overview

Squeeze v2 is a modular C++17 audio engine for hosting VST/AU plugins and routing audio through programmable graphs. It exposes a C ABI (`squeeze_ffi`) for multi-language integration — the host language (Python, Rust, etc.) provides the user-facing interface. All routing is explicit (named ports, no global buses), execution order is derived from topology, and the audio thread is strictly lock-free.

---

## Design Principles

1. **Connections are the API, topology is derived.** Users declare data flow between nodes via explicit `connect()` calls; the engine computes execution order automatically.

2. **Explicit over implicit.** Named, typed ports with explicit connections. No auto-summing. No global buses. Audio goes where you connect it, nowhere else. Errors caught at connection time, not runtime.

3. **Realtime safety.** The audio thread never blocks, allocates, or waits. All mutations flow through lock-free SPSC queues. Deferred deletion handles cleanup.

4. **Modular and testable.** Each component has clear boundaries, a specification, and can be tested in isolation. Engine is a focused processing kernel — peripheral concerns (audio device, plugin management, buffer library, MIDI devices) are separate components.

5. **Spec-first development.** Specification → tests → implementation. The spec is the source of truth. Spec drift is a bug.

6. **FFI-first.** The C ABI is the primary interface. If it works through `squeeze_ffi.h`, it works. Every tier ships a working FFI and Python client.

---

## System Diagram

```
Host Language (Python / Rust / Node.js / etc.)
     ↓ C ABI (squeeze_ffi.h)
┌─────────────────────────────────────────────┐
│              squeeze_ffi                    │
│         (C functions, opaque handles)       │
├──────────────┬──────────────────────────────┤
│ AudioDevice  │       PluginManager          │
│ (JUCE        │  (JUCE AudioPluginFormat,    │
│  DeviceMgr)  │   PluginCache)              │
├──────────────┴──────────────────────────────┤
│                  Engine                     │
│  ┌────────┐ ┌────────────┐ ┌────────────┐  │
│  │ Graph  │ │CommandQueue│ │ PerfMonitor│  │
│  └────────┘ └────────────┘ └────────────┘  │
│  ┌────────────┐ ┌──────────────────────┐    │
│  │ Transport  │ │  EventScheduler      │    │
│  └────────────┘ └──────────────────────┘    │
│  ┌──────────────────┐ ┌────────────────┐    │
│  │ GraphSnapshot    │ │ GarbageQueue   │    │
│  └──────────────────┘ └────────────────┘    │
├─────────────────────────────────────────────┤
│  BufferLibrary  │    MidiDeviceManager      │
│  (AudioFormat,  │    (MidiRouter,           │
│   Buffer IDs)   │     device open/close)    │
└─────────────────────────────────────────────┘
```

---

## Component Architecture

### Engine (core processing kernel)

Engine is the central coordinator. It owns:

- **All nodes** (`std::unique_ptr<Node>`) and a global **IdAllocator**
- **Top-level Graph** (topology, connections, cycle detection)
- **CommandQueue** (lock-free control → audio SPSC bridge)
- **Transport** (tempo, position, loop state)
- **EventScheduler** (beat-timed event resolution)
- **PerfMonitor** (audio thread instrumentation)
- **Built-in output node** representing the audio device
- **GraphSnapshot** building and atomic swap
- **Garbage collection** (deferred deletion of old snapshots)

Engine's `processBlock()` is called by AudioDevice (or directly in tests via `prepareForTesting()`). It drains commands, advances transport, resolves events, routes buffers, and processes nodes in topological order.

Engine does **not** own or know about:
- Audio device management (AudioDevice)
- JUCE MessageManager / message pump (FFI-level `sq_pump`)
- Plugin scanning and instantiation (PluginManager)
- Buffer/sample loading (BufferLibrary)
- MIDI device open/close (MidiDeviceManager)

### AudioDevice

Wraps `juce::AudioDeviceManager`. Implements `juce::AudioIODeviceCallback`, calls `Engine::processBlock()` from the JUCE audio callback. Engine has no dependency on JUCE audio devices — AudioDevice depends on Engine, not the other way around.

### PluginManager

Owns `juce::AudioPluginFormatManager` and a plugin cache. Scans for plugins, instantiates them, returns `std::unique_ptr<Node>` (PluginNode). No Engine dependency — returns nodes that Engine then owns.

### BufferLibrary

Owns loaded audio `Buffer` objects with IDs. Uses `juce::AudioFormatManager` for decoding. No Engine dependency — returns Buffer handles that nodes reference.

### MidiDeviceManager

Wraps `MidiRouter`. Handles MIDI device open/close and routing rules. Uses its own SPSC queue from the MIDI callback thread.

### Message Pump

`sq_pump()` at the FFI level. Drives the process-global JUCE MessageManager and drains Engine's garbage queue. Not an Engine responsibility — it's a process-level concern exposed through the C ABI.

---

## Component Dependency Order

Build bottom-up, organized into phases. A component may only depend on those above it. The order is optimized for an early proof-of-concept: **load a plugin, open a MIDI keyboard, route, and play.**

### Phase 1: Foundations (tiers 0–6)

```
0.  Logger                (no dependencies — owns its internal lock-free ring buffer)
1.  Port                  (no dependencies)
2.  Node                  (Port)
3.  Graph                 (Node, Port)
4.  SPSCQueue             (no dependencies)
5.  CommandQueue           (SPSCQueue)
6.  MidiRouter            (SPSCQueue)
```

### Phase 2: Engine + Plugin Playback (tiers 7–11) — POC milestone

```
7.  Engine                (Graph, CommandQueue, MidiRouter)
8.  PluginNode            (Node, JUCE plugin hosting)
9.  PluginManager          (PluginNode, JUCE AudioPluginFormatManager)
10. AudioDevice           (Engine, JUCE AudioDeviceManager)
11. MidiDeviceManager      (MidiRouter)
```

**★ After tier 11:** load a VST plugin from cache, open a MIDI keyboard, route MIDI to the plugin, hear audio output. The first working end-to-end demo.

### Phase 3: Composite Nodes (tier 12)

```
12. GroupNode             (Node, Graph, Engine)
```

GroupNode is a core graph primitive — composite node types (mixers, channel strips, kits) are GroupNode configurations, not separate node classes. Built early to ensure the subgraph model is solid before adding more features on top.

### Phase 4: Transport & Scheduling (tiers 13–15)

```
13. Transport             (no dependencies)
14. EventScheduler        (SPSCQueue)
15. PerfMonitor           (no dependencies)
```

After phase 4, Engine's processBlock gains transport advance, beat-timed event resolution, loop boundary splitting, and audio thread instrumentation.

### Phase 5: Sample Playback (tiers 16–21)

```
16. Buffer                (no dependencies)
17. SamplerVoice          (Buffer)
18. TimeStretchEngine     (signalsmith-stretch)
19. VoiceAllocator        (SamplerVoice)
20. SamplerNode           (Node, VoiceAllocator, TimeStretchEngine, Buffer)
21. BufferLibrary          (Buffer, JUCE AudioFormatManager)
```

### Phase 6: Advanced (tiers 22–25)

```
22. RecorderNode          (Node, Buffer)
23. MidiSplitterNode      (Node — graph-level MIDI routing with note-range filtering)
24. ScopeTap / Metering   (SPSCQueue / SeqLock)
```

### Notes

Logger is tier 0 — it has no dependencies and every subsequent component uses it.

Engine is built at tier 7 with a simplified processBlock: drain commands, dispatch MIDI, process nodes. Transport advance (step 5), event resolution (in processSubBlock), and perf monitoring (steps 1/8) are added when those components land in phase 4.

Peripheral components (AudioDevice, PluginManager, BufferLibrary, MidiDeviceManager) are separate from Engine. They extend the system without bloating the Engine class. The FFI layer orchestrates across them via EngineHandle.

---

## Data Flow Examples

### Adding a node and connecting it

```
FFI Caller                        Engine (control thread)
    │                                   │
    │  sq_add_node(engine, node)        │
    │──────────────────────────────────▶│
    │                                   │ lock controlMutex_
    │                                   │ collectGarbage()
    │                                   │ id = idAllocator_.next()
    │                                   │ nodes_[id] = std::move(node)
    │                                   │ graph_.addNode(id, nodes_[id].get())
    │◀──────────────────────────────────│ return id
    │                                   │
    │  sq_connect(engine, src,          │
    │    "out", dst, "in", &err)        │
    │──────────────────────────────────▶│
    │                                   │ lock controlMutex_
    │                                   │ collectGarbage()
    │                                   │ connId = graph_.connect(src, dst)
    │                                   │ buildSnapshot() → new GraphSnapshot*
    │                                   │ commandQueue_.sendCommand(swapSnapshot)
    │◀──────────────────────────────────│ return connId
    │                                   │
```

### Processing a block

```
Engine::processBlock(numSamples):

  1. commandQueue_.processPending(handler)
     — install new snapshot if swapped
     — apply transport commands
     — push old snapshot to garbage queue

  2. transport_.advance(numSamples)
     — update sample position, beat position
     — detect loop boundary → split point

  3. If loop wraps mid-block:
     — process [0, splitSample) with pre-wrap beat range
     — process [splitSample, numSamples) with post-wrap beat range
     For each sub-block:

  4. eventScheduler_.retrieve(beatStart, beatEnd, subBlockSamples, ...)
     — resolve beat-timed events to sample offsets
     — MIDI events → node MidiBuffers
     — paramChange events → sub-block split points

  5. For each node in snapshot execution order:
     — sum fan-in audio sources into node's input buffer
     — merge MIDI sources into node's MIDI input buffer
     — node->process(context)

  6. Copy output node's buffer to device output
```

### Loop boundary splitting

When Transport detects a loop boundary within the current block, Engine splits processing:

```
Block: 512 samples
Loop region: beats 4.0 → 8.0
Current position: beat 7.8

Split at loop boundary (e.g., sample 200):
  Sub-block 1: samples [0, 200)   → beats [7.8, 8.0)
  Sub-block 2: samples [200, 512) → beats [4.0, 4.6...)

Each sub-block gets its own retrieve() call with a monotonic beat range.
Nodes see shorter numSamples — they are unaware of the split.
```

---

## Thread Model

| Thread | Locks controlMutex? | May block? | Responsibilities |
|--------|---------------------|------------|------------------|
| Audio (processBlock) | Never | Never | Drain commands, advance transport, resolve events, process nodes |
| FFI caller (host language) | Yes | Yes | All control-plane operations via `sq_*` functions |
| AudioDevice callback | Never | Never | Calls Engine::processBlock() |
| MIDI callback | Never | No | Writes to MidiRouter SPSC queue |
| GUI / message thread | Never | Yes | Plugin editor windows, JUCE MessageManager |

**Communication:**
- Control → Audio: lock-free SPSC queue (CommandQueue). Engine's `controlMutex_` ensures the SPSC single-producer invariant when multiple FFI callers exist.
- Audio → Control: lock-free SPSC queue (garbage items, meter data)
- Beat-timed events: SPSC queue (EventScheduler — control → audio)
- MIDI device input: SPSC queue (MidiRouter — MIDI callback → audio)

**Lock ordering:** `controlMutex_` must never be held when acquiring `MessageManagerLock`.

---

## Realtime Safety Rules

All code on the audio thread must be RT-safe.

### Forbidden on audio thread
- `new` / `delete` / `malloc` / `free`
- `std::vector::push_back`, `std::map` insert, any allocating container op
- `std::mutex`, `std::condition_variable`, any blocking primitive
- File I/O, network I/O, `std::cout`
- `std::string` construction (allocates)

### Allowed on audio thread
- Lock-free SPSC queues
- Atomic operations
- `std::array`, pre-allocated buffers
- Arithmetic, DSP, SIMD
- Calling `Node::process()` (nodes must also be RT-safe)

### Deferred deletion pattern
Old graph snapshots and buffers are pushed to the garbage queue by the audio thread and freed later by the control thread (via `CommandQueue::collectGarbage()`).

---

## Key Design Patterns

### Graph snapshot swap
The control thread builds a `GraphSnapshot` (execution order, pre-allocated buffers per node), then atomically swaps it in via the CommandQueue. The audio thread never modifies the graph — it reads an immutable snapshot.

### Topology is derived
Users declare data flow between nodes; the engine computes execution order automatically via Kahn's algorithm in `Graph::getExecutionOrder()`.

### Sub-block parameter splitting
When parameter changes arrive mid-block (from EventScheduler), the engine splits processing into sub-blocks at each change point. Nodes are unaware — they just see shorter `numSamples`.

### Error reporting
Internal C++ functions return `bool`/`int` with `std::string& errorMessage` out-params. The C ABI returns error codes (`SqResult`) and provides `sq_error_message()` for the last error string.

### Connections as API
Topology is derived from explicit `connect()` calls. Connections validate: matching signal types, no cycles (BFS detection). Both audio and MIDI allow fan-in — the Engine sums multiple audio sources into the input buffer before calling `process()`.

### Explicit output routing
No auto-summing anywhere. Audio goes where you connect it, nowhere else. Engine provides a built-in output node representing the audio device. All audio chains must explicitly connect to it:

```python
engine.connect(synth, "out", engine.output, "in")
```

Nodes with unconnected audio outputs are warned about at info level ("node 'Diva' output 'out' is unconnected") but produce no sound. This applies uniformly to Engine and GroupNode — all routing is explicit at every level.

### GroupNode (composite subgraph)
A `GroupNode` owns an internal `Graph` and implements the `Node` interface. It exports selected internal ports as group-level ports via `exportInput()` / `exportOutput()`. From the parent graph's perspective, it's just another node. Composite node types like mixers, channel strips, and kits are GroupNode configurations, not separate classes.

### Dynamic ports
Most nodes declare fixed ports at construction. GroupNode supports dynamic port export/unexport on the control thread — adding an input to a mixer bus or inserting an FX slot does not require tearing down and rebuilding the node. Port changes are graph mutations: modify ports, reconnect, rebuild snapshot, atomic swap. Removing a port auto-disconnects any connections referencing it.

### Scope / meter taps
Audio thread publishes metering data via SeqLock (like PerfMonitor). Scope taps use SPSC ring buffers. FFI callers read the latest values — no IPC, no shared memory, just in-process lock-free reads.

### C ABI conventions
- Opaque handles: `SqEngine`, `SqNode`, `SqScope`, etc. (pointers to internal C++ objects)
- All functions prefixed `sq_`
- Error returns via `SqResult` enum
- No C++ types in the public header — plain C, `extern "C"`
- Caller manages handle lifetime via `sq_*_create` / `sq_*_destroy` pairs

---

## Decisions Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-02-07 | Engine control-plane mutex | Each gateway calls Engine directly; Engine serializes with `std::mutex`. Simpler than a shared command queue. |
| 2026-02-16 | Split Engine into core + peripherals | v1 Engine was a god class. AudioDevice, PluginManager, BufferLibrary, MidiDeviceManager are separate components. Engine is a focused processing kernel. |
| 2026-02-16 | AudioDevice depends on Engine, not vice versa | Engine doesn't know about JUCE audio devices. AudioDevice wraps DeviceManager and calls processBlock(). Enables headless testing via prepareForTesting(). |
| 2026-02-16 | PluginManager: cache + instantiation in one class | Replaces v1's separate PluginCache. Owns AudioPluginFormatManager, loads XML cache, creates PluginNode instances. No Engine dependency — returns nodes. |
| 2026-02-16 | BufferLibrary: separate component with ID registry | Owns AudioFormatManager and buffer ID map. Monotonic IDs (never reused). removeBuffer() returns unique_ptr for deferred deletion. No Engine dependency. |
| 2026-02-16 | MidiDeviceManager / MidiRouter two-class split | MidiRouter (tier 6) handles routing table + audio-thread dispatch. MidiDeviceManager (tier 11) adds JUCE device open/close. Router works standalone (programmatic MIDI via EventScheduler). |
| 2026-02-16 | EngineHandle holds all top-level components | FFI layer orchestrates across Engine, AudioDevice, PluginManager, BufferLibrary, MidiDeviceManager. Engine never knows about peripheral components. |
| 2026-02-16 | Reorder tiers for plugin+MIDI POC | First milestone is "load plugin, open MIDI keyboard, play." Moved SPSCQueue/CommandQueue/MidiRouter to phase 1, PluginNode/PluginManager/AudioDevice/MidiDeviceManager to phase 2. Transport/EventScheduler/PerfMonitor deferred to phase 3. GroupNode deferred to phase 5. |
