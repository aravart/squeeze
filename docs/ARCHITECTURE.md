# Squeeze v2 — Architecture

## Overview

Squeeze v2 is a mixer-centric C++17 audio engine for hosting VST/AU plugins and routing audio through a structured mixer model. It exposes a C ABI (`squeeze_ffi`) for multi-language integration — the host language (Python, Rust, etc.) provides the user-facing interface.

The architecture models what the requirements actually describe: a mixer. Sources generate audio through insert chains, buses sum and process, sends tap signals to effects returns, and the master bus outputs to the audio device. This purpose-built structure replaces the general-purpose node graph, eliminating ports, connections, topological sorts, GroupNode, dynamic ports, and cascading change detection.

---

## Design Principles

1. **Mixer-centric model.** The primitives directly model what users build: channel strips, buses, sends, insert racks. Not a circuit diagram — a mixer.

2. **In-place processing.** Processors read and write the same buffer. Chains call processors sequentially on a single buffer — zero-copy serial processing.

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
│  ┌──────────┐ ┌────────────┐ ┌──────────┐  │
│  │ Sources  │ │   Buses    │ │  Master  │  │
│  └──────────┘ └────────────┘ └──────────┘  │
│  ┌────────────┐ ┌──────────────────────┐    │
│  │CommandQueue│ │    MidiRouter        │    │
│  └────────────┘ └──────────────────────┘    │
│  ┌────────────┐ ┌──────────────────────┐    │
│  │ Transport  │ │  EventScheduler      │    │
│  └────────────┘ └──────────────────────┘    │
│  ┌──────────────────┐ ┌────────────────┐    │
│  │ MixerSnapshot    │ │ GarbageQueue   │    │
│  └──────────────────┘ └────────────────┘    │
├─────────────────────────────────────────────┤
│  BufferLibrary  │    MidiDeviceManager      │
│  (AudioFormat,  │    (MidiRouter,           │
│   Buffer IDs)   │     device open/close)    │
└─────────────────────────────────────────────┘
```

---

## Primitives

### Processor

The base abstraction. Processes audio **in-place** on a buffer. No ports, no connections.

```cpp
class Processor {
public:
    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void process(AudioBuffer& buffer) = 0;
    virtual void process(AudioBuffer& buffer, const MidiBuffer& midi) { process(buffer); }

    virtual int getParameterCount() const { return 0; }
    virtual float getParameter(const std::string& name) const { return 0.0f; }
    virtual void setParameter(const std::string& name, float value) {}
    virtual int getLatencySamples() const { return 0; }

    const std::string& getName() const;
    int getHandle() const;
};
```

Concrete types: PluginProcessor, GainProcessor, MeterProcessor, RecordingProcessor.

### Chain

An ordered list of Processors. Sequential in-place processing on the same buffer.

```cpp
class Chain {
public:
    void process(AudioBuffer& buffer, const MidiBuffer& midi);
    int getLatencySamples() const; // sum of all processor latencies

    void append(Processor* p);
    void insert(int index, Processor* p);
    Processor* remove(int index);
    void move(int from, int to);
};
```

This is the **insert rack**. Every Source and Bus owns one.

### Source

A sound generator + insert chain + routing + MIDI.

```
Source
├── generator: Processor*          (synth, sampler, audio input)
├── chain: Chain                   (insert effects)
├── output_bus: Bus*               (where this source feeds)
├── sends: [(Bus*, float level)]   (send taps with levels)
└── midi_input: MidiAssignment     (device + channel + note range)
```

### Bus

A summing point + insert chain + routing.

```
Bus
├── inputs: [buffer references]    (from sources or other buses)
├── chain: Chain                   (insert effects)
├── output_bus: Bus*               (downstream bus, or master)
├── sends: [(Bus*, float level)]   (send taps with levels)
└── metering: peak, rms            (atomic, always available)
```

### Master

A special Bus. Always exists. Final output to the audio device.

### Send

Not a separate object — a routing entry: `(destination_bus, level_db)`. Engine copies post-chain buffer (scaled) to the destination bus input.

---

## Component Architecture

### Engine (core processing kernel)

Engine is the central coordinator. It owns:

- **All Sources** and **all Buses** (including Master)
- **CommandQueue** (lock-free control → audio SPSC bridge)
- **Transport** (tempo, position, loop state)
- **EventScheduler** (beat-timed event resolution)
- **PerfMonitor** (audio thread instrumentation)
- **MidiRouter** (MIDI device queue dispatch in processBlock)
- **MixerSnapshot** building and atomic swap
- **Garbage collection** (deferred deletion of old snapshots)

Engine's `processBlock()` is called by AudioDevice (or directly in tests via `render()`). It drains commands, advances transport, resolves events, processes sources, accumulates bus inputs, processes buses in dependency order, and outputs Master.

Engine does **not** own or know about:
- Audio device management (AudioDevice)
- JUCE MessageManager / message pump (FFI-level `sq_pump`)
- Plugin scanning and instantiation (PluginManager)
- Buffer/sample loading (BufferLibrary)
- MIDI device open/close (MidiDeviceManager)

### AudioDevice

Wraps `juce::AudioDeviceManager`. Implements `juce::AudioIODeviceCallback`, calls `Engine::processBlock()` from the JUCE audio callback. Engine has no dependency on JUCE audio devices.

### PluginManager

Owns `juce::AudioPluginFormatManager` and a plugin cache. Scans for plugins, instantiates them, returns `std::unique_ptr<Processor>` (PluginProcessor). No Engine dependency.

### BufferLibrary

Owns loaded audio `Buffer` objects with IDs. Uses `juce::AudioFormatManager` for decoding. No Engine dependency.

### MidiDeviceManager

Wraps `MidiRouter`. Handles MIDI device open/close and routing rules. Uses its own SPSC queue from the MIDI callback thread.

### Message Pump

`sq_pump()` at the FFI level. Drives the process-global JUCE MessageManager and drains Engine's garbage queue.

---

## Component Dependency Order

Build bottom-up, organized into phases.

### Phase 1: Foundations (tiers 0–6)

```
0.  Logger                (no dependencies)
1.  Processor             (JUCE AudioBuffer, MidiBuffer)
2.  Chain                 (Processor)
3.  Source                (Processor, Chain)
4.  Bus                   (Chain)
5.  SPSCQueue             (no dependencies)
6.  CommandQueue          (SPSCQueue)
    MidiRouter            (SPSCQueue, Source)
