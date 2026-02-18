# PDC (Plugin Delay Compensation) Specification

## Responsibilities

- Compute per-input compensation delays at snapshot build time so that audio paths arriving at a bus merge point are time-aligned
- Apply delays via ring buffers during `processBlock` bus input summation
- Report per-processor latency and total mixer latency through the public API
- Toggle PDC on/off (disable for low-latency live monitoring scenarios)
- Detect latency changes during `buildAndSwapSnapshot()` and recompute compensation

## Overview

Plugins report processing latency (look-ahead compressors, linear-phase EQs, oversampled saturators). When multiple audio paths merge at a bus, shorter paths must be delayed to match the longest path — otherwise the signals are misaligned and phase cancellation occurs.

PDC operates on the **bus DAG**, not an arbitrary node graph. This makes the algorithm dramatically simpler:

1. Each Processor reports its latency via `getLatencySamples()`
2. A Chain's latency = sum of its processor latencies
3. A Source's total latency = generator latency + chain latency
4. At each bus: find the maximum total latency among all inputs; insert compensation delays on shorter paths

The bus DAG has ~4-8 nodes. PDC computation is trivial.

### What PDC does NOT do

- **MIDI compensation.** Only audio paths are delayed. MIDI is not compensated — standard DAW behavior.
- **Fractional-sample delays.** Latency is integer samples only. No interpolation.
- **Preserve delay state across snapshots.** A topology or latency change rebuilds all delay lines from zero.

## Interface

### C++ — Processor (already defined)

```cpp
class Processor {
public:
    virtual int getLatencySamples() const { return 0; }
};
```

PluginProcessor overrides to delegate to `processor_->getLatencySamples()`.

### C++ — Engine additions

```cpp
class Engine {
public:
    void setPDCEnabled(bool enabled);
    bool isPDCEnabled() const;
    int getProcessorLatency(int procHandle) const;
    int getTotalLatency() const;

private:
    bool pdcEnabled_ = true;
};
```

### C++ — MixerSnapshot additions

```cpp
struct MixerSnapshot {
    // ... existing fields ...

    struct BusEntry {
        // ... existing fields ...
        // Per-input compensation delays
        struct InputDelay {
            int delaySamples;         // 0 = no compensation needed
            std::unique_ptr<DelayLine> delayLine;  // only allocated when > 0
        };
        std::vector<InputDelay> inputDelays;  // parallel to input list
    };

    int totalLatency;
};
```

### C ABI

```c
void sq_set_pdc_enabled(SqEngine engine, bool enabled);
bool sq_pdc_enabled(SqEngine engine);
int  sq_proc_latency(SqEngine engine, SqProc proc);
int  sq_total_latency(SqEngine engine);
```

### Python

```python
# On Squeeze:
class Squeeze:
    @property
    def pdc_enabled(self) -> bool: ...

    @pdc_enabled.setter
    def pdc_enabled(self, enabled: bool) -> None: ...

    @property
    def total_latency(self) -> int: ...

# On Processor:
class Processor:
    @property
    def latency(self) -> int: ...
```

## Algorithm

### Latency propagation (runs in `buildAndSwapSnapshot()`)

```
Input:  sources (list of Source*)
        buses (list of Bus*, in dependency order)
        pdcEnabled flag

Output: per-bus-input compensation delays
        totalLatency

1. Compute source latencies:
     For each source:
       sourceLatency[source] = source.generator.getLatencySamples()
                             + source.chain.getLatencySamples()

2. For each bus (in dependency order):
     a. Collect all input latencies:
        - From sources: sourceLatency[source]
        - From upstream buses: busOutputLatency[upstreamBus]
        - From sends: sourceLatency[sendSource] or busOutputLatency[sendBus]

     b. Find maxInputLatency = max of all input latencies

     c. For each input:
        if pdcEnabled:
            compensationDelay = maxInputLatency - inputLatency
        else:
            compensationDelay = 0

     d. busOutputLatency[bus] = maxInputLatency + bus.chain.getLatencySamples()

3. totalLatency = busOutputLatency[master]
```

### Worked example

```
Sources:
  synth (latency: 0)
  vocal (generator: 0, chain: EQ=256 + Comp=512 → chain latency: 768)
    → sourceLatency[vocal] = 768

Both route to Master (no intermediate bus).

At Master:
  inputs: [synth (0), vocal (768)]
  maxInputLatency = 768
  synth compensation = 768 - 0 = 768 samples  ← DELAY APPLIED
  vocal compensation = 768 - 768 = 0
  busOutputLatency[master] = 768 + master.chain.getLatencySamples()

Result: synth path gets a 768-sample delay ring buffer.
```

### processBlock bus input summation

The existing summation:
```
for each input to this bus:
    bus_buffer += input_buffer
```

Becomes:
```
for each input to this bus:
    if input has DelayLine (compensationDelay > 0):
        delayLine.write(input_buffer, numSamples)
        delayLine.read(bus_buffer, numSamples, addTo=true)
    else:
        bus_buffer += input_buffer
```

