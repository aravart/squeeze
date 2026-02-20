# Engine Specification

## Responsibilities

- Own all Sources and Buses (including the Master bus)
- Own `CommandQueue` (lock-free control → audio SPSC bridge)
- Own `Transport` (tempo, position, loop state)
- Own `EventScheduler` (beat-timed event resolution)
- Own `PerfMonitor` (audio thread instrumentation)
- Own `MidiRouter` (MIDI device queue dispatch in processBlock)
- Provide the **Master bus** — the always-present final output destination
- Build and swap lightweight snapshots (source/bus arrays, routing tables, processor arrays)
- Execute `processBlock()`: drain commands, advance transport, resolve events, process sources, accumulate bus inputs, process buses in dependency order, output master
- Manage garbage collection (deferred deletion of old snapshots)
- Detect and reject routing changes that would create bus DAG cycles
- Accept `sampleRate` and `blockSize` as constructor parameters (immutable for the engine's lifetime)
- Assign opaque handles to processors, sources, and buses
- Report the engine version string

## Overview

Engine is the core processing kernel of Squeeze v2. It coordinates a mixer-centric architecture: Sources generate audio through insert chains, Buses sum and process audio through their own insert chains, and the Master bus outputs to the audio device. There is no general-purpose node graph, no ports, no connections, and no topological sort over individual processors.

The processing loop is:
1. Process all sources independently (generator → chain)
2. Accumulate source outputs + sends into bus input buffers
3. Process buses in dependency order (bus DAG — typically 2-8 buses)
4. Process Master bus, output to audio device

The control thread acquires `controlMutex_` for all mutations. The audio thread calls `processBlock()` without locking — it reads an immutable snapshot and drains lock-free queues.

## Interface

### C++ (`squeeze::Engine`)

```cpp
namespace squeeze {

class Engine {
public:
    Engine(double sampleRate, int blockSize);
    ~Engine();

    std::string getVersion() const;  // Returns "0.3.0"
    double getSampleRate() const;
    int getBlockSize() const;

    // --- Source management (control thread) ---
    Source* addSource(const std::string& name, std::unique_ptr<Processor> generator);
    bool removeSource(Source* src);
    Source* getSource(int handle) const;
    std::vector<Source*> getSources() const;

    // --- Bus management (control thread) ---
    Bus* addBus(const std::string& name);
    bool removeBus(Bus* bus);
    Bus* getBus(int handle) const;
    std::vector<Bus*> getBuses() const;
    Bus* getMaster() const;

    // --- Routing (control thread) ---
    void route(Source* src, Bus* bus);
    int  sendFrom(Source* src, Bus* bus, float levelDb, SendTap tap = SendTap::postFader);
    void removeSend(Source* src, int sendId);
    void setSendLevel(Source* src, int sendId, float levelDb);
    void setSendTap(Source* src, int sendId, SendTap tap);

    bool busRoute(Bus* from, Bus* to);
    int  busSend(Bus* from, Bus* to, float levelDb, SendTap tap = SendTap::postFader);
    void busRemoveSend(Bus* bus, int sendId);
    void busSendLevel(Bus* bus, int sendId, float levelDb);
    void busSendTap(Bus* bus, int sendId, SendTap tap);

    // --- Insert chains (control thread) ---
    // Source chain operations
    Processor* sourceAppend(Source* src, std::unique_ptr<Processor> p);
    Processor* sourceInsert(Source* src, int index, std::unique_ptr<Processor> p);
    void sourceRemove(Source* src, int index);

    // Bus chain operations
    Processor* busAppend(Bus* bus, std::unique_ptr<Processor> p);
    Processor* busInsert(Bus* bus, int index, std::unique_ptr<Processor> p);
    void busRemove(Bus* bus, int index);

    // --- Parameters (control thread) ---
    float getParameter(int procHandle, const std::string& name) const;
    bool setParameter(int procHandle, const std::string& name, float value);
    std::string getParameterText(int procHandle, const std::string& name) const;
    std::vector<ParamDescriptor> getParameterDescriptors(int procHandle) const;

    // --- MIDI (control thread) ---
    void midiAssign(Source* src, const std::string& device, int channel);
    void midiNoteRange(Source* src, int low, int high);
    void midiCCMap(int procHandle, const std::string& paramName, int ccNumber);

    // --- Metering (any thread) ---
    float busPeak(Bus* bus) const;
    float busRMS(Bus* bus) const;

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
    bool scheduleNoteOn(Source* src, double beatTime, int channel, int note, float velocity);
    bool scheduleNoteOff(Source* src, double beatTime, int channel, int note);
    bool scheduleCC(Source* src, double beatTime, int channel, int ccNum, int ccVal);
    bool scheduleParamChange(int procHandle, double beatTime, const std::string& paramName, float value);

    // --- PDC (control thread) ---
    void setPDCEnabled(bool enabled);
    bool isPDCEnabled() const;
    int getProcessorLatency(int procHandle) const;
    int getTotalLatency() const;

    // --- Batching (control thread) ---
    void batchBegin();   // defer snapshot rebuilds until batchCommit()
    void batchCommit();  // rebuild snapshot if dirty, clear batch flag

    // --- Audio processing (audio thread) ---
    void processBlock(float** outputChannels, int numChannels, int numSamples);

    // --- Testing ---
    void render(int numSamples);  // process one block in test mode

    // --- Query ---
    int getSourceCount() const;
    int getBusCount() const;

private:
    mutable std::mutex controlMutex_;

    // Sources and Buses
    std::vector<std::unique_ptr<Source>> sources_;
    std::vector<std::unique_ptr<Bus>> buses_;
    Bus* master_ = nullptr;  // always the first bus, never removed

    // Handle allocation
    int nextHandle_ = 1;

    // Processor handle registry (all processors across all sources and buses)
    std::unordered_map<int, Processor*> processorRegistry_;

    // Snapshot — raw pointer, not atomic. Only the audio thread reads/writes
    // activeSnapshot_ (set during commandQueue drain, read during processBlock).
    // The control thread never accesses it — it sends new snapshots via
    // CommandQueue, which provides the memory ordering guarantees.
    struct MixerSnapshot;
    MixerSnapshot* activeSnapshot_ = nullptr;

    // Sub-components
    CommandQueue commandQueue_;
    Transport transport_;
    EventScheduler eventScheduler_;
    PerfMonitor perfMonitor_;
    MidiRouter midiRouter_;

    bool pdcEnabled_ = true;
    bool batching_ = false;
    bool snapshotDirty_ = false;

    // Internal
    void buildAndSwapSnapshot();
    void handleCommand(const Command& cmd);
    bool wouldCreateCycle(Bus* from, Bus* to) const;
    int assignHandle();
};

} // namespace squeeze
```

**PlayHead wiring:** All add/append/insert methods call `setPlayHead(&transport_)` on new processors after `prepare()`. This includes `addSource()` (on the generator), `sourceAppend()`, `sourceInsert()`, `busAppend()`, and `busInsert()` (on the new chain processor). This allows processors like PluginProcessor and PlayerProcessor to access transport state (tempo, position) during processing.

### C ABI (`squeeze_ffi.h`)

```c
typedef void* SqEngine;
typedef void* SqSource;
typedef void* SqBus;
typedef void* SqProc;

// Lifecycle
SqEngine sq_create(double sample_rate, int block_size, char** error);
void     sq_destroy(SqEngine engine);
char*    sq_version(SqEngine engine);
void     sq_free_string(char* s);

// Sources
SqSource sq_add_source_plugin(SqEngine engine, const char* name,
                               const char* plugin_path, char** error);
SqSource sq_add_source_input(SqEngine engine, const char* name, int hw_channel);
void     sq_remove_source(SqEngine engine, SqSource src);
int      sq_source_count(SqEngine engine);

// Buses
SqBus    sq_add_bus(SqEngine engine, const char* name);
void     sq_remove_bus(SqEngine engine, SqBus bus);
SqBus    sq_master(SqEngine engine);
int      sq_bus_count(SqEngine engine);

// Routing
bool     sq_route(SqEngine engine, SqSource src, SqBus bus, char** error);
int      sq_send(SqEngine engine, SqSource src, SqBus bus, float level_db, int pre_fader);
void     sq_remove_send(SqEngine engine, SqSource src, int send_id);
void     sq_set_send_level(SqEngine engine, SqSource src, int send_id, float level_db);
void     sq_set_send_tap(SqEngine engine, SqSource src, int send_id, int pre_fader);
bool     sq_bus_route(SqEngine engine, SqBus from, SqBus to, char** error);
int      sq_bus_send(SqEngine engine, SqBus from, SqBus to, float level_db, int pre_fader);
void     sq_bus_remove_send(SqEngine engine, SqBus bus, int send_id);
void     sq_bus_set_send_level(SqEngine engine, SqBus bus, int send_id, float level_db);
void     sq_bus_set_send_tap(SqEngine engine, SqBus bus, int send_id, int pre_fader);

// Insert chains
SqProc   sq_source_append(SqEngine engine, SqSource src, const char* plugin_path);
SqProc   sq_source_insert(SqEngine engine, SqSource src, int index, const char* plugin_path);
SqProc   sq_source_append_proc(SqEngine engine, SqSource src, SqProc proc);
SqProc   sq_source_insert_proc(SqEngine engine, SqSource src, int index, SqProc proc);
void     sq_source_remove_proc(SqEngine engine, SqSource src, int index);
void     sq_source_move(SqEngine engine, SqSource src, int from_index, int to_index);
int      sq_source_chain_size(SqEngine engine, SqSource src);

SqProc   sq_bus_append(SqEngine engine, SqBus bus, const char* plugin_path);
SqProc   sq_bus_insert(SqEngine engine, SqBus bus, int index, const char* plugin_path);
SqProc   sq_bus_append_proc(SqEngine engine, SqBus bus, SqProc proc);
SqProc   sq_bus_insert_proc(SqEngine engine, SqBus bus, int index, SqProc proc);
void     sq_bus_remove_proc(SqEngine engine, SqBus bus, int index);
void     sq_bus_move(SqEngine engine, SqBus bus, int from_index, int to_index);
int      sq_bus_chain_size(SqEngine engine, SqBus bus);

// Parameters (by processor handle)
float    sq_get_param(SqEngine engine, SqProc proc, const char* name);
void     sq_set_param(SqEngine engine, SqProc proc, const char* name, float value);
char*    sq_param_text(SqEngine engine, SqProc proc, const char* name);
int      sq_param_count(SqEngine engine, SqProc proc);
SqParamDescriptorList sq_param_descriptors(SqEngine engine, SqProc proc);

// MIDI
void     sq_midi_assign(SqEngine engine, SqSource src, const char* device, int channel);
void     sq_midi_note_range(SqEngine engine, SqSource src, int low, int high);
void     sq_midi_cc_map(SqEngine engine, SqProc proc, const char* param, int cc_number);

// Metering
float    sq_bus_peak(SqEngine engine, SqBus bus);
float    sq_bus_rms(SqEngine engine, SqBus bus);

// PDC
void     sq_set_pdc_enabled(SqEngine engine, bool enabled);
bool     sq_pdc_enabled(SqEngine engine);
int      sq_proc_latency(SqEngine engine, SqProc proc);
int      sq_total_latency(SqEngine engine);

// Plugin editor
void     sq_editor_open(SqEngine engine, SqProc proc);
void     sq_editor_close(SqEngine engine, SqProc proc);

// Transport
void     sq_transport_play(SqEngine engine);
void     sq_transport_stop(SqEngine engine);
void     sq_transport_pause(SqEngine engine);
void     sq_transport_set_tempo(SqEngine engine, double bpm);
void     sq_transport_set_time_signature(SqEngine engine, int numerator, int denominator);
void     sq_transport_seek_samples(SqEngine engine, int64_t samples);
void     sq_transport_seek_beats(SqEngine engine, double beats);
void     sq_transport_set_loop_points(SqEngine engine, double start_beats, double end_beats);
void     sq_transport_set_looping(SqEngine engine, bool enabled);
double   sq_transport_position(SqEngine engine);
double   sq_transport_tempo(SqEngine engine);
bool     sq_transport_is_playing(SqEngine engine);

// Event scheduling
bool     sq_schedule_note_on(SqEngine engine, SqSource src, double beat_time,
                              int channel, int note, float velocity);
bool     sq_schedule_note_off(SqEngine engine, SqSource src, double beat_time,
                               int channel, int note);
bool     sq_schedule_cc(SqEngine engine, SqSource src, double beat_time,
                         int channel, int cc_num, int cc_val);
bool     sq_schedule_param_change(SqEngine engine, SqProc proc, double beat_time,
                                   const char* param_name, float value);

// Batching
void     sq_batch_begin(SqEngine engine);
void     sq_batch_commit(SqEngine engine);

// Testing
void     sq_render(SqEngine engine, int num_samples);

// Message pump
void     sq_pump(void);
```

### FFI List Types

```c
typedef struct {
    char** names;
    float* default_values;
    float* min_values;
    float* max_values;
    int*   num_steps;       // 0 = continuous, >0 = stepped
    bool*  automatable;
    bool*  booleans;
    char** labels;          // unit: "dB", "Hz", "%", ""
    char** groups;          // "" = ungrouped
    int count;
} SqParamDescriptorList;

typedef struct {
    char** items;
    int count;
} SqStringList;

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

`sq_create(sr, bs)` allocates an `EngineHandle` on the heap. The returned opaque `SqEngine` pointer is the only handle the caller needs. Peripheral components (AudioDevice, PluginManager, BufferLibrary, MidiDeviceManager) are peers of Engine within the handle — Engine never knows about them.

**FFI orchestration:** Some `sq_*` functions span multiple components. For example, `sq_add_source_plugin(handle, "Lead", "Diva.vst3", &err)` calls `handle->pluginManager.createProcessor("Diva.vst3", ...)` then `handle->engine.addSource("Lead", std::move(processor))`. Similarly, `sq_source_append(handle, src, "EQ.vst3")` creates a processor via PluginManager, then appends it to the source's chain.

## processBlock Sequence

Called by AudioDevice (or test harness) on the audio thread. Must be fully RT-safe.

```
processBlock(outputChannels, numChannels, numSamples):

  1. perfMonitor_.beginBlock()

  2. commandQueue_.processPending([this](cmd) { handleCommand(cmd); })
     — swapSnapshot: install new, push old to garbage
     — transport commands: forward to transport_
     — transportStop / seek: clear EventScheduler, send all-notes-off
       to every source MidiBuffer (prevents stuck notes from
       scheduled noteOns whose matching noteOffs were discarded)

  3. if no active snapshot → write silence, return

  4. transport_.advance(numSamples)
     — detect loop boundary → compute splitSample

  5. if loop wraps mid-block:
       processSubBlock(0, splitSample, prewrapBeats)
       processSubBlock(splitSample, numSamples, postwrapBeats)
     else:
       processSubBlock(0, numSamples, blockBeats)

  6. copy master bus buffer → outputChannels

  7. perfMonitor_.endBlock()
```

### processSubBlock

```
processSubBlock(startSample, endSample, beatRange):

  1. eventScheduler_.retrieve(beatStart, beatEnd, subSamples, tempo, sr, ...)
     — resolve beat-timed events to sample offsets

  2. dispatch MIDI events → source MidiBuffers (via MidiRouter + assignments)
     collect paramChange events → sub-block split points

  3. For each source (independent — future: parallelizable):

       if source.isBypassed():
           buffer.clear()
           wasBypassed = true
           continue

       if wasBypassed:
           source.generator->reset()
           for each proc in snapshot.sourceChainProcessors:
               proc->reset()
               proc->wasBypassed_ = false
           wasBypassed = false

       // Generator
       source.generator->process(buffer, midiBuffer)

       // Chain (iterate snapshot array, not Chain object)
       for each proc in snapshot.sourceChainProcessors:
           bypassed = proc->isBypassed()
           if !bypassed && proc->wasBypassed_:
               proc->reset()
           if !bypassed:
               proc->process(buffer, midiBuffer)
           proc->wasBypassed_ = bypassed

       // Pre-fader send taps
       for send in source.sends where send.tap == preFader:
           sendBus.accumulate(buffer * dbToLinear(send.levelDb))

       // Channel strip: gain + pan
       buffer.applyGain(source.getGain())
       applyPan(buffer, source.getPan())

       // Post-fader send taps
       for send in source.sends where send.tap == postFader:
           sendBus.accumulate(buffer * dbToLinear(send.levelDb))

       // Route to output bus
       outputBus.accumulate(buffer)

  4. For each bus in dependency order:

       // Sum accumulated inputs (already done via accumulate() above)

       if bus.isBypassed():
           buffer.clear()
           wasBypassed = true
           continue

       if wasBypassed:
           for each proc in snapshot.busChainProcessors:
               proc->reset()
               proc->wasBypassed_ = false
           wasBypassed = false

       // Chain (iterate snapshot array)
       for each proc in snapshot.busChainProcessors:
           bypassed = proc->isBypassed()
           if !bypassed && proc->wasBypassed_:
               proc->reset()
           if !bypassed:
               proc->process(buffer)
           proc->wasBypassed_ = bypassed

       // Pre-fader send taps
       for send in bus.sends where send.tap == preFader:
           sendBus.accumulate(buffer * dbToLinear(send.levelDb))

       // Gain + pan
       buffer.applyGain(bus.getGain())
       applyPan(buffer, bus.getPan())

       // Post-fader send taps
       for send in bus.sends where send.tap == postFader:
           sendBus.accumulate(buffer * dbToLinear(send.levelDb))

       // Metering (atomic writes)
       bus.updateMetering(buffer)

       // Route to downstream bus
       downstreamBus.accumulate(buffer)

  5. Master bus: same as step 4, always processed last
```

## MixerSnapshot (internal)

`MixerSnapshot` is a lightweight, immutable structure built by the control thread and swapped into the audio thread atomically via CommandQueue. Much simpler than the graph-based `GraphSnapshot`.

```cpp
struct MixerSnapshot {
    // Source processing arrays
    struct SourceEntry {
        Source* source;
        std::vector<Processor*> chainProcessors;  // snapshot of chain state
        juce::AudioBuffer<float> buffer;           // pre-allocated
        juce::MidiBuffer midiBuffer;               // pre-allocated
        Bus* outputBus;
        std::vector<Send> sends;
    };
    std::vector<SourceEntry> sources;

    // Bus processing arrays (in dependency order)
    struct DelayLine { /* circular buffer, pre-allocated */ };
    struct BusEntry {
        Bus* bus;
        std::vector<Processor*> chainProcessors;   // snapshot of chain state
        juce::AudioBuffer<float> buffer;            // pre-allocated
        std::vector<Send> sends;
        int compensationDelay;                      // PDC: max input path delay
        std::vector<DelayLine> delayLines;          // PDC: per-input compensation
    };
    std::vector<BusEntry> buses;  // sorted in dependency order, master last

    int totalLatency;
};
```

**Key property:** All buffers are pre-allocated at snapshot build time. The audio thread never allocates — it writes into existing buffers.

**Lifecycle:** Built by `buildAndSwapSnapshot()` on the control thread → sent via CommandQueue → installed on the audio thread → old snapshot pushed to garbage queue → freed on control thread by `collectGarbage()`.

## Garbage Collection

Engine calls `commandQueue_.collectGarbage()` at the top of every control-thread method that acquires `controlMutex_`. This ensures old snapshots and other deferred-deletion items are freed regularly on the control thread, without requiring an external timer or caller discipline.

## Bus DAG Cycle Detection

When routing is changed (`route()`, `busRoute()`, `sendFrom()`, `busSend()`), the Engine checks whether the new routing would create a cycle in the bus DAG. The check is a simple BFS/DFS from the destination bus following downstream routing and sends — if it reaches the source bus, a cycle would be created.

With ~4-8 buses, this check is trivially cheap.

**Error reporting:** All four routing methods must report a rejected cycle. `route()` and `busRoute()` return `bool` (`false` on cycle). `sendFrom()` and `busSend()` return `-1` on cycle. All four log at warn level with the source and destination handles/names so the caller can diagnose which routing change was rejected. The FFI functions `sq_route()` and `sq_bus_route()` additionally set `*error` to a caller-freeable string describing the cycle (e.g. `"routing bus 'Drums' -> bus 'Reverb' would create a cycle"`).

## Invariants

- `sq_create` returns a non-NULL handle on success
- `sq_create` returns NULL and sets `*error` on failure
- `sq_destroy(NULL)` is a no-op
- `sq_free_string(NULL)` is a no-op
- `sq_version` returns a non-NULL string that the caller must free with `sq_free_string`
- Multiple engines can be created and destroyed independently
- JUCE MessageManager is initialized exactly once, on the first `sq_create` call
- `processBlock()` is fully RT-safe: no allocation, no blocking, no unbounded work
- The audio thread never modifies sources, buses, or chains — it reads the immutable MixerSnapshot
- The Master bus always exists and cannot be removed
- Newly created Sources default to routing to Master
- Newly created Buses default to routing to Master
- Bus routing forms a DAG — cycles are rejected
- Every structural mutation triggers a snapshot rebuild
- `controlMutex_` serializes all control-thread operations
- Garbage is collected at the top of every control-thread operation
- Sample rate and block size are immutable — set at construction, never changed
- The engine is always prepared from birth — no separate preparation step needed
- `render()` allocates the output buffer internally — it is a testing convenience, not RT-safe
- Processor handles are globally unique, monotonically increasing, never reused
- Transport stop and seek send all-notes-off (CC 123, value 0, all channels) to every source MidiBuffer before the next processSubBlock, preventing stuck notes from orphaned EventScheduler noteOns

## Error Conditions

- `sq_create`: allocation failure or JUCE init failure → returns NULL, sets `*error`
- `sq_create`: NULL `error` pointer is safe — error message is discarded
- `addSource()` with null generator: returns nullptr
- `removeSource()` with unknown source: returns false
- `removeBus()` with Master: returns false (cannot remove Master)
- `removeBus()` with unknown bus: returns false
- `route()` / `busRoute()` that would create a cycle: returns `false`, does not modify routing. C++ logs at warn level with source and destination identifiers: `SQ_WARN("route: rejected, would create cycle: source %d -> bus %d", srcHandle, busHandle)`. FFI sets `*error` to a descriptive message (caller frees with `sq_free_string`). NULL `error` pointer is safe — message is discarded.
- `sendFrom()` / `busSend()` that would create a cycle: returns -1, logs at warn level with source and destination identifiers
- `getParameter()` with unknown handle: returns 0.0f
- `setParameter()` with unknown handle: returns false
- `scheduleNoteOn()` with full EventScheduler queue: returns false
- `commandQueue_.sendCommand()` full: snapshot deleted by caller, logged at warn

## Does NOT Handle

- **Audio device management** — AudioDevice wraps JUCE AudioDeviceManager, calls processBlock()
- **JUCE MessageManager / message pump** — FFI-level `sq_pump()`, not Engine
- **Plugin scanning and instantiation** — PluginManager returns `unique_ptr<Processor>`, Engine just owns them
- **Buffer/sample loading and management** — BufferLibrary owns Buffers with IDs
- **MIDI device open/close and routing rules** — MidiDeviceManager wraps MidiRouter
- **Processor creation** — PluginManager creates PluginProcessors, Engine receives them

## Dependencies

- Source (sound generators with chains and routing)
- Bus (summing points with chains)
- Chain (ordered processor lists)
- Processor (the abstract processing interface)
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
| `sq_create` / `sq_destroy` | Control | Not concurrent with other calls |
| `addSource()` / `removeSource()` | Control | Acquires `controlMutex_` |
| `addBus()` / `removeBus()` | Control | Acquires `controlMutex_` |
| `route()` / `sendFrom()` / routing | Control | Acquires `controlMutex_`, triggers snapshot rebuild |
| `sourceAppend()` / chain ops | Control | Acquires `controlMutex_`, triggers snapshot rebuild |
| `getParameter()` / `setParameter()` | Control | Acquires `controlMutex_` |
| `transportPlay()` / etc. | Control | Acquires `controlMutex_`, sends command |
| `scheduleNoteOn()` / etc. | Control | Acquires `controlMutex_`, writes to EventScheduler |
| `processBlock()` | Audio | Never locks, reads snapshot, drains queues |
| `busPeak()` / `busRMS()` | Any | Atomic reads |
| `render()` | Control | Acquires `controlMutex_`, calls processBlock internally |

JUCE init is guarded by a static flag (single-threaded assumption for first create call).

## Example Usage

### C ABI

```c
#include "squeeze_ffi.h"

int main() {
    char* error = NULL;
    SqEngine engine = sq_create(44100.0, 512, &error);
    if (!engine) {
        fprintf(stderr, "Failed: %s\n", error);
        sq_free_string(error);
        return 1;
    }

    // Create a source with a plugin
    SqSource synth = sq_add_source_plugin(engine, "Lead", "Diva.vst3", &error);

    // Create a bus and route
    SqBus drum_bus = sq_add_bus(engine, "Drums");
    if (!sq_bus_route(engine, drum_bus, sq_master(engine), &error)) {
        fprintf(stderr, "Route failed: %s\n", error);
        sq_free_string(error);
    }

    // Insert effects
    SqProc eq = sq_bus_append(engine, drum_bus, "EQ.vst3");
    sq_set_param(engine, eq, "high_gain", 0.4f);  // normalized 0-1 for plugins

    // MIDI
    sq_midi_assign(engine, synth, "Keylab", 1);

    // Transport
    sq_transport_set_tempo(engine, 120.0);
    sq_transport_play(engine);

    // Metering
    printf("Master peak: %f\n", sq_bus_peak(engine, sq_master(engine)));

    sq_destroy(engine);
    return 0;
}
```

### Python

```python
from squeeze import Squeeze

s = Squeeze()

synth = s.add_source("Lead", plugin="Diva.vst3")
drums = s.add_source("Drums", sampler=True)

drum_bus = s.add_bus("Drum Bus")
reverb_bus = s.add_bus("Reverb")

drums.route_to(drum_bus)
drum_bus.chain.append("SSL_Channel.vst3")
drum_bus.route_to(s.master)

synth.route_to(s.master)
synth.send(reverb_bus, level=-6.0)

reverb_bus.chain.append("ValhallaRoom.vst3")
reverb_bus.route_to(s.master)

synth.midi_assign(device="Keylab", channel=1)

s.transport.tempo = 128
s.transport.playing = True

s.start()
```

### Headless testing (C++)

```cpp
Engine engine(44100.0, 512);

auto gen = std::make_unique<TestSynthProcessor>();
auto* src = engine.addSource("synth", std::move(gen));
// Source defaults to routing to Master

engine.render(512);  // process one block

float peak = engine.busPeak(engine.getMaster());
```
