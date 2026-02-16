# Engine Specification

## Responsibilities

- Own all nodes (`std::unique_ptr<Node>`) and provide a global `IdAllocator`
- Own the top-level `Graph` (topology, connections, cycle detection)
- Own `CommandQueue` (lock-free control → audio SPSC bridge)
- Own `Transport` (tempo, position, loop state)
- Own `EventScheduler` (beat-timed event resolution)
- Own `PerfMonitor` (audio thread instrumentation)
- Own `MidiRouter` (MIDI device queue dispatch in processBlock)
- Provide a built-in **output node** representing the audio device
- Build and swap `GraphSnapshot` (execution order + pre-allocated buffers)
- Execute `processBlock()`: drain commands, advance transport, resolve events, route buffers, process nodes
- Manage garbage collection (deferred deletion of old snapshots via `CommandQueue::collectGarbage()`)
- Detect and handle cascading topology changes from GroupNode port mutations
- Warn at info level when a node's audio output has no consumers
- Provide `prepareForTesting(sr, bs)` for headless unit tests
- Report the engine version string

## Overview

Engine is the core processing kernel of Squeeze v2. It coordinates graph topology, audio processing, transport, and event scheduling. It is a focused component — peripheral concerns (audio device, plugin management, buffer library, MIDI devices, message pump) are handled by separate components that interact with Engine through its public interface.

The control thread acquires `controlMutex_` for all mutations. The audio thread calls `processBlock()` without locking — it reads an immutable `GraphSnapshot` and drains lock-free queues.

## Interface

### C++ (`squeeze::Engine`)

```cpp
namespace squeeze {

class Engine {
public:
    Engine();
    ~Engine();

    std::string getVersion() const;  // Returns "0.2.0"

    // --- Node management (control thread) ---
    int addNode(std::unique_ptr<Node> node, const std::string& name);
    bool removeNode(int nodeId);
    Node* getNode(int nodeId) const;
    std::string getNodeName(int nodeId) const;
    int getOutputNodeId() const;  // built-in output node

    // --- Connections (control thread) ---
    int connect(int srcNode, const std::string& srcPort,
                int dstNode, const std::string& dstPort,
                std::string& error);
    bool disconnect(int connectionId);
    std::vector<Connection> getConnections() const;

    // --- Parameters (control thread) ---
    float getParameter(int nodeId, const std::string& name) const;
    bool setParameter(int nodeId, const std::string& name, float value);
    std::string getParameterText(int nodeId, const std::string& name) const;
    std::vector<ParameterDescriptor> getParameterDescriptors(int nodeId) const;

    // --- Transport forwarding (control thread → CommandQueue) ---
    void transportPlay();
    void transportStop();
    void transportPause();
    void transportSetTempo(double bpm);
    void transportSetTimeSignature(int numerator, int denominator);
    void transportSeekSamples(int64_t samples);
    void transportSeekBeats(double beats);
    void transportSetLoopPoints(double startBeats, double endBeats);
    void transportSetLooping(bool enabled);

    // --- Transport query (control thread, reads atomic state) ---
    double getTransportPosition() const;   // beats
    double getTransportTempo() const;      // BPM
    bool isTransportPlaying() const;

    // --- Event scheduling (control thread → EventScheduler) ---
    bool scheduleNoteOn(int nodeId, double beatTime, int channel, int note, float velocity);
    bool scheduleNoteOff(int nodeId, double beatTime, int channel, int note);
    bool scheduleCC(int nodeId, double beatTime, int channel, int ccNum, int ccVal);
    bool scheduleParamChange(int nodeId, double beatTime, const std::string& paramName, float value);

    // --- Audio processing (audio thread) ---
    void processBlock(float** outputChannels, int numChannels, int numSamples);

    // --- Testing ---
    void prepareForTesting(double sampleRate, int blockSize);
    void render(int numSamples);  // process one block in test mode (allocates output buffer internally)

    // --- Query ---
    std::vector<std::pair<int, std::string>> getNodes() const;
    std::vector<int> getExecutionOrder() const;
    int getNodeCount() const;

private:
    mutable std::mutex controlMutex_;

    // Node storage
    struct NodeEntry {
        std::unique_ptr<Node> node;
        std::string name;
    };
    std::unordered_map<int, NodeEntry> nodes_;
    IdAllocator idAllocator_;
    int outputNodeId_ = -1;

    // Graph and snapshot
    Graph graph_;
    GraphSnapshot* activeSnapshot_ = nullptr;

    // Sub-components
    CommandQueue commandQueue_;
    Transport transport_;
    EventScheduler eventScheduler_;
    PerfMonitor perfMonitor_;
    MidiRouter midiRouter_;

    // Internal
    void buildAndSwapSnapshot();
    void handleCommand(const Command& cmd);  // audio thread command handler
    void checkForCascadingChanges();          // after GroupNode mutations
};

} // namespace squeeze
```

