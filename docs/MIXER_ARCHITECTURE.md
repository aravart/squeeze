# Mixer-Centric Architecture

## Core Insight

The requirements describe a mixer, not an arbitrary signal graph. The infrastructure should match.

A traditional DAW/live-performance audio engine has a fixed structure: sources generate audio, insert chains process it in-place, buses sum multiple streams, sends tap signals to effects returns. This structure is universal — from Ableton to Pro Tools to hardware digital mixers. A general-purpose node graph can represent this structure, but at the cost of enormous accidental complexity (ports, connections, topology sorts over hundreds of nodes, dynamic port management, cascading change detection).

This architecture replaces the general-purpose graph with purpose-built primitives that directly model the mixer structure. The result is fewer concepts, less code, better performance, and a Python API that reads like a mixer, not a circuit diagram.

---

## Primitives

### Processor

The base abstraction. Processes audio **in-place** on a buffer. No ports, no connections — just a buffer in, same buffer out.

```cpp
class Processor {
public:
    virtual ~Processor() = default;
    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void process(AudioBuffer& buffer) = 0;
    virtual void process(AudioBuffer& buffer, const MidiBuffer& midi) { process(buffer); }

    // Parameters
    virtual int getParameterCount() const = 0;
    virtual ParamDescriptor getParameterDescriptor(int index) const = 0;
    virtual float getParameter(const std::string& name) const = 0;
    virtual void setParameter(const std::string& name, float value) = 0;

    // Identity
    const std::string& getName() const;

    // Latency
    virtual int getLatencySamples() const { return 0; }
};
```

Concrete types:
- **PluginProcessor** — wraps a VST3/AU plugin
- **GainProcessor** — volume + pan
- **MeterProcessor** — peak/RMS measurement (read-only tap, passes audio through)
- **RecorderProcessor** — writes audio to disk or memory buffer (passes audio through)

### Chain

An ordered list of Processors. Calls each sequentially on the **same buffer** — zero-copy serial processing.

```cpp
class Chain {
public:
    void process(AudioBuffer& buffer, const MidiBuffer& midi) {
        for (auto* p : processors_)
            p->process(buffer, midi);
    }

    int getLatencySamples() const {
        int total = 0;
        for (auto* p : processors_)
            total += p->getLatencySamples();
        return total;
    }

    // Structural modification (control thread only)
    // Builds a new processor array internally; swapped atomically at next block boundary
    void append(Processor* p);
    void insert(int index, Processor* p);
    void remove(int index);
    void move(int from, int to);
};
```

This is the **insert rack**. Every source and every bus owns one.

### Source

A sound generator + its channel strip + routing. Wraps a Processor that produces audio (a synth plugin, a sampler, an audio input).

```
Source
├── generator: Processor*                    (synth, sampler, audio input)
├── chain: Chain                             (insert effects)
├── gain: float                              (channel fader, post-chain)
├── pan: float                               (stereo placement)
├── bypassed: bool                           (skip all processing, save CPU)
├── output_bus: Bus*                         (where this source is routed)
├── sends: [(Bus*, float level, SendTap)]    (pre or post fader taps)
└── midi_input: MidiAssignment               (device + channel filter)
```

Processing (Engine orchestrates the full pipeline, iterating snapshot arrays):
```
generator.process(buffer, midi)               // generate audio
for each proc in snapshot.chainProcessors:    // insert effects, in-place
    proc.process(buffer, midi)
── pre-fader send taps ──
apply gain + pan                              // channel strip
── post-fader send taps ──
→ bus summing
```

### Bus

A summing point + channel strip + routing.

```
Bus
├── inputs: [buffer references]              (from sources or other buses)
├── chain: Chain                             (insert effects)
├── gain: float                              (bus fader, post-chain)
├── pan: float                               (stereo placement)
├── bypassed: bool                           (skip processing, save CPU)
├── output_bus: Bus*                         (downstream bus, or master)
└── sends: [(Bus*, float level, SendTap)]    (pre or post fader taps)
```

