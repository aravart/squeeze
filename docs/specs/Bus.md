# Bus Specification

## Responsibilities

- Sum N input buffers from Sources and/or other Buses
- Own a Chain for insert effects
- Apply bus gain and pan (post-chain, pre-downstream-summing)
- Route to another Bus or to the Master (output)
- Manage sends: pre-fader or post-fader taps for effects routing
- Provide metering: peak and RMS levels always available
- Support bypass

## Overview

A Bus is a summing point with an insert chain, channel strip controls, and routing. It collects audio from multiple Sources (and/or other Buses), sums them, processes the sum through its insert chain, applies gain and pan, and routes the result to a downstream Bus or the Master. Buses are the mixer-centric equivalent of the audio graph's fan-in + GroupNode pattern.

The **Master** bus is a special Bus that always exists, cannot be removed, and whose output goes to the audio device. All bus DAGs ultimately terminate at the Master.

The bus channel strip structure is:

```
inputs summed → chain (inserts) → [pre-fader tap] → gain+pan → metering → [post-fader tap] → downstream
```

**Sends** are routing entries on a Source or Bus: `(destination_bus, level_db, tap_point)`. During processing, the Engine copies the buffer (scaled by send level) at the appropriate tap point into the destination bus's input accumulator.

## Bypass Semantics

See the Source spec for the full bypass semantics table. Bus bypass follows the same pattern:

- **Bypassed**: chain processing is skipped, buffer is cleared to silence, sends get nothing, metering reads 0. Saves CPU.
- **On un-bypass**: Engine calls `reset()` on all chain processors to clear stale state. Engine detects the transition by tracking per-bus bypass state across blocks on the audio thread.
- **Gain = 0.0** achieves a traditional "mute" — processing continues (chain runs, state stays current), output is silent.

## Interface

### C++ (`squeeze::Bus`)

```cpp
namespace squeeze {

class Bus {
public:
    Bus(const std::string& name, bool isMaster = false);
    ~Bus();

    // Non-copyable, non-movable
    Bus(const Bus&) = delete;
    Bus& operator=(const Bus&) = delete;

    // --- Lifecycle (control thread) ---
    void prepare(double sampleRate, int blockSize);
    void release();

    // --- Identity ---
    const std::string& getName() const;
    int getHandle() const;
    void setHandle(int h);
    bool isMaster() const;

    // --- Insert chain ---
    Chain& getChain();
    const Chain& getChain() const;

    // --- Gain and Pan (control thread write, audio thread read) ---
    void setGain(float linear);    // linear gain, default 1.0 (unity)
    float getGain() const;
    void setPan(float pan);        // -1.0 (left) to 1.0 (right), default 0.0 (center)
    float getPan() const;

    // --- Bus routing (control thread) ---
    void routeTo(Bus* bus);
    Bus* getOutputBus() const;

    // --- Sends (control thread) ---
    int addSend(Bus* bus, float levelDb, SendTap tap = SendTap::postFader);
    bool removeSend(int sendId);
    void setSendLevel(int sendId, float levelDb);
    void setSendTap(int sendId, SendTap tap);
    std::vector<Send> getSends() const;

    // --- Bypass (control thread write, audio thread read) ---
    void setBypassed(bool bypassed);
    bool isBypassed() const;

    // --- Metering (audio thread writes, any thread reads) ---
    float getPeak() const;
    float getRMS() const;
    void updateMetering(const juce::AudioBuffer<float>& buffer, int numSamples);
    void resetMetering();

    // --- Latency ---
    int getLatencySamples() const;

private:
    std::string name_;
    int handle_ = -1;
    bool master_;
    Chain chain_;
    std::atomic<float> gain_{1.0f};    // linear gain
    std::atomic<float> pan_{0.0f};     // -1.0 to 1.0
    Bus* outputBus_ = nullptr;
    std::vector<Send> sends_;
    std::atomic<bool> bypassed_{false};
    int nextSendId_ = 1;

    // Metering (atomics for cross-thread read)
    std::atomic<float> peak_{0.0f};
    std::atomic<float> rms_{0.0f};
};

} // namespace squeeze
```