### C ABI (`squeeze_ffi.h`)

```c
typedef void* SqEngine;

// Lifecycle
SqEngine sq_engine_create(char** error);
void     sq_engine_destroy(SqEngine engine);
char*    sq_version(SqEngine engine);
void     sq_free_string(char* s);

// Node management
// No generic sq_add_node — node creation is type-specific at the FFI level:
//   sq_add_plugin(engine, name, &error)  → PluginManager creates, Engine owns
//   sq_add_gain(engine, &error)          → built-in test/utility nodes
// Each creator returns a node ID (or -1 on failure).
bool sq_remove_node(SqEngine engine, int node_id);
int  sq_output_node(SqEngine engine);
char* sq_node_name(SqEngine engine, int node_id);
int  sq_node_count(SqEngine engine);
SqIdNameList sq_nodes(SqEngine engine);

// Connections
int  sq_connect(SqEngine engine, int src_node, const char* src_port,
                int dst_node, const char* dst_port, char** error);
bool sq_disconnect(SqEngine engine, int conn_id);
SqConnectionList sq_connections(SqEngine engine);

// Parameters
float sq_get_param(SqEngine engine, int node_id, const char* name);
bool  sq_set_param(SqEngine engine, int node_id, const char* name, float value);
char* sq_param_text(SqEngine engine, int node_id, const char* name);
SqParamDescriptorList sq_param_descriptors(SqEngine engine, int node_id);

// Transport
void sq_transport_play(SqEngine engine);
void sq_transport_stop(SqEngine engine);
void sq_transport_pause(SqEngine engine);
void sq_transport_set_tempo(SqEngine engine, double bpm);
void sq_transport_set_time_signature(SqEngine engine, int numerator, int denominator);
void sq_transport_seek_samples(SqEngine engine, int64_t samples);
void sq_transport_seek_beats(SqEngine engine, double beats);
void sq_transport_set_loop_points(SqEngine engine, double start_beats, double end_beats);
void sq_transport_set_looping(SqEngine engine, bool enabled);
double sq_transport_position(SqEngine engine);
double sq_transport_tempo(SqEngine engine);
bool sq_transport_is_playing(SqEngine engine);

// Event scheduling
bool sq_schedule_note_on(SqEngine engine, int node_id, double beat_time,
                         int channel, int note, float velocity);
bool sq_schedule_note_off(SqEngine engine, int node_id, double beat_time,
                          int channel, int note);
bool sq_schedule_cc(SqEngine engine, int node_id, double beat_time,
                    int channel, int cc_num, int cc_val);
bool sq_schedule_param_change(SqEngine engine, int node_id, double beat_time,
                              const char* param_name, float value);

// Testing
void sq_prepare_for_testing(SqEngine engine, double sample_rate, int block_size);
void sq_render(SqEngine engine, int num_samples);

// Message pump (process-global, not Engine-specific)
void sq_pump(void);
```

### FFI List Types

Shared return types for functions that return variable-length lists. The caller frees each with its corresponding `sq_free_*` function.