```

### Phase 2: Engine + Plugin Playback (tiers 7–11) — POC milestone

```
7.  Engine                (Source, Bus, Chain, CommandQueue, MidiRouter)
8.  PluginProcessor       (Processor, JUCE plugin hosting)
9.  PluginManager         (PluginProcessor, JUCE AudioPluginFormatManager)
10. AudioDevice           (Engine, JUCE AudioDeviceManager)
11. MidiDeviceManager     (MidiRouter)
```

**After tier 11:** Load a VST plugin as a Source, open a MIDI keyboard, route MIDI to the source, hear audio output through Master. First working end-to-end demo.

### Phase 3: Transport & Scheduling (tiers 12–14)

```
12. Transport             (no dependencies)
13. EventScheduler        (SPSCQueue)
14. PerfMonitor           (no dependencies)
```

After phase 3, Engine's processBlock gains transport advance, beat-timed event resolution, and audio thread instrumentation.

### Phase 4: Sample Playback (tiers 15–20)

```
15. Buffer                (no dependencies)
16. SamplerVoice          (Buffer)
17. TimeStretchEngine     (signalsmith-stretch)
18. VoiceAllocator        (SamplerVoice)
19. SamplerProcessor      (Processor, VoiceAllocator, TimeStretchEngine, Buffer)
20. BufferLibrary         (Buffer, JUCE AudioFormatManager)
```

### Phase 5: Advanced (tiers 21–23)

```
21. RecordingProcessor    (Processor)
22. PDC                   (Processor, Chain, Source, Bus, Engine — cross-cutting)
23. MeterProcessor        (Processor)
```

### Notes

Logger is tier 0 — every subsequent component uses it.

Engine is built at tier 7 with a simplified processBlock: drain commands, dispatch MIDI, process sources, sum into buses, process buses, output master. Transport advance, event resolution, and perf monitoring are added when those components land in phase 3.

Peripheral components (AudioDevice, PluginManager, BufferLibrary, MidiDeviceManager) are separate from Engine. The FFI layer orchestrates across them via EngineHandle.

---

## Processing Loop

```
1. For each source (independent — parallelizable):
      source.generator.process(buffer, midi)
      source.chain.process(buffer, midi)

2. Accumulate bus inputs:
      For each source:  add buffer to source.output_bus
      For each source:  for each send, add buffer * level to send.bus
      For each bus:     for each send, add buffer * level to send.bus

3. For each bus (in dependency order — DAG sort over buses):
      bus.chain.process(buffer)

4. master.chain.process(buffer)
   output(master.buffer)
```

The dependency sort is over **buses** (typically 2–8), not individual processors. Trivially cheap.

---

## Data Flow Examples

### Adding a source and routing it

```
FFI Caller                        Engine (control thread)
    │                                   │
    │  sq_add_source_plugin(...)       │
    │──────────────────────────────────▶│
    │                                   │ lock controlMutex_
    │                                   │ collectGarbage()
    │                                   │ create Source with PluginProcessor generator
    │                                   │ source.routeTo(master)   // default
    │                                   │ buildAndSwapSnapshot()
    │◀──────────────────────────────────│ return SqSource handle
    │                                   │
    │  sq_route(engine, src, bus)       │
    │──────────────────────────────────▶│
    │                                   │ lock controlMutex_
    │                                   │ collectGarbage()
    │                                   │ source.routeTo(bus)
    │                                   │ buildAndSwapSnapshot()
    │◀──────────────────────────────────│ return