## Processing

Bus processing is orchestrated by the Engine (see Engine spec, `processSubBlock` for the definitive pseudocode):

```
Engine processing for each bus (in dependency order):

    if bus.isBypassed():
        buffer.clear()
        bus.resetMetering()
        continue

    // On bypass→active transition:
    //   reset all chain processors (via wasBypassed_ on each Processor)

    // buffer already contains the sum of all inputs (Engine handles accumulation)
    // Engine iterates snapshot.busChainProcessors with per-processor bypass tracking

    // Pre-fader send taps
    for send in bus.sends where send.tap == preFader:
        sendBus.accumulate(buffer * dbToLinear(send.levelDb))

    // Channel strip: gain + pan
    buffer.applyGain(bus.getGain())
    applyPan(buffer, bus.getPan())

    bus.updateMetering(buffer)

    // Post-fader send taps
    for send in bus.sends where send.tap == postFader:
        sendBus.accumulate(buffer * dbToLinear(send.levelDb))

    // Route downstream (or output to device for Master)
    if !bus.isMaster():
        outputBus.accumulate(buffer)
```

The chain is iterated via the snapshot's processor array (not by calling `Chain::process()` — see Chain spec). The Engine handles all audio-thread processing.

The **Engine** is responsible for:
1. Pre-allocating the bus buffer in the snapshot
2. Accumulating source outputs and upstream bus outputs into the bus buffer (summing)
3. Iterating chain processors (via snapshot array) with per-processor bypass tracking
4. Tapping pre-fader sends
5. Applying gain and pan
6. Updating metering
7. Tapping post-fader sends
8. Routing to downstream bus or audio device
9. Processing buses in dependency order (DAG sort over buses)

### Bus Dependency Order

Buses form a DAG through their `routeTo` relationships and sends. The Engine sorts buses in dependency order: a bus is processed only after all buses it depends on (via sends) have been processed. The Master bus is always processed last.

Typical session: 2-8 buses. The dependency sort is trivially cheap.

### Summing

The Engine sums input buffers into the bus buffer before calling `process()`:

```
bus_buffer.clear()
for each source routed to this bus:
    bus_buffer += source_output_buffer
for each upstream bus routed to this bus:
    bus_buffer += upstream_bus_output_buffer
for each send targeting this bus:
    bus_buffer += send_source_buffer * db_to_linear(send_level)
```

## Master Bus

The Master is a Bus with `isMaster() == true`:

- Created automatically by the Engine at construction
- Cannot be removed
- Cannot route to another bus (`routeTo()` is a no-op on Master)
- Its output goes directly to the audio device
- Newly created Sources route to Master by default
- Newly created Buses route to Master by default

```python
# Master is always available
engine.master.chain.append("Limiter.vst3")
engine.master.gain = 0.9
print(engine.master.peak)
```

## Sends

Sends are signal taps, identical to Source sends. Each send has a tap point — pre-fader or post-fader:

- **Pre-fader sends** tap after the insert chain but before gain and pan.
- **Post-fader sends** tap after gain and pan.

```python
drum_bus.send(reverb_bus, level=-6.0)                     # post-fader (default)
drum_bus.send(parallel_bus, level=0.0, tap="pre")         # pre-fader parallel processing
```

Bus sends enable effects returns, parallel processing, and complex routing topologies.

**Cycle detection**: The Engine must detect and reject send configurations that would create cycles in the bus DAG. For example, bus A sending to bus B while bus B sends to bus A is a cycle.

## Gain and Pan

Gain and pan are dedicated bus controls, separate from the insert chain:

- **Gain**: linear multiplier, default 1.0 (unity). Applied post-chain, pre-downstream-summing. This is the bus fader.
- **Pan**: stereo placement, -1.0 (full left) to 1.0 (full right), default 0.0 (center). Applied after gain.

Setting `gain = 0.0` achieves a traditional "mute" — processing continues (chain runs, state stays current), output is silent.