```c
typedef struct {
    int* ids;
    char** names;
    int count;
} SqIdNameList;

typedef struct {
    int* ids;
    int* src_nodes;
    char** src_ports;
    int* dst_nodes;
    char** dst_ports;
    int count;
} SqConnectionList;

typedef struct {
    char** names;
    float* min_values;
    float* max_values;
    float* default_values;
    int count;
} SqParamDescriptorList;

typedef struct {
    char** items;
    int count;
} SqStringList;

void sq_free_id_name_list(SqIdNameList list);
void sq_free_connection_list(SqConnectionList list);
void sq_free_param_descriptor_list(SqParamDescriptorList list);
void sq_free_string_list(SqStringList list);
```

### EngineHandle (internal)

```cpp
struct EngineHandle {
    squeeze::Engine engine;
    squeeze::AudioDevice audioDevice;           // wraps engine
    squeeze::PluginManager pluginManager;       // independent
    squeeze::BufferLibrary bufferLibrary;       // independent
    squeeze::MidiDeviceManager midiDeviceManager; // wraps MidiRouter → engine
};
```

`sq_engine_create()` allocates an `EngineHandle` on the heap. The returned opaque `SqEngine` pointer is the only handle the caller needs. Peripheral components (AudioDevice, PluginManager, BufferLibrary, MidiDeviceManager) are peers of Engine within the handle — Engine never knows about them.

**FFI orchestration:** Some `sq_*` functions span multiple components. For example, `sq_add_plugin(handle, "Diva", &err)` calls `handle->pluginManager.createNode("Diva", ...)` then `handle->engine.addNode(std::move(node), "Diva")`. Similarly, `sq_load_buffer` uses BufferLibrary, and buffer assignment to nodes is orchestrated through both BufferLibrary and Engine. These are FFI-layer concerns, not Engine methods.

## Built-in Output Node

Engine creates a built-in output node at construction. This node represents the audio device and is the only path to audible output.

- The output node has a well-known ID accessible via `getOutputNodeId()` / `sq_output_node()` / `engine.output`
- It has a stereo audio input port `"in"` (channel count may be configurable later)
- It cannot be removed
- Audio chains must explicitly connect to it: `sq_connect(engine, synth, "out", SQ_OUTPUT, "in", &error)`
- Nodes with unconnected audio outputs are warned about at info level during snapshot rebuild

```python
synth = engine.add_plugin("Diva")
engine.connect(synth, "out", engine.output, "in")  # explicit — no sound without this
```

## processBlock Sequence

Called by AudioDevice (or test harness) on the audio thread. Must be fully RT-safe.

```
processBlock(outputChannels, numChannels, numSamples):

  1. perfMonitor_.beginBlock()

  2. commandQueue_.processPending([this](cmd) { handleCommand(cmd); })
     — swapSnapshot: install new, push old to garbage
     — transport commands: forward to transport_
     — transport stop/seek: also clear eventScheduler_

  3. midiRouter_.dispatch(snapshot nodeBuffers, numSamples)
     — drain per-device SPSC queues
     — route messages to destination node MidiBuffers per routing table

  4. if no active snapshot → write silence, return

  5. transport_.advance(numSamples)
     — detect loop boundary → compute splitSample

  6. if loop wraps mid-block:
       processSubBlock(0, splitSample, prewrapBeats)
       processSubBlock(splitSample, numSamples, postwrapBeats)
     else:
       processSubBlock(0, numSamples, blockBeats)

  7. copy output node's buffer → outputChannels

  8. perfMonitor_.endBlock()
```

### processSubBlock

```
processSubBlock(startSample, endSample, beatRange):

  1. eventScheduler_.retrieve(beatStart, beatEnd, subSamples, tempo, sr, ...)
     — resolve beat-timed events to sample offsets

  2. collect paramChange events → sub-block split points
     dispatch MIDI events → target node MidiBuffers

  3. for each sub-block between parameter split points:
       for each node in snapshot execution order:
         — sum fan-in audio into input buffer
         — merge MIDI into input MIDI buffer
         — node->process(context)
         — apply parameter changes at sub-block boundaries
```

## GraphSnapshot (internal)

