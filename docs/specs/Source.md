# Source Specification

## Responsibilities

- Wrap a generator Processor (synth plugin, sampler, audio input) as a sound source
- Own a Chain for insert effects
- Apply channel gain and pan (post-chain, pre-bus-summing)
- Manage bus routing: which Bus this source feeds
- Manage sends: pre-fader or post-fader taps for effects/monitor routing
- Hold a MIDI input assignment (device + channel + note range filter)
- Support bypass
- Orchestrate processing: generator → chain → gain/pan → ready for bus summing

## Overview

A Source is a sound generator with its insert chain, channel strip controls, and routing. It is the mixer-centric equivalent of an instrument Node + its connection chain. Sources are owned by the Engine and processed independently (parallelizable in the future).

Each Source has:
- A **generator** Processor that produces audio (synth plugin, sampler, audio hardware input)
- A **Chain** of insert effects (EQ, compressor, etc.) that process the generated audio in-place
- **Gain and pan** — dedicated channel strip controls, applied after the chain
- A **bus assignment** determining which Bus receives this source's output
- A list of **sends** — each a (Bus, level, tap point) tuple, tapping either pre-fader or post-fader
- A **MIDI assignment** — device filter, channel filter, and note range filter for MIDI routing
- A **bypass** flag

The channel strip structure is:

```
generator → chain (inserts) → [pre-fader tap] → gain+pan → [post-fader tap] → bus summing
```

Gain and pan are **not** insert effects — they are dedicated stages with semantic meaning. This enables pre-fader sends, PFL (pre-fader listen), and scene systems that can distinguish channel volume from effect parameters.

## Bypass Semantics

"Bypass" means "skip processing to save CPU." The term is used at three levels in the system, with the same core meaning but different audio consequences depending on where in the signal flow the bypass occurs:

| Level | What is skipped | Audio result | Sends affected | On resume |
|-------|----------------|--------------|----------------|-----------|
| **Processor bypass** | One processor in a chain | Audio passes through unchanged | No — audio still flows | `reset()` on the processor |
| **Source bypass** | Generator + entire chain | Silence (no audio generated) | Yes — sends get nothing | `reset()` on generator + all chain processors |
| **Bus bypass** | Bus chain processing | Silence (summed input discarded) | Yes — sends get nothing | `reset()` on all chain processors |

The common thread: bypass = no processing, state goes stale, `reset()` on resume.

To silence a source while preserving processing state (e.g., so a reverb tail continues building), set `gain = 0.0` instead. When gain is restored, the processor state is current because processing never stopped.

## Interface

### Shared Types

```cpp
namespace squeeze {

enum class SendTap { preFader, postFader };

struct Send {
    Bus* bus;
    float levelDb;  // send level in dB (0.0 = unity, -inf = silent)
    SendTap tap;    // where to tap the signal
    int id;         // unique send ID (monotonically increasing, never reused)
};

struct MidiAssignment {
    std::string device;    // "" = any device
    int channel;           // 0 = all channels, 1-16 = specific channel
    int noteLow;           // 0-127, low end of note range filter
    int noteHigh;          // 0-127, high end of note range filter

    static MidiAssignment all() { return {"", 0, 0, 127}; }
    static MidiAssignment none() { return {"", -1, 0, 0}; }  // channel -1 = disabled
};

} // namespace squeeze
```

### C++ (`squeeze::Source`)

```cpp
namespace squeeze {

class Source {
public:
    Source(const std::string& name, std::unique_ptr<Processor> generator);
    ~Source();

    // Non-copyable, non-movable
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;

    // --- Lifecycle (control thread) ---
    void prepare(double sampleRate, int blockSize);
    void release();

    // --- Identity ---
    const std::string& getName() const;
    int getHandle() const;
    void setHandle(int h);

    // --- Generator ---
    Processor* getGenerator() const;
    void setGenerator(std::unique_ptr<Processor> generator);

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

    // --- MIDI assignment (control thread) ---
    void setMidiAssignment(const MidiAssignment& assignment);
    MidiAssignment getMidiAssignment() const;

    // --- Bypass (control thread write, audio thread read) ---
    void setBypassed(bool bypassed);
    bool isBypassed() const;

    // --- Processing (audio thread, RT-safe) ---
    // Generator → chain only. Engine applies gain/pan and handles sends.
    void process(juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi);

    // --- Latency ---
    int getLatencySamples() const;

private:
    std::string name_;
    int handle_ = -1;
    std::unique_ptr<Processor> generator_;
    Chain chain_;
    std::atomic<float> gain_{1.0f};    // linear gain
    std::atomic<float> pan_{0.0f};     // -1.0 to 1.0
    Bus* outputBus_ = nullptr;
    std::vector<Send> sends_;
    MidiAssignment midiAssignment_ = MidiAssignment::none();
    std::atomic<bool> bypassed_{false};
    int nextSendId_ = 1;
};

} // namespace squeeze
```

