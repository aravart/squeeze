# Engine Specification

## Overview

Engine is the central owner of the audio graph, nodes, plugin cache, and audio callback. It orchestrates all processing: it processes pending commands from the Scheduler, walks the graph snapshot in execution order, and routes buffers between nodes. It also provides the control-thread API for node management, graph topology, plugin instantiation, MIDI input management, and parameter access. The processing logic is decoupled from the audio device so it can be tested without hardware.

## GraphSnapshot

An immutable, pre-allocated structure that the audio thread processes. Built on the control thread from Graph state, swapped atomically via the Scheduler.

```cpp
struct GraphSnapshot {
    struct NodeSlot {
        Node* node;
        int audioSourceIndex;  // index in slots for audio input, or -1 (silence)
        int midiSourceIndex;   // index in slots for MIDI input, or -1 (empty)
        bool isAudioLeaf;      // true if no other node reads this node's audio output
    };

    std::vector<NodeSlot> slots;  // in execution order

    // Pre-allocated buffers, one per slot
    std::vector<juce::AudioBuffer<float>> audioOutputs;
    std::vector<juce::MidiBuffer> midiOutputs;

    // For unconnected inputs
    juce::AudioBuffer<float> silenceBuffer;
    juce::MidiBuffer emptyMidi;
};
```

## Responsibilities

- Own the Graph, PluginCache, AudioPluginFormatManager, and all dynamically created nodes
- Manage node lifecycle: add, remove, name tracking
- Manage MIDI input devices: add, auto-load, refresh
- Manage graph topology: connect, disconnect, query connections
- Provide plugin instantiation from cache
- Provide parameter read/write on nodes
- Process pending commands from Scheduler each block
- Process nodes in topological order
- Route audio and MIDI buffers between connected nodes
- Copy final output to the device output buffer
- Build GraphSnapshots from Graph state on the control thread
- Manage audio device lifecycle (start/stop)

## Interface

```cpp
class Engine : public juce::AudioIODeviceCallback {
public:
    explicit Engine(Scheduler& scheduler);
    ~Engine();

    // Control thread: device management
    bool start(double sampleRate = 44100.0, int blockSize = 512);
    void stop();
    bool isRunning() const;
    double getSampleRate() const;
    int getBlockSize() const;

    // Plugin cache
    bool loadPluginCache(const std::string& xmlPath);
    std::vector<std::string> getAvailablePluginNames() const;
    const juce::PluginDescription* findPluginByName(const std::string& name) const;

    // Node management (control thread)
    int addNode(std::unique_ptr<Node> node, const std::string& name);
    bool removeNode(int id);
    Node* getNode(int id) const;
    std::string getNodeName(int id) const;
    std::vector<std::pair<int, std::string>> getNodes() const;

    // Plugin instantiation
    int addPlugin(const std::string& name, std::string& errorMessage);

    // MIDI input management
    std::vector<std::string> getAvailableMidiInputs() const;
    int addMidiInput(const std::string& deviceName, std::string& errorMessage);
    void autoLoadMidiInputs();

    struct MidiRefreshResult {
        std::vector<std::string> added;
        std::vector<std::string> removed;
    };
    MidiRefreshResult refreshMidiInputs();

    // Graph topology
    int connect(int srcId, const std::string& srcPort,
                int dstId, const std::string& dstPort, std::string& error);
    bool disconnect(int connId);
    std::vector<Connection> getConnections() const;

    // Push internal graph to audio thread
    void updateGraph();
    // Legacy overload for tests with external graphs
    void updateGraph(const Graph& graph);

    // Parameters
    bool setParameter(int nodeId, const std::string& name, float value);
    float getParameter(int nodeId, const std::string& name) const;
    std::vector<std::string> getParameterNames(int nodeId) const;

    // Processing (public for testing without a device)
    void processBlock(juce::AudioBuffer<float>& outputBuffer,
                      juce::MidiBuffer& outputMidi,
                      int numSamples);

    // JUCE AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(...) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // For testing
    void prepareForTesting(double sampleRate, int blockSize);

    // Access internal graph
    Graph& getGraph();
};
```

## Owned State

| Member | Type | Purpose |
|--------|------|---------|
| `controlMutex_` | `std::mutex` | Serializes all control-plane access (see [ConcurrencyModel](ConcurrencyModel.md)) |
| `graph_` | `Graph` | Internal graph topology |
| `cache_` | `PluginCache` | Plugin description lookup |
| `formatManager_` | `AudioPluginFormatManager` | Plugin instantiation (initialized with default formats in constructor) |
| `ownedNodes_` | `unordered_map<int, unique_ptr<Node>>` | Lifetime ownership of all nodes |
| `nodeNames_` | `unordered_map<int, string>` | Human-readable name per node |
| `midiDeviceNodes_` | `unordered_map<string, int>` | MIDI device name to node ID tracking |
| `pendingDeletions_` | `vector<unique_ptr<Node>>` | Deferred node destruction for RT safety |

## Node Management

### addNode(node, name)
Takes ownership of a `unique_ptr<Node>`, registers it in `graph_`, `ownedNodes_`, and `nodeNames_`. Returns the graph node ID.