Processing (Engine orchestrates the full pipeline, iterating snapshot arrays):
```
sum(input_buffers)                            // mix all inputs into bus buffer
for each proc in snapshot.chainProcessors:    // insert effects, in-place
    proc.process(buffer)
── pre-fader send taps ──
apply gain + pan                              // bus strip
── post-fader send taps ──
→ metering → downstream summing
```

### Master

A special Bus. Always exists. Final output destination. Its output goes to the audio device.

### Send

Not a separate object — just a routing entry on a Source or Bus: `(destination_bus, level, tap_point)`. Each send taps either **pre-fader** (after chain, before gain/pan) or **post-fader** (after gain/pan). Pre-fader sends are used for monitor mixes and cue; post-fader sends for effects (reverb, delay). During processing, the engine copies the buffer (scaled by send level) at the appropriate tap point into the destination bus's input list.

---

## Processing Loop

```
1. For each source (independent — parallelizable):
      if bypassed: clear buffer, skip
      source.generator.process(buffer, midi)
      for each proc in snapshot.chainProcessors:
          proc.process(buffer, midi)              // Engine iterates snapshot array
      tap pre-fader sends (copy buffer * level to send buses)
      apply source.gain + source.pan
      tap post-fader sends (copy buffer * level to send buses)
      add buffer to source.output_bus

2. For each bus (in dependency order — DAG sort over buses only):
      if bypassed: clear buffer, skip
      bus.sum(input_buffers)
      for each proc in snapshot.chainProcessors:
          proc.process(buffer)                    // Engine iterates snapshot array
      tap pre-fader sends
      apply bus.gain + bus.pan
      metering
      tap post-fader sends
      add buffer to downstream bus (or output for master)
```

The dependency sort is over **buses** (typically 2–8 in a session), not individual processors (potentially hundreds). This is trivially cheap.

---

## What's Eliminated

| Old Concept | Replacement |
|---|---|
| Node (with ports) | Processor (no ports, in-place) |
| Port / PortDescriptor | Gone — chains pass one buffer, buses sum explicitly |
| Connection | Gone — routing is source→bus, bus→bus, send→bus |
| Graph | Gone — bus DAG is implicit in routing assignments |
| GroupNode | Gone — Bus + Chain cover all composition patterns |
| Dynamic ports | Gone — chains grow/shrink, buses accept any number of inputs |
| Port export/unexport | Gone |
| Cascading change detection | Gone |
| Global IdAllocator | Gone — processors/buses identified by opaque handles |
| Topology sort over N nodes | Sort over M buses (M << N) |
| MidiSplitterNode | Gone — MIDI routing table replaces it |
| Doubled `sq_group_*` API | Gone — no hierarchy, one API surface |

---

## Composition Patterns

### Channel Strip

A Source with a populated insert chain:
```python
vocal = engine.add_source("Vocal", plugin="audio_input", channel=1)
vocal.chain.append(eq)
vocal.chain.append(compressor)
vocal.chain.append(desser)
vocal.route_to(vocal_bus)
```

### Effects Return

A Bus used as a shared effects processor:
```python
reverb_bus = engine.add_bus("Reverb")
reverb_bus.chain.append(reverb_plugin)
reverb_bus.route_to(master)

vocal.send(reverb_bus, level=-6.0)
guitar.send(reverb_bus, level=-12.0)
```

### Drum Kit

Multiple sources feeding one bus:
```python
kick = engine.add_source("Kick", sampler="kick.wav")
snare = engine.add_source("Snare", sampler="snare.wav")
hat = engine.add_source("Hat", sampler="hat.wav")

drum_bus = engine.add_bus("Drums")
kick.route_to(drum_bus)
snare.route_to(drum_bus)
hat.route_to(drum_bus)

drum_bus.chain.append(bus_compressor)
drum_bus.route_to(master)
```