```

### Processing a block

```
Engine::processBlock(numSamples):

  1. perfMonitor_.beginBlock()

  2. commandQueue_.processPending(handler)
     — install new snapshot if swapped
     — apply transport commands
     — push old snapshot to garbage queue

  3. midiRouter_.dispatch(sourceBuffers, numSamples)
     — drain per-device SPSC queues
     — route messages to destination source MidiBuffers

  4. if no active snapshot → write silence, return

  5. transport_.advance(numSamples)

  6. For each source: source.process(buffer, midi)

  7. Accumulate into buses (sources + sends)

  8. For each bus in dependency order: bus.process(buffer)

  9. Copy master buffer → outputChannels

  10. perfMonitor_.endBlock()
```

---

## What's Eliminated (from v1/graph-based architecture)

| Old Concept | Replacement |
|---|---|
| Node (with ports) | Processor (no ports, in-place) |
| Port / PortDescriptor | Gone — chains pass one buffer, buses sum explicitly |
| Connection | Gone — routing is source→bus, bus→bus, send→bus |
| Graph / GraphSnapshot | Gone — MixerSnapshot with bus DAG |
| GroupNode | Gone — Bus + Chain cover all composition patterns |
| Dynamic ports | Gone — chains grow/shrink, buses accept any input |
| Port export/unexport | Gone |
| Cascading change detection | Gone |
| Global IdAllocator | Gone — opaque handles |
| Topology sort over N nodes | Sort over M buses (M << N) |
| MidiSplitterNode | Gone — MIDI routing table |
| Doubled `sq_group_*` API | Gone — one API surface |

---

## Thread Model

| Thread | Locks controlMutex? | May block? | Responsibilities |
|--------|---------------------|------------|------------------|
| Audio (processBlock) | Never | Never | Drain commands, advance transport, process sources/buses |
| FFI caller (host language) | Yes | Yes | All control-plane operations via `sq_*` functions |
| AudioDevice callback | Never | Never | Calls Engine::processBlock() |
| MIDI callback | Never | No | Writes to MidiRouter SPSC queue |
| GUI / message thread | Never | Yes | Plugin editor windows, JUCE MessageManager |

**Communication:**
- Control → Audio: lock-free SPSC queue (CommandQueue)
- Audio → Control: lock-free SPSC queue (garbage items, meter data)
- Beat-timed events: SPSC queue (EventScheduler)
- MIDI device input: SPSC queue (MidiRouter)

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
- Calling `Processor::process()` (processors must also be RT-safe)

### Deferred deletion pattern
Old snapshots and removed processors are pushed to the garbage queue by the audio thread and freed later by the control thread (via `CommandQueue::collectGarbage()`).

---

## Key Design Patterns

### Mixer snapshot swap
The control thread builds a `MixerSnapshot` (source arrays, bus arrays in dependency order, pre-allocated buffers, routing tables), then atomically swaps it in via the CommandQueue. The audio thread reads the immutable snapshot.

### Bus dependency sort
Buses form a DAG through their `routeTo` relationships and sends. The Engine sorts buses in dependency order at snapshot build time. With ~4-8 buses, this is trivially cheap.

### In-place processing
Processors read and write the same buffer. Chains call processors sequentially on a single buffer. No separate input/output buffers, no port declarations, no channel routing.

### Sub-block parameter splitting
When parameter changes arrive mid-block (from EventScheduler), the engine splits processing into sub-blocks at each change point. Processors are unaware — they just see shorter `numSamples`.

### Error reporting
Internal C++ functions return `bool`/`int` with `std::string& errorMessage` out-params. The C ABI returns error codes and provides `sq_free_string()` for error strings.

### C ABI conventions
- Opaque handles: `SqEngine`, `SqSource`, `SqBus`, `SqProc`
- All functions prefixed `sq_`
- No C++ types in the public header — plain C, `extern "C"`
- Caller manages handle lifetime

---

## Decisions Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-02-07 | Engine control-plane mutex | Serializes FFI callers. Simpler than a shared command queue. |
| 2026-02-16 | Split Engine into core + peripherals | AudioDevice, PluginManager, BufferLibrary, MidiDeviceManager are separate components. |
| 2026-02-16 | AudioDevice depends on Engine, not vice versa | Enables headless testing. |
| 2026-02-16 | EngineHandle holds all top-level components | FFI layer orchestrates across Engine, AudioDevice, PluginManager, etc. |
| 2026-02-17 | Mixer-centric architecture | Requirements describe a mixer, not an arbitrary signal graph. Purpose-built primitives (Source, Bus, Chain, Processor) replace general-purpose graph (Node, Port, Connection, Graph, GroupNode). Dramatically simpler, fewer concepts, better performance, API reads like a mixer. |
| 2026-02-17 | In-place processing model | Processors process audio in-place on a single buffer. Chains are zero-copy sequential. Eliminates separate input/output buffers, port channel routing, and fan-in summation at the processor level. |
| 2026-02-17 | Bus DAG replaces node graph | Routing is source→bus, bus→bus, send→bus. The dependency sort is over ~4-8 buses, not hundreds of nodes. Topological sort is trivially cheap. |
| 2026-02-17 | Opaque handles replace node IDs | Processors, Sources, and Buses are identified by opaque handles. No global IdAllocator needed. |