## DelayLine

A minimal RT-safe ring buffer for integer-sample delays. Lives inside `MixerSnapshot`.

```cpp
struct DelayLine {
    std::vector<std::vector<float>> buffer;  // [channel][delaySamples]
    int writePos = 0;
    int delaySamples = 0;
    int numChannels = 0;

    void allocate(int channels, int delay);
    void write(const juce::AudioBuffer<float>& src, int numSamples);
    void read(juce::AudioBuffer<float>& dst, int numSamples, bool addTo);
};
```

- Buffer is zero-initialized at allocation
- First `delaySamples` of output will be silence (delay line filling up)
- ~30 lines of code
- No JUCE dependency beyond `juce::AudioBuffer` for the I/O interface

## Invariants

- `getLatencySamples()` returns >= 0
- Compensation delays are >= 0 for every bus input
- At any bus, `compensationDelay + inputLatency` is equal for all inputs (when PDC enabled)
- `totalLatency` = max cumulative path latency to master output
- Delay ring buffers are pre-allocated at snapshot build time
- DelayLine operations are RT-safe
- When PDC is disabled, all compensation delays are 0
- Delay line state is not preserved across snapshot swaps

## Error Conditions

- `sq_proc_latency()` with unknown handle: returns 0
- `sq_total_latency()` with no snapshot built: returns 0
- `sq_set_pdc_enabled()` when audio is running: takes effect on next snapshot rebuild
- A processor reports negative latency: clamped to 0, logged at warn

## Does NOT Handle

- **MIDI delay compensation** — only audio is compensated
- **Fractional-sample delays** — integer samples only
- **Delay state preservation across topology changes** — rebuilt from zero
- **Per-processor PDC bypass** — PDC is global
- **Automatic latency change detection mid-block** — only queried during snapshot build
- **Parallel thread processing** — assumes single-threaded execution order

## Dependencies

- **Processor** — `getLatencySamples()` virtual
- **PluginProcessor** — overrides to delegate to `processor_->getLatencySamples()`
- **Chain** — `getLatencySamples()` returns sum of processor latencies
- **Source** — `getLatencySamples()` returns generator + chain latency
- **Bus** — `getLatencySamples()` returns chain latency
- **Engine** — `buildAndSwapSnapshot()` runs the compensation algorithm; `processBlock()` applies delays
- **MixerSnapshot** — DelayLine storage, `totalLatency` field
- **JUCE** — `AudioProcessor::getLatencySamples()`, `juce::AudioBuffer<float>`

## Thread Safety

| Method / Operation | Thread | Notes |
|---|---|---|
| `Processor::getLatencySamples()` | Control | Called during snapshot build |
| `PluginProcessor::getLatencySamples()` | Control | Delegates to JUCE processor |
| `Engine::setPDCEnabled()` | Control | Acquires controlMutex_, triggers rebuild |
| `Engine::isPDCEnabled()` | Control | Acquires controlMutex_ |
| `Engine::getProcessorLatency()` | Control | Acquires controlMutex_ |
| `Engine::getTotalLatency()` | Control | Acquires controlMutex_ |
| Compensation computation | Control | Inside buildAndSwapSnapshot() |
| DelayLine allocation | Control | Inside buildAndSwapSnapshot() |
| `DelayLine::write()` / `read()` | Audio | RT-safe |

## Example Usage

### C ABI

```c
SqEngine engine = sq_create(44100.0, 512, &error);

SqSource vocal = sq_add_source_plugin(engine, "Vocal", "audio_input", &error);
SqProc eq = sq_source_append(engine, vocal, "Linear Phase EQ");

printf("EQ latency: %d samples\n", sq_proc_latency(engine, eq));
printf("Total latency: %d samples\n", sq_total_latency(engine));

// Disable for live monitoring
sq_set_pdc_enabled(engine, false);

sq_destroy(engine);
```

### Python

```python
s = Squeeze()

vocal = s.add_source("Vocal", plugin="audio_input")
eq = vocal.chain.append("Linear Phase EQ")
comp = vocal.chain.append("Look-Ahead Compressor")

synth = s.add_source("Synth", plugin="Diva.vst3")

# Both route to master — PDC will delay the synth path
print(f"EQ latency: {eq.latency}")
print(f"Total latency: {s.total_latency}")

# Disable for live use
s.pdc_enabled = False
```

### Headless testing (C++)

```cpp
Engine engine(44100.0, 512);

auto gen1 = std::make_unique<TestSynthProcessor>();
auto* src1 = engine.addSource("dry", std::move(gen1));

auto gen2 = std::make_unique<TestSynthProcessor>();
auto* src2 = engine.addSource("wet", std::move(gen2));

auto latencyProc = std::make_unique<TestLatencyProcessor>(256);
src2->getChain().append(latencyProc.release());

// Both route to master — dry path should get 256 samples compensation
assert(engine.getTotalLatency() == 256);

engine.render(512);
// Verify output alignment...
```