```python
drum_bus.gain = 0.85
drum_bus.pan = 0.1       # slightly right
drum_bus.gain = 0.0      # "mute" — silent but still processing
```

## Metering

Every Bus provides peak and RMS levels, updated on every audio block:

- **Peak**: maximum absolute sample value across all channels
- **RMS**: root-mean-square level across all channels

Metering is updated **post gain/pan** — it reflects the actual signal level going downstream.

Metering values are written atomically by the audio thread and read by any thread (typically the control thread for UI updates).

```python
print(drum_bus.peak)    # e.g., 0.85
print(drum_bus.rms)     # e.g., 0.42
print(engine.master.peak)
```

## Bypass

When bypassed, the bus skips chain processing and produces silence. Bypassed buses do not feed sends or downstream buses. Metering is reset to 0. **On un-bypass, the Engine calls `reset()` on all chain processors** to clear stale internal state. The Engine detects the bypassed→active transition by tracking per-bus state across blocks on the audio thread.

```python
drum_bus.bypassed = True    # no processing, saves CPU
drum_bus.bypassed = False   # resumes (chain processors reset)
```

## Latency

A Bus's chain latency is the sum of its chain processor latencies:

```
getLatencySamples():
    return chain.getLatencySamples()
```

Gain and pan have zero latency. PDC uses this, along with Source latencies, to compute compensation delays at each bus merge point.

## Invariants

- Master bus always exists and cannot be removed
- Master bus does not route to another bus
- A bus routes to exactly one downstream bus (or is the Master)
- Bus buffers are pre-allocated in the snapshot — no allocation on the audio thread
- Metering values are atomic — safe to read from any thread
- Metering reflects post gain/pan levels
- Send IDs are monotonically increasing, never reused
- The bus DAG is acyclic — the Engine rejects routing/send changes that would create cycles
- When bypassed, the bus produces silence and metering reads 0
- Newly created buses default to routing to Master
- Gain defaults to 1.0 (unity), pan defaults to 0.0 (center)
- Pre-fader sends tap between chain output and gain; post-fader sends tap after gain+pan

## Error Conditions

- `routeTo(nullptr)`: no-op, logged at warn level
- `routeTo()` on Master: no-op (Master cannot route elsewhere)
- `routeTo()` that would create a cycle: rejected by Engine, logged at warn, returns error
- `addSend()` with nullptr bus: returns -1
- `addSend()` that would create a cycle: rejected by Engine, returns -1
- `removeSend()` with unknown ID: returns false
- `setSendLevel()` with unknown send ID: no-op
- `setSendTap()` with unknown send ID: no-op
- `setGain()` with negative value: clamped to 0.0
- `setPan()` outside [-1.0, 1.0]: clamped

## Does NOT Handle

- **Source processing** — Sources process independently before feeding the bus
- **Buffer allocation** — Engine pre-allocates bus buffers in the snapshot
- **Input accumulation (summing)** — Engine accumulates source/bus outputs into the bus buffer
- **Gain/pan application** — Engine applies gain and pan between send phases
- **Send level scaling** — Engine handles send tap copies
- **Bus dependency sort** — Engine sorts buses in processing order
- **Audio device output** — Engine copies Master output to device
- **Processor creation** — PluginManager creates PluginProcessors
- **Snapshot mechanism** — Engine builds and swaps snapshots
- **Cycle detection** — Engine validates routing changes before applying them
- **Pan law** — Engine applies the pan law

## Dependencies

- Chain (insert effects)
- Processor (chain members)
- Send struct, SendTap enum (defined in Source.md, shared between Source and Bus)
- JUCE (`juce::AudioBuffer<float>`)

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| Constructor / Destructor | Control | |
| `prepare()` / `release()` | Control | Forwarded to chain |
| `setGain()` / `setPan()` | Control | Atomic store |
| `getGain()` / `getPan()` | Any | Atomic load |
| `routeTo()` | Control | Under Engine's controlMutex_, triggers snapshot rebuild |
| `addSend()` / `removeSend()` / `setSendLevel()` / `setSendTap()` | Control | Under controlMutex_ |
| `setBypassed()` | Control | Atomic store |
| `isBypassed()` | Any | Atomic load |
| `getPeak()` / `getRMS()` | Any | Atomic load |
| `updateMetering()` | Audio | Atomic store |
| `getChain()` | Control | |
| `getName()` | Any | Immutable |
| `getLatencySamples()` | Control | Called during snapshot build |