`GraphSnapshot` is an immutable, pre-computed structure built by the control thread and swapped into the audio thread atomically via CommandQueue. The audio thread reads the active snapshot — it never modifies Graph directly.

```cpp
struct GraphSnapshot {
    // Execution order (topologically sorted node IDs)
    std::vector<int> executionOrder;

    // Per-node pre-allocated buffers
    struct NodeBuffers {
        juce::AudioBuffer<float> audio;   // pre-sized to max channels × block size
        juce::MidiBuffer midi;
    };
    std::unordered_map<int, NodeBuffers> nodeBuffers;

    // Fan-in connection lists (which sources feed each node's input)
    struct FanIn {
        int sourceNodeId;
        std::string sourcePort;
        std::string destPort;
    };
    std::unordered_map<int, std::vector<FanIn>> audioFanIn;
    std::unordered_map<int, std::vector<FanIn>> midiFanIn;
};
```

**Lifecycle:** Built by `buildAndSwapSnapshot()` on the control thread → sent via CommandQueue → installed on the audio thread → old snapshot pushed to garbage queue → freed on control thread by `collectGarbage()`.

**Key property:** All buffers are pre-allocated at snapshot build time. The audio thread never allocates — it writes into existing buffers.

## Garbage Collection

Engine calls `commandQueue_.collectGarbage()` at the top of every control-thread method that acquires `controlMutex_`. This ensures old snapshots and other deferred-deletion items are freed regularly on the control thread, without requiring an external timer or caller discipline.

## Cascading Topology Changes

When a GroupNode's external ports change (via `unexportPort()` or removal of an internal node with exported ports), Engine must detect this and auto-disconnect any parent-graph connections that reference removed ports. This cascade is checked after every structural mutation that involves a GroupNode.

## Invariants

- `sq_engine_create` returns a non-NULL handle on success
- `sq_engine_create` returns NULL and sets `*error` on failure
- `sq_engine_destroy(NULL)` is a no-op
- `sq_free_string(NULL)` is a no-op
- `sq_version` returns a non-NULL string that the caller must free with `sq_free_string`
- Multiple engines can be created and destroyed independently
- JUCE MessageManager is initialized exactly once, on the first `sq_engine_create` call
- `processBlock()` is fully RT-safe: no allocation, no blocking, no unbounded work
- The audio thread never touches Graph directly — it reads the immutable GraphSnapshot
- All node IDs are globally unique (across Engine and all nested GroupNodes)
- The output node cannot be removed
- Every structural mutation (add/remove node, connect/disconnect) triggers a snapshot rebuild
- `controlMutex_` serializes all control-thread operations
- Garbage is collected at the top of every control-thread operation
- `render()` requires `prepareForTesting()` to have been called first
- `render()` allocates the output buffer internally — it is a testing convenience, not RT-safe

## Error Conditions

- `sq_engine_create`: allocation failure or JUCE init failure → returns NULL, sets `*error`
- `sq_engine_create`: NULL `error` pointer is safe — error message is discarded
- `addNode()` with null node: returns -1
- `removeNode()` with output node ID: returns false (cannot remove)
- `removeNode()` with unknown ID: returns false
- `connect()` with nonexistent node or port: returns -1, sets error
- `connect()` that would create a cycle: returns -1, sets error
- `getNode()` with unknown ID: returns nullptr
- `setParameter()` with unknown node ID: returns false
- `scheduleNoteOn()` with full EventScheduler queue: returns false
- `commandQueue_.sendCommand()` full: snapshot deleted by caller, logged at warn

## Does NOT Handle

- **Audio device management** — AudioDevice wraps JUCE AudioDeviceManager, calls processBlock()
- **JUCE MessageManager / message pump** — FFI-level `sq_pump()`, not Engine
- **Plugin scanning and instantiation** — PluginManager returns `unique_ptr<Node>`, Engine just owns nodes
- **Buffer/sample loading and management** — BufferLibrary owns Buffers with IDs
- **MIDI device open/close and routing rules** — MidiDeviceManager wraps MidiRouter
- **Type-specific node creation** — nodes are created by their respective managers and passed to Engine
- **Type-specific node configuration** — nodes expose through the generic parameter system