MIDI routing assigns each source to a note range:
```python
kick.midi_assign(device="MPD", channel=10, note_range=(36, 36))
snare.midi_assign(device="MPD", channel=10, note_range=(38, 38))
hat.midi_assign(device="MPD", channel=10, note_range=(42, 42))
```

### Parallel Compression

```python
drum_bus = engine.add_bus("Drums Dry")
crush_bus = engine.add_bus("Drums Crush")
crush_bus.chain.append(heavy_compressor)

# Both buses feed master
drum_bus.route_to(master)
crush_bus.route_to(master)

# Drums source feeds both
drums.route_to(drum_bus)
drums.send(crush_bus, level=0.0)  # unity send to parallel bus
```

### Sidechain Compression

A compressor in one chain needs to read the signal from another source/bus as its sidechain trigger:
```python
bass = engine.add_source("Bass", plugin="bass_synth.vst3")
kick = engine.add_source("Kick", sampler="kick.wav")

sc_compressor = engine.add_plugin("compressor.vst3")
bass.chain.append(sc_compressor)

# Route kick's audio as sidechain input to the compressor
engine.sidechain(sc_compressor, source=kick)
```

The sidechain is a read-only auxiliary input — it doesn't affect the chain's in-place flow. The compressor reads the sidechain buffer for detection while processing the main (bass) buffer in-place.

---

## MIDI Model

MIDI routing is **separate from audio routing**. There is no "MIDI connection" between nodes.

- Each Source has a MIDI input assignment: device filter + channel filter + note range filter
- MIDI CC → parameter mappings are a routing table on the Engine
- Note events are scheduled sample-accurately via the Engine's event scheduler
- CC events are applied per-block (control rate)

```python
# MIDI assignments
synth.midi_assign(device="Keylab", channel=1)
drums.midi_assign(device="MPD", channel=10)

# CC mapping
engine.midi_map(device="Keylab", cc=74, target=filter_plugin, param="cutoff")
```

This replaces MidiRouter, MidiSplitterNode, and all MIDI-as-graph-connection complexity.

---

## Parameter Model

- Processors expose named parameters with metadata (range, default, display name)
- Addressed by **processor handle + parameter name** (not node ID + port name)
- Automation via the scheduler at control rate
- Plugin parameters come from the VST/AU plugin's own parameter list

```python
eq.set_param("high_frequency", 8000.0)
eq.set_param("high_gain", -3.0)
print(eq.get_param("high_gain"))        # -3.0
print(eq.param_descriptors())           # [ParamDescriptor(...), ...]
```

---

## Live Operation (Glitch-Free Structural Changes)

All structural changes happen on the **control thread**. The audio thread sees immutable snapshots.

| Operation | Mechanism |
|---|---|
| Insert effect into chain | Build new processor array, atomic swap at block boundary |
| Remove effect from chain | Build new array without it, swap, defer-delete old processor |
| Add source | Add to source list, assign to bus, swap source array |
| Remove source | Remove from array, swap, defer-delete |
| Add bus | Add to bus list, rebuild bus dependency order, swap |
| Bypass bus | Flag on bus — audio thread skips processing (zero-cost) |
| Change routing | Update bus assignment, rebuild bus input lists, swap |

The "snapshot" is lightweight — arrays of pointers to processors, arrays of bus input references. Not a deep copy of the entire engine state.

### Batching Changes

By default, every structural mutation (route, add send, insert processor, etc.) triggers an immediate snapshot rebuild. This is correct but wasteful when making multiple changes — especially when some involve slow operations like plugin loading.

The Engine provides an explicit batch mechanism to coalesce multiple mutations into a single snapshot rebuild:

```python
with engine.batch():
    synth = engine.add_source("Lead", plugin="Serum.vst3")  # slow: plugin load
    synth.chain.append("EQ.vst3")                           # slow: plugin load
    synth.chain.append("Compressor.vst3")                   # slow: plugin load
    synth.route_to(lead_bus)
    synth.send(reverb_bus, level=-6.0)
    synth.gain = 0.8
# single snapshot rebuild here
```

**Mechanism:**

- `batch_begin()` — sets a flag on the Engine. While set, mutations that would normally trigger a snapshot rebuild instead set `snapshotDirty_ = true` and return.
- `batch_commit()` — if `snapshotDirty_`, rebuilds the snapshot once. Clears both flags.
- Outside a batch, every mutation rebuilds immediately (dirty flag is irrelevant — the rebuild is inline).
- Batches do not nest. Calling `batch_begin()` while already in a batch is a no-op with a warning.
- If `batch_commit()` is never called (e.g., exception), the snapshot is stale until the next mutation outside a batch triggers a rebuild. The Python context manager guarantees `commit` via `__exit__`.

**What batching does NOT affect:**

- Atomic flag operations (`setBypassed`, `setGain`, `setPan`) take effect immediately — they don't need snapshot rebuilds because the audio thread reads them directly via atomics.
- Parameter changes (`setParameter`) — same, no snapshot involved.
- Only structural mutations (routing, chain modification, source/bus add/remove, generator swap) are deferred.

```c
// C ABI
void sq_batch_begin(SqEngine engine);
void sq_batch_commit(SqEngine engine);
```

---

## Latency Compensation (PDC)

PDC operates on the **bus DAG**:

1. Each Processor reports its latency (plugins report theirs; built-in processors return 0)
2. A Chain's latency = sum of its processor latencies
3. A Source's total latency = generator latency + chain latency
4. At each bus: find the maximum total latency among all inputs; insert compensation delays on the shorter paths

The DAG has ~4–8 nodes (buses). PDC computation is trivial compared to walking an arbitrary graph of hundreds of nodes.

---

## Metering and Recording

Both are **inline processors** in a chain, or **taps** on a source/bus output:

```python
# Inline meter (in the chain, measures signal at that point)
meter = engine.add_meter()
vocal.chain.append(meter)
print(meter.peak, meter.rms)

# Bus output meter (always available on every bus)
print(drum_bus.peak, drum_bus.rms)

# Recording
vocal.record_to_disk("vocal_take1.wav")
drum_bus.record_to_buffer()             # capture to memory
```

---

## Transport and Tempo

The Engine owns a Transport:
- BPM, time signature, beat position, playing state
- Hosted plugins receive transport info via VST/AU host callbacks
- The scheduler uses transport position for time-based automation

```python
engine.transport.bpm = 120.0
engine.transport.playing = True
```

---

## C ABI (Sketch)

The C ABI is dramatically simpler. No graph management, no port wiring, no group variants.