## Processing

Source processing produces the post-chain audio. The Engine orchestrates the full channel strip pipeline:

```
Engine processing for each source:

    if source.isBypassed():
        buffer.clear()
        wasBypassed = true
        return

    if wasBypassed:
        source.generator->reset()
        source.chain.resetAll()       // calls reset() on each processor
        wasBypassed = false

    source.process(buffer, midi)     ← generator → chain (in-place)

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
```

`Source::process()` itself only runs the generator and chain:

```
process(buffer, midi):
    generator->process(buffer, midi)    // generate audio (in-place)
    chain.process(buffer, midi)         // insert effects (in-place)
```

The Engine is responsible for:
1. Providing the buffer (pre-allocated in the snapshot)
2. Providing the MIDI buffer (from MidiRouter, filtered by this source's MidiAssignment)
3. Checking bypass state
4. Tapping pre-fader sends after `process()` returns
5. Applying gain and pan
6. Tapping post-fader sends
7. Adding the buffer to the output bus's input accumulator

## Bus Routing

Every Source routes to exactly one Bus (its "output bus"). By default, a newly created Source routes to the Master bus.

```python
vocal.route_to(vocal_bus)     # change output bus
vocal.route_to(engine.master) # route directly to master
```

The Engine updates the bus input lists when routing changes, and rebuilds the snapshot.

## Sends

Sends are signal taps that copy the source's audio (scaled by level) to a destination bus. Each send has a **tap point** — either pre-fader or post-fader:

- **Pre-fader sends** tap after the insert chain but before gain and pan. Used for monitor mixes, cue/PFL, and any routing where the fader shouldn't affect the send level.
- **Post-fader sends** tap after gain and pan. Used for effects sends (reverb, delay) where the send level should follow the fader.

```python
vocal.send(reverb_bus, level=-6.0)                        # post-fader (default)
vocal.send(monitor_bus, level=0.0, tap="pre")             # pre-fader monitor mix
vocal.send(cue_bus, level=-3.0, tap="pre")                # pre-fader cue mix
```

Send levels are in dB. 0.0 = unity. Negative values attenuate.

Sends do not affect the main output — the full-level buffer still goes to the output bus (post gain/pan).

## Gain and Pan

Gain and pan are dedicated channel strip controls, separate from the insert chain:

- **Gain**: linear multiplier, default 1.0 (unity). Applied post-chain, pre-bus-summing. This is the channel fader.
- **Pan**: stereo placement, -1.0 (full left) to 1.0 (full right), default 0.0 (center). Applied after gain.

These are **not** insert effects. They are semantically distinct from processor parameters, enabling:
- Pre-fader sends and PFL taps
- Scene systems that can identify and morph channel volumes
- Consistent fader/pan behavior regardless of chain contents

Setting `gain = 0.0` achieves a traditional "mute" — the source is silenced but processing continues, so reverb tails and compressor state remain current.

```python
vocal.gain = 0.75       # fader at ~-2.5 dB
vocal.pan = -0.3        # slightly left
vocal.gain = 0.0        # "mute" — silent but still processing
```

## Bypass

When bypassed, the source skips all processing (generator and chain). The buffer is cleared to silence. Bypassed sources do not feed sends. **On un-bypass, the Engine calls `reset()` on the generator and all chain processors** to clear stale internal state. The Engine detects the bypassed→active transition by tracking per-source state across blocks on the audio thread.

```python
vocal.bypassed = True   # no processing, saves CPU, sends get nothing
vocal.bypassed = False  # resumes (generator + chain processors reset)
```

## Generator Swap

`setGenerator()` replaces the source's generator while preserving the entire channel strip (chain, routing, sends, gain, pan, bypass, MIDI assignment).

```python
synth = engine.add_source("Lead", plugin="Diva.vst3")
synth.chain.append("EQ.vst3")
synth.route_to(lead_bus)
synth.send(reverb_bus, level=-6.0)

# Later: swap Diva for Serum — everything else stays
synth.set_generator("Serum.vst3")
```

Sequence (control thread, under controlMutex_):
1. Engine creates the new Processor (via PluginManager)
2. Engine calls `prepare()` on the new processor with current sample rate and block size
3. Source stores the new generator
4. Engine rebuilds the snapshot
5. Engine defer-deletes the old generator (audio thread may still reference it until snapshot swap completes)

The new generator gets a new Processor handle (handles are never reused). Source latency is recalculated since the new generator may have different latency.

## MIDI Assignment

Each Source has a MidiAssignment that tells the MidiRouter which MIDI messages to deliver:

- **device**: filter by device name ("" = any device)
- **channel**: filter by channel (0 = all, 1-16 = specific)
- **noteLow/noteHigh**: filter by note range (0-127 inclusive)

The MidiRouter uses this assignment during dispatch to filter messages before delivering them to the Source's MIDI buffer.

```python
synth.midi_assign(device="Keylab", channel=1)
kick.midi_assign(device="MPD", channel=10, note_range=(36, 36))
```

## Latency

A Source's total latency is the generator's latency plus the chain's latency:

```
getLatencySamples():
    return generator->getLatencySamples() + chain.getLatencySamples()
```

Gain and pan have zero latency. This is used by PDC to compute compensation delays at each Bus merge point.

## Invariants

- A Source always has a generator Processor (never null after construction)
- A Source always routes to exactly one Bus (defaults to Master)
- The generator is processed before the chain
- Processing is RT-safe: no allocation, no blocking
- Send IDs are monotonically increasing, never reused
- MIDI assignment filtering is applied by MidiRouter, not by Source itself
- When bypassed, the source produces silence (zero buffer) — no processing occurs
- Bypassed sources do not feed sends
- Source latency = generator latency + chain latency
- Gain defaults to 1.0 (unity), pan defaults to 0.0 (center)
- Gain is applied after the insert chain and before bus summing
- Pan is applied after gain
- Pre-fader sends tap between chain output and gain; post-fader sends tap after gain+pan

## Error Conditions

- `routeTo(nullptr)`: no-op, logged at warn level
- `addSend()` with nullptr bus: returns -1
- `addSend()` with a bus that is the source's own output bus: allowed (unusual but valid)
- `removeSend()` with unknown ID: returns false
- `setSendLevel()` with unknown send ID: no-op
- `setSendTap()` with unknown send ID: no-op
- `setGenerator()` with null processor: no-op, logged at warn level
- `setGenerator()` with a plugin that fails to load: returns error, old generator is preserved
- `setGain()` with negative value: clamped to 0.0
- `setPan()` outside [-1.0, 1.0]: clamped

## Does NOT Handle

- **Bus summing** — Engine accumulates source output into the target bus
- **Send level scaling and accumulation** — Engine handles send taps
- **Gain/pan application** — Engine applies gain and pan between send phases
- **Pan law** — Engine applies the pan law (constant-power or constant-gain)
- **MIDI message filtering** — MidiRouter applies the MidiAssignment filter
- **Processor creation** — PluginManager creates processors; Source receives them
- **Snapshot mechanism** — Engine reads source state for snapshot build
- **Deferred deletion** — Engine handles garbage collection of removed processors
- **Audio buffer allocation** — Engine pre-allocates per-source buffers in the snapshot

## Dependencies

- Processor (generator and chain members)
- Chain (insert effects)
- Bus (routing target — forward declaration sufficient)
- JUCE (`juce::AudioBuffer<float>`, `juce::MidiBuffer`)

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| Constructor / Destructor | Control | |
| `prepare()` / `release()` | Control | Forwarded to generator and chain |
| `process()` | Audio | RT-safe, called via snapshot |
| `setGain()` / `setPan()` | Control | Atomic store |
| `getGain()` / `getPan()` | Any | Atomic load |
| `routeTo()` | Control | Under Engine's controlMutex_, triggers snapshot rebuild |
| `addSend()` / `removeSend()` / `setSendLevel()` / `setSendTap()` | Control | Under controlMutex_, triggers snapshot rebuild |
| `setMidiAssignment()` | Control | Under controlMutex_ |
| `getMidiAssignment()` | Control | Read-only |
| `setBypassed()` | Control | Atomic store |
| `isBypassed()` | Any | Atomic load |
| `setGenerator()` | Control | Under controlMutex_, triggers snapshot rebuild, defer-deletes old |
| `getGenerator()` / `getChain()` | Control | |
| `getName()` | Any | Immutable |
| `getLatencySamples()` | Control | Called during snapshot build |

## C ABI

```c
typedef void* SqSource;

// Source creation
SqSource sq_add_source_plugin(SqEngine engine, const char* name, const char* plugin_path, char** error);
SqSource sq_add_source_input(SqEngine engine, const char* name, int hw_channel);
void     sq_remove_source(SqEngine engine, SqSource src);

// Generator swap
SqProc sq_source_set_generator(SqEngine engine, SqSource src, const char* plugin_path, char** error);

// Routing
void sq_route(SqEngine engine, SqSource src, SqBus bus);
int  sq_send(SqEngine engine, SqSource src, SqBus bus, float level_db, int pre_fader);
void sq_remove_send(SqEngine engine, SqSource src, int send_id);
void sq_set_send_level(SqEngine engine, SqSource src, int send_id, float level_db);
void sq_set_send_tap(SqEngine engine, SqSource src, int send_id, int pre_fader);

// Gain and Pan
void  sq_source_set_gain(SqEngine engine, SqSource src, float linear);
float sq_source_get_gain(SqEngine engine, SqSource src);
void  sq_source_set_pan(SqEngine engine, SqSource src, float pan);
float sq_source_get_pan(SqEngine engine, SqSource src);

// MIDI
void sq_midi_assign(SqEngine engine, SqSource src, const char* device, int channel);
void sq_midi_note_range(SqEngine engine, SqSource src, int low, int high);

// Source chain (insert effects)
SqProc sq_source_append(SqEngine engine, SqSource src, const char* plugin_path);
SqProc sq_source_insert(SqEngine engine, SqSource src, int index, const char* plugin_path);
void   sq_source_remove(SqEngine engine, SqSource src, int index);
int    sq_source_chain_size(SqEngine engine, SqSource src);

// Bypass
void sq_source_set_bypassed(SqEngine engine, SqSource src, bool bypassed);
bool sq_source_is_bypassed(SqEngine engine, SqSource src);
```

## Example Usage

### Channel strip with pre-fader monitor send

```python
vocal = engine.add_source("Vocal", plugin="audio_input", channel=1)
vocal.chain.append("EQ.vst3")
vocal.chain.append("Compressor.vst3")
vocal.chain.append("DeEsser.vst3")
vocal.gain = 0.8
vocal.pan = -0.2
vocal.route_to(vocal_bus)
vocal.send(reverb_bus, level=-6.0)                   # post-fader (default)
vocal.send(monitor_bus, level=0.0, tap="pre")         # pre-fader monitor mix
```

### Synth with MIDI

```python
synth = engine.add_source("Lead", plugin="Diva.vst3")
synth.midi_assign(device="Keylab", channel=1)
synth.route_to(engine.master)
```

### Drum kit with per-slice sources

```python
kick = engine.add_source("Kick", sampler="kick.wav")
snare = engine.add_source("Snare", sampler="snare.wav")
hat = engine.add_source("Hat", sampler="hat.wav")

drum_bus = engine.add_bus("Drums")
kick.route_to(drum_bus)
snare.route_to(drum_bus)
hat.route_to(drum_bus)

kick.midi_assign(device="MPD", channel=10, note_range=(36, 36))
snare.midi_assign(device="MPD", channel=10, note_range=(38, 38))
hat.midi_assign(device="MPD", channel=10, note_range=(42, 42))
```

### Bypass

```python
vocal.bypassed = True   # no processing, saves CPU
vocal.bypassed = False  # resumes (generator + chain processors reset)
vocal.gain = 0.0        # "mute" — silent but processing continues (reverb tails preserved)
```

### C++ headless test

```cpp
Engine engine(44100.0, 512);

auto gen = std::make_unique<TestSynthProcessor>();
auto* src = engine.addSource("synth", std::move(gen));

// Insert an effect
auto eq = std::make_unique<GainProcessor>();
src->getChain().append(eq.release());

// Channel strip controls
src->setGain(0.75f);
src->setPan(-0.3f);

// Route to master (default)
assert(src->getOutputBus() == engine.getMaster());

// Add a pre-fader send
auto* monitorBus = engine.addBus("Monitor");
src->addSend(monitorBus, -3.0f, SendTap::preFader);

engine.render(512);
```