## Dependencies

- Graph (topology, connections, cycle detection)
- Node (the abstract interface Engine owns and processes)
- Port (PortDescriptor, PortAddress, Connection)
- CommandQueue (lock-free control → audio bridge)
- Transport (tempo, position, loop)
- EventScheduler (beat-timed event resolution)
- PerfMonitor (audio thread instrumentation)
- MidiRouter (MIDI device queue dispatch in processBlock)
- JUCE (`juce_core`, `juce_events` for MessageManager, `juce_audio_basics` for AudioBuffer/MidiBuffer)
- C standard library (`malloc`, `free`, `strdup`)

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| `sq_engine_create` / `sq_engine_destroy` | Control | Not concurrent with other calls |
| `addNode()` / `removeNode()` | Control | Acquires `controlMutex_` |
| `connect()` / `disconnect()` | Control | Acquires `controlMutex_`, triggers snapshot rebuild |
| `getParameter()` / `setParameter()` | Control | Acquires `controlMutex_` |
| `transportPlay()` / `transportStop()` / etc. | Control | Acquires `controlMutex_`, sends command |
| `scheduleNoteOn()` / etc. | Control | Acquires `controlMutex_`, writes to EventScheduler |
| `processBlock()` | Audio | Never locks, reads snapshot, drains queues |
| `getNode()` / `getNodes()` / queries | Control | Acquires `controlMutex_` |
| `prepareForTesting()` | Control | Acquires `controlMutex_`, sets up test mode |
| `render()` | Control | Acquires `controlMutex_`, calls processBlock internally |

JUCE init is guarded by a static flag (single-threaded assumption for first create call).

## Python API

```python
from squeeze import Engine

engine = Engine()
print(engine.version)       # "0.2.0"

# Node management
synth = engine.add_plugin("Diva")
engine.connect(synth, "out", engine.output, "in")

# Parameters
engine.set_param(synth, "cutoff", 0.5)
print(engine.get_param(synth, "cutoff"))

# Transport
engine.transport_set_tempo(120.0)
engine.transport_play()

# Event scheduling
engine.schedule_note_on(synth, beat_time=1.0, channel=1, note=60, velocity=0.8)

# Headless testing (no audio device)
engine.prepare_for_testing(sample_rate=44100.0, block_size=512)
engine.render(512)  # process one block

# Cleanup
engine.close()
```

## Example Usage

### C ABI

```c
#include "squeeze_ffi.h"
#include <stdio.h>

int main() {
    char* error = NULL;
    SqEngine engine = sq_engine_create(&error);
    if (!engine) {
        fprintf(stderr, "Failed: %s\n", error);
        sq_free_string(error);
        return 1;
    }

    char* version = sq_version(engine);
    printf("Squeeze %s\n", version);
    sq_free_string(version);

    // Get output node
    int output = sq_output_node(engine);

    // Connect a node to the output
    // int synth = sq_add_plugin(engine, "Diva", &error);
    // sq_connect(engine, synth, "out", output, "in", &error);

    sq_engine_destroy(engine);
    return 0;
}
```

### Headless testing (C++)

```cpp
Engine engine;
engine.prepareForTesting(44100.0, 512);

auto gain = std::make_unique<GainNode>();
int gainId = engine.addNode(std::move(gain), "gain");

std::string error;
int connId = engine.connect(gainId, "out", engine.getOutputNodeId(), "in", error);
assert(connId >= 0);

// Option 1: render() convenience (allocates output buffer internally)
engine.render(512);

// Option 2: processBlock() with caller-supplied buffers
float* outputs[2] = { leftBuf, rightBuf };
engine.processBlock(outputs, 2, 512);
```

### Headless testing (C ABI)

```c
SqEngine engine = sq_engine_create(&error);
sq_prepare_for_testing(engine, 44100.0, 512);

// ... add nodes, connect ...

sq_render(engine, 512);  // process one block
sq_engine_destroy(engine);
```