## C ABI

```c
typedef void* SqBus;

// Bus creation
SqBus sq_add_bus(SqEngine engine, const char* name);
void  sq_remove_bus(SqEngine engine, SqBus bus);
SqBus sq_master(SqEngine engine);

// Routing
bool sq_bus_route(SqEngine engine, SqBus from, SqBus to, char** error);
int  sq_bus_send(SqEngine engine, SqBus from, SqBus to, float level_db, int pre_fader);
void sq_bus_remove_send(SqEngine engine, SqBus bus, int send_id);
void sq_bus_set_send_level(SqEngine engine, SqBus bus, int send_id, float level_db);
void sq_bus_set_send_tap(SqEngine engine, SqBus bus, int send_id, int pre_fader);

// Gain and Pan
void  sq_bus_set_gain(SqEngine engine, SqBus bus, float linear);
float sq_bus_get_gain(SqEngine engine, SqBus bus);
void  sq_bus_set_pan(SqEngine engine, SqBus bus, float pan);
float sq_bus_get_pan(SqEngine engine, SqBus bus);

// Bus chain (insert effects)
SqProc sq_bus_append(SqEngine engine, SqBus bus, const char* plugin_path);
SqProc sq_bus_insert(SqEngine engine, SqBus bus, int index, const char* plugin_path);
void   sq_bus_remove_proc(SqEngine engine, SqBus bus, int index);
int    sq_bus_chain_size(SqEngine engine, SqBus bus);

// Bypass
void sq_bus_set_bypassed(SqEngine engine, SqBus bus, bool bypassed);
bool sq_bus_is_bypassed(SqEngine engine, SqBus bus);

// Metering
float sq_bus_peak(SqEngine engine, SqBus bus);
float sq_bus_rms(SqEngine engine, SqBus bus);
```

## Example Usage

### Effects return

```python
reverb_bus = engine.add_bus("Reverb")
reverb_bus.chain.append("ValhallaRoom.vst3")
reverb_bus.route_to(engine.master)

vocal.send(reverb_bus, level=-6.0)
guitar.send(reverb_bus, level=-12.0)
```

### Drum bus with gain

```python
drum_bus = engine.add_bus("Drums")
kick.route_to(drum_bus)
snare.route_to(drum_bus)
hat.route_to(drum_bus)

drum_bus.chain.append("SSL_Channel.vst3")
drum_bus.chain.append("API_2500.vst3")
drum_bus.gain = 0.9
drum_bus.route_to(engine.master)

print(drum_bus.peak, drum_bus.rms)
```

### Parallel compression with pre-fader send

```python
dry_bus = engine.add_bus("Drums Dry")
crush_bus = engine.add_bus("Drums Crush")
crush_bus.chain.append(heavy_compressor)

dry_bus.route_to(engine.master)
crush_bus.route_to(engine.master)

drums.route_to(dry_bus)
drums.send(crush_bus, level=0.0, tap="pre")  # pre-fader: unaffected by dry_bus fader
```

### Bypass

```python
drum_bus.bypassed = True    # no processing, saves CPU
drum_bus.bypassed = False   # resumes (chain processors reset)
drum_bus.gain = 0.0         # "mute" — silent but still processing
```

### C++ headless test

```cpp
Engine engine(44100.0, 512);

auto* bus = engine.addBus("FX");
auto gain = std::make_unique<GainProcessor>();
bus->getChain().append(std::move(gain));
bus->setGain(0.85f);
bus->setPan(0.1f);
bus->routeTo(engine.getMaster());

auto gen = std::make_unique<TestSynthProcessor>();
auto* src = engine.addSource("synth", std::move(gen));
src->routeTo(bus);

engine.render(512);

float peak = bus->getPeak();
float rms = bus->getRMS();
```