### removeNode(id)
1. Looks up node in `ownedNodes_`; returns false if not found
2. Removes from `graph_`
3. Removes from `midiDeviceNodes_` if present
4. Moves the `unique_ptr` to `pendingDeletions_` (deferred destruction — the audio thread's snapshot may still reference the node)
5. Removes from `ownedNodes_` and `nodeNames_`
6. Auto-calls `updateGraph()` to push a snapshot without the removed node
7. Returns true

### addPlugin(name, errorMessage)
Looks up plugin in cache, instantiates via `PluginNode::create()`, delegates to `addNode()`. Returns node ID or -1.

### addMidiInput(deviceName, errorMessage)
Creates a `MidiInputNode` via `MidiInputNode::create()`, delegates to `addNode()`, records in `midiDeviceNodes_`. Returns node ID or -1.

## MIDI Device Management

### autoLoadMidiInputs()
Enumerates all available MIDI input devices. For each device not already tracked in `midiDeviceNodes_`, calls `addMidiInput()`. Intended to be called once at startup.

### refreshMidiInputs()
1. Enumerates currently available MIDI devices
2. For each new device (available but not in `midiDeviceNodes_`): adds via `addMidiInput()`, records in `result.added`
3. For each disappeared device (in `midiDeviceNodes_` but not available): records in `result.removed` but does **not** remove the node (graceful degradation — `MidiInputNode::process()` outputs empty MIDI when the device is gone)
4. If any devices were added, calls `updateGraph()`
5. Returns `MidiRefreshResult{added, removed}`

## Per-Block Processing (processBlock)

1. **Drain Scheduler**: Process all pending commands
   - `swapGraph`: swap `activeSnapshot_` pointer, send old to garbage
   - `setParameter`: call `node->setParameterByIndex()`
2. **If no active snapshot**: write silence, return
3. **For each slot in execution order**:
   - Resolve audio input: source slot's `audioOutputs[i]` or `silenceBuffer`
   - Resolve MIDI input: source slot's `midiOutputs[i]` or `emptyMidi`
   - Clear this slot's output buffers
   - Call `node->process(context)`
4. **Sum leaf nodes to device output**: all audio leaf nodes (nodes with an audio output that no other node reads) have their outputs summed to the device buffer. This means parallel branches (e.g., two independent synths) are both heard. Mid-chain nodes are not double-counted. If no leaf nodes, silence.

## Building a Snapshot (updateGraph)

The no-arg `updateGraph()` pushes the internal `graph_`. The `updateGraph(const Graph&)` overload accepts an external graph (used by legacy tests).

1. Get execution order and connections from Graph
2. Map node IDs to slot indices
3. For each node in order, find which source slot feeds its audio and MIDI inputs
4. Pre-allocate output buffers sized to each node's output channel count
5. Mark each slot as `isAudioLeaf` if it has an audio output port but no other slot reads from it
6. Send snapshot via Scheduler `swapGraph` command

## Invariants

- `processBlock` is realtime-safe: no allocation, no blocking
- All buffers are pre-allocated in the snapshot on the control thread
- The audio thread only swaps pointers and reads pre-allocated data
- `updateGraph` can be called while audio is running (goes through Scheduler)
- Nodes are prepared (via `prepare()`) before being included in a snapshot
- Node IDs are stable — removing a node does not change other IDs
- `removeNode()` defers destruction to avoid use-after-free on the audio thread
- `removeNode()` auto-pushes an updated graph snapshot
- `refreshMidiInputs()` never removes nodes (preserves graph topology)
- `formatManager_` is initialized with default formats in the constructor

## Error Conditions

- `processBlock` with no active snapshot: outputs silence
- `start()` returns false if device fails to open
- Empty graph produces silence
- `removeNode(id)`: returns false if node not found
- `addPlugin(name)`: returns -1 if plugin not in cache or instantiation fails
- `addMidiInput(name)`: returns -1 if device not found
- `loadPluginCache(path)`: returns false if file not found or invalid
- `connect(...)`: returns -1 if nodes not found or Graph rejects connection
- `disconnect(id)`: returns false if connection not found
- `setParameter(id, ...)`: returns false if node not found
- `getParameter(id, ...)`: returns 0.0f if node not found
- `getParameterNames(id)`: returns empty vector if node not found

## Does NOT Handle

- Transport (no tempo, no position for this milestone)
- Meters
- Plugin scanning (uses pre-existing cache file)
- Plugin GUI / editor
- State save/restore
- Undo/redo

## Dependencies

- Scheduler (command queue)
- Graph (topology queries for building snapshots)
- PluginCache (plugin description lookup)
- PluginNode (plugin instantiation)
- MidiInputNode (MIDI device instantiation)
- Node (processing)
- Port (connection routing)
- JUCE (AudioIODeviceCallback, AudioBuffer, MidiBuffer, AudioPluginFormatManager, MidiInput)

## Thread Safety

All control-plane methods are serialized by `controlMutex_` and may be called from any thread (REPL, OSC, WebSocket, etc.). See [ConcurrencyModel](ConcurrencyModel.md) for the full thread model and lock ordering rules.

**Methods that lock `controlMutex_`:** `loadPluginCache`, `getAvailablePluginNames`, `findPluginByName`, `addNode`, `removeNode`, `getNode`, `getNodeName`, `getNodes`, `addPlugin`, `getAvailableMidiInputs`, `addMidiInput`, `autoLoadMidiInputs`, `refreshMidiInputs`, `connect`, `disconnect`, `getConnections`, `updateGraph` (both overloads), all parameter methods, all buffer methods, `prepareForTesting`, `audioDeviceAboutToStart`.

**Methods that do NOT lock:** `start()` (delegates to `audioDeviceAboutToStart` which locks), `stop()` (atomic + deviceManager only), `isRunning()`, `getSampleRate()`, `getBlockSize()` (atomic reads), `processBlock()` / `audioDeviceIOCallbackWithContext()` (audio thread — never locks), `audioDeviceStopped()` (atomic only), `getGraph()` (test-only reference return).