```c
// Lifecycle
SqEngine    sq_create(double sample_rate, int block_size);
void        sq_destroy(SqEngine);
int         sq_start(SqEngine);
int         sq_stop(SqEngine);

// Sources
SqSource    sq_add_source_plugin(SqEngine, const char* plugin_path);
SqSource    sq_add_source_sampler(SqEngine);
SqSource    sq_add_source_input(SqEngine, int hw_channel);
void        sq_remove_source(SqEngine, SqSource);

// Buses
SqBus       sq_add_bus(SqEngine, const char* name);
void        sq_remove_bus(SqEngine, SqBus);
SqBus       sq_master(SqEngine);

// Routing
void        sq_route(SqEngine, SqSource, SqBus);
int         sq_send(SqEngine, SqSource, SqBus, float level_db, int pre_fader);
void        sq_bus_route(SqEngine, SqBus from, SqBus to);
int         sq_bus_send(SqEngine, SqBus from, SqBus to, float level_db, int pre_fader);

// Gain / Pan
void        sq_source_set_gain(SqEngine, SqSource, float linear);
void        sq_source_set_pan(SqEngine, SqSource, float pan);
void        sq_bus_set_gain(SqEngine, SqBus, float linear);
void        sq_bus_set_pan(SqEngine, SqBus, float pan);

// Bypass
void        sq_source_set_bypassed(SqEngine, SqSource, int bypassed);
void        sq_bus_set_bypassed(SqEngine, SqBus, int bypassed);

// Insert chains
SqProc      sq_source_insert(SqEngine, SqSource, int index, const char* plugin_path);
SqProc      sq_source_append(SqEngine, SqSource, const char* plugin_path);
void        sq_source_remove_proc(SqEngine, SqSource, int index);

SqProc      sq_bus_insert(SqEngine, SqBus, int index, const char* plugin_path);
SqProc      sq_bus_append(SqEngine, SqBus, const char* plugin_path);
void        sq_bus_remove_proc(SqEngine, SqBus, int index);

// Parameters
float       sq_get_param(SqEngine, SqProc, const char* name);
void        sq_set_param(SqEngine, SqProc, const char* name, float value);
int         sq_param_count(SqEngine, SqProc);
const char* sq_param_name(SqEngine, SqProc, int index);
// ... param descriptors ...

// MIDI
void        sq_midi_assign(SqEngine, SqSource, const char* device, int channel);
void        sq_midi_note_range(SqEngine, SqSource, int low, int high);
void        sq_midi_cc_map(SqEngine, SqProc, const char* param, int cc_number);

// Plugin GUI
void        sq_editor_open(SqEngine, SqProc);
void        sq_editor_close(SqEngine, SqProc);

// Metering
float       sq_peak(SqEngine, SqBus);
float       sq_rms(SqEngine, SqBus);

// Recording
SqRec       sq_record_disk(SqEngine, SqBus, const char* path);
SqRec       sq_record_buffer(SqEngine, SqBus);
void        sq_record_stop(SqEngine, SqRec);

// Transport
void        sq_set_tempo(SqEngine, double bpm);
void        sq_set_playing(SqEngine, int playing);
double      sq_get_position_beats(SqEngine);
```

---

## Python API (Sketch)

```python
from squeeze import Engine

engine = Engine(sample_rate=44100, block_size=128)

# Create sources
synth = engine.add_source("Lead", plugin="Diva.vst3")
drums = engine.add_source("Drums", sampler=True)

# Create buses
drum_bus = engine.add_bus("Drum Bus")
reverb_bus = engine.add_bus("Reverb")

# Insert effects
drum_bus.chain.append("SSL_Channel.vst3")
drum_bus.chain.append("API_2500.vst3")
reverb_bus.chain.append("ValhallaRoom.vst3")

# Routing
synth.route_to(engine.master)
synth.send(reverb_bus, level=-6.0)           # post-fader (default)
drums.route_to(drum_bus)
drum_bus.route_to(engine.master)
reverb_bus.route_to(engine.master)
# Gain / Pan
synth.gain = 0.8
synth.pan = -0.2
drums.gain = 1.0
drum_bus.gain = 0.9

# Monitor mix (pre-fader sends)
monitor_bus = engine.add_bus("Monitor")
synth.send(monitor_bus, level=0.0, tap="pre")
drums.send(monitor_bus, level=-3.0, tap="pre")

# MIDI
synth.midi_assign(device="Keylab", channel=1)
drums.midi_assign(device="MPD", channel=10)

# Parameters
reverb_bus.chain[0].set_param("decay", 2.5)
reverb_bus.chain[0].set_param("mix", 0.3)

# Transport
engine.transport.bpm = 128
engine.transport.playing = True

# Live: hot-swap an insert
saturator = drum_bus.chain.insert(1, "Saturator.vst3")
# ... later ...
drum_bus.chain.remove(saturator)

# Metering
print(drum_bus.peak, drum_bus.rms)

# Recording
drum_bus.record("drum_bus_take1.wav")

engine.start()
```
