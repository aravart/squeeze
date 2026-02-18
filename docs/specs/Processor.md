# Processor Specification

## Responsibilities

- Define the abstract interface for anything that processes audio in the mixer
- Process audio **in-place** on a single buffer — no separate input/output buffers
- Optionally accept MIDI input alongside the audio buffer
- Expose parameters by name with rich metadata (`ParamDescriptor`)
- Report processing latency in samples
- Support bypass (audio passes through unchanged; Chain enforces this)
- Provide a name for identification

## Overview

Processor is the base abstraction in the mixer-centric architecture. It replaces v1's Node. The key differences:

- **In-place processing**: `process(AudioBuffer&)` — the same buffer is input and output. No port declarations, no channel routing, no fan-in summation at the Processor level.
- **No ports**: no `getInputPorts()`, no `getOutputPorts()`, no PortDescriptor. Audio routing is handled by Chain (sequential in-place), Source (generator → chain), and Bus (sum → chain).
- **MIDI variant**: `process(AudioBuffer&, const MidiBuffer&)` for processors that respond to MIDI (synth plugins, samplers). The default implementation ignores MIDI and delegates to `process(AudioBuffer&)`.
- **Parameters**: named string-based parameters survive from Node — `getParameter(name)`, `setParameter(name, value)`.
- **Latency**: `getLatencySamples()` for PDC.

Concrete subclasses:
- **PluginProcessor** — wraps a VST3/AU plugin
- **GainProcessor** — volume + pan
- **MeterProcessor** — peak/RMS measurement (read-only tap, passes audio through)
- **RecordingProcessor** — writes audio to disk or memory buffer (passes audio through)

## Interface

### C++ (`squeeze::Processor`)

```cpp
namespace squeeze {

struct ParamDescriptor {
    std::string name;       // unique identifier — the only key used in the public API
    float defaultValue;
    float minValue;         // typically 0.0
    float maxValue;         // typically 1.0
    int numSteps;           // 0 = continuous, >0 = stepped
    bool automatable;
    bool boolean;
    std::string label;      // unit: "dB", "Hz", "%", ""
    std::string group;      // "" = ungrouped
};

class Processor {
public:
    explicit Processor(const std::string& name);
    virtual ~Processor() = default;

    // Non-copyable, non-movable
    Processor(const Processor&) = delete;
    Processor& operator=(const Processor&) = delete;

    // --- Lifecycle (control thread) ---
    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void reset() {}
    virtual void release() {}

    // --- Processing (audio thread, RT-safe) ---
    virtual void process(juce::AudioBuffer<float>& buffer) = 0;
    virtual void process(juce::AudioBuffer<float>& buffer,
                         const juce::MidiBuffer& midi) {
        process(buffer);  // default: ignore MIDI
    }

    // --- Parameters (string-based) ---
    virtual int getParameterCount() const { return 0; }
    virtual ParamDescriptor getParameterDescriptor(int index) const { return {}; }
    virtual std::vector<ParamDescriptor> getParameterDescriptors() const { return {}; }
    virtual float getParameter(const std::string& name) const { return 0.0f; }
    virtual void setParameter(const std::string& name, float value) {}
    virtual std::string getParameterText(const std::string& name) const { return ""; }

    // --- Identity ---
    const std::string& getName() const { return name_; }

    // --- Bypass (control thread write, audio thread read) ---
    void setBypassed(bool b);
    bool isBypassed() const;

    // --- Latency ---
    virtual int getLatencySamples() const { return 0; }

    // --- Handle (set by Engine when processor is added) ---
    int getHandle() const { return handle_; }
    void setHandle(int h) { handle_ = h; }

    // --- Audio-thread bypass tracking (written by Engine, not by Processor) ---
    bool wasBypassed_ = false;  // audio-thread only, not atomic

private:
    std::string name_;
    int handle_ = -1;
    std::atomic<bool> bypassed_{false};
};

} // namespace squeeze
```

## Lifecycle

```
construct → prepare() → [process()...] → release() → destroy
                ↑                             |
                └─────────────────────────────┘
                (may cycle on device restart)
```

- `prepare(sampleRate, blockSize)` — called on the control thread before the first `process()`. Store sample rate and block size, allocate any pre-sized state. Must handle being called multiple times (e.g., device restart).
- `reset()` — clears internal processing state (filter histories, delay lines, gain reduction, etc.) without the full `prepare()`/`release()` cycle. Called by the Engine on the audio thread when un-bypassing a processor so that stale state doesn't produce artifacts. **Must be RT-safe** (same constraint as `process()`). Parameters and configuration are preserved — only transient processing state is cleared. Default: no-op.
- `process(buffer)` — called on the audio thread. **Must be RT-safe.** Processes audio in-place: reads from the buffer, writes results back to the same buffer. Called repeatedly between `prepare()` and `release()`.
- `process(buffer, midi)` — variant for MIDI-receiving processors. Default implementation ignores MIDI and delegates to `process(buffer)`.
- `release()` — called on the control thread before destruction or device shutdown. Free resources. Default: no-op.

Calling `process()` before `prepare()` is undefined behavior (caller's responsibility).

## Parameter System

Parameters are identified by **name only**. No index appears in the public API (Processor, C ABI, or Python). This is a deliberate design: the C ABI is the primary interface, and FFI callers work with names, not opaque integers.

### ParamDescriptor

| Field | Description |
|-------|-------------|
| `name` | Unique string identifier — the only key |
| `defaultValue` | Default value |
| `minValue` | Minimum value (typically 0.0) |
| `maxValue` | Maximum value (typically 1.0) |
| `numSteps` | 0 = continuous, >0 = discrete steps |
| `automatable` | Whether EventScheduler can target this parameter |
| `boolean` | Two-state parameter (0.0 or 1.0) |
| `label` | Unit string for display: `"dB"`, `"Hz"`, `"%"`, `""` |
| `group` | Grouping label for UI: `"filter"`, `"envelope"`, `""` |

### String-based virtuals

- `getParameterDescriptors()` — returns all parameter metadata. Default: empty (no parameters).
- `getParameter(name)` — returns current value. Default: 0.0f.
- `setParameter(name, value)` — sets value. Default: no-op.
- `getParameterText(name)` — human-readable display string (e.g. `"-6.0 dB"`). Default: `""`.

### Storage is private

Subclasses store parameters however they choose. Common patterns:

| Processor type | Storage |
|----------------|---------|
| GainProcessor | Single `float` member, `if (name == "gain")` |
| PluginProcessor | Delegates to `juce::AudioProcessor` via name→JUCE-index map |

### RT-safe parameter dispatch (Engine concern)

Sample-accurate parameter automation (EventScheduler) requires RT-safe dispatch on the audio thread. Since `std::string` operations are not RT-safe, the **Engine** pre-resolves parameter names to internal tokens at scheduling time (control thread). The audio thread dispatches using these tokens. This is an Engine/EventScheduler implementation detail — Processor's public interface stays string-based.

## Processor Identity

Processors are identified by **opaque handles** assigned by the Engine when the processor is added to a Source chain or Bus chain. Handles are monotonically increasing integers, never reused. The handle is stored on the Processor via `setHandle()`/`getHandle()`.

The name (set at construction) is for display/debugging. Multiple processors may share a name.

## Bypass

A processor can be bypassed, causing audio to pass through unchanged. Bypass is a flag on the Processor base class — the **Engine** reads the flag during snapshot iteration and skips bypassed processors entirely (no virtual call overhead).

- `setBypassed(bool)` — control thread. Atomic store.
- `isBypassed()` — any thread. Atomic load.
- Default: not bypassed (`false`).
- **Bypass transition tracking** uses `wasBypassed_` (a non-atomic `bool` on the Processor, written only by the audio thread). Each block, the Engine compares `isBypassed()` against `wasBypassed_`. On a bypassed→active transition, the Engine calls `reset()` before the first `process()` call to clear stale internal state. Because `wasBypassed_` lives on the Processor object itself (not in the snapshot), it naturally survives snapshot swaps with no transfer logic.
- **Latency still counts when bypassed.** `getLatencySamples()` returns the same value regardless of bypass state. This avoids audible glitches from PDC recalculation when toggling bypass.
- Bypass is not mute. A bypassed processor passes audio through; a muted source/bus produces silence.
- Bypass applies to insert chain processors. It does not apply to Source generators — use `Source.muted` to silence a source.

## Invariants

- `process()` and `reset()` are RT-safe: no allocation, no blocking, no unbounded work
- `prepare()` is called before the first `process()` call
- `release()` is called before destruction
- Processor does not own or allocate its processing buffers — it processes in-place on a buffer provided by Chain/Source/Bus
- Parameter names are unique within a processor
- `getLatencySamples()` returns >= 0 (never negative)
- The handle is -1 until the Engine assigns one
- Bypass defaults to false
- `getLatencySamples()` is unaffected by bypass state

## Error Conditions

- `getParameter()` with unknown name: returns 0.0f
- `setParameter()` with unknown name: no-op
- `getParameterText()` with unknown name: returns `""`

## Does NOT Handle

- Buffer allocation (Engine/Chain provides buffers)
- Audio routing (Source and Bus handle routing)
- Sequential chaining (Chain manages ordering; Engine iterates snapshot and enforces bypass)
- Thread safety of control-thread mutations (Engine's controlMutex_)
- Deferred deletion (Engine manages lifecycle)
- RT-safe parameter dispatch optimization (Engine/EventScheduler)
- MIDI routing (MidiRouter/Engine distributes MIDI to Sources)

## Dependencies

- JUCE (`juce::AudioBuffer<float>`, `juce::MidiBuffer`)

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| Constructor | Control | Sets name |
| `prepare()` | Control | Called with controlMutex_ held |
| `reset()` | Audio | Called by Engine on un-bypass; must be RT-safe |
| `release()` | Control | Called with controlMutex_ held |
| `process()` | Audio | Must be RT-safe |
| `getParameter()` | Control | Audio-thread param reads happen inside `process()` via internal state |
| `setParameter()` | Control | Called with controlMutex_ held |
| `getParameterDescriptors()` | Control | Metadata query |
| `setBypassed()` | Control | Atomic store |
| `isBypassed()` | Any | Atomic load |
| `getName()` | Any | Immutable after construction |
| `getLatencySamples()` | Control | Called during snapshot build |

## C ABI

Processor creation is type-specific (`sq_source_append`, `sq_bus_append`, etc. load plugins; built-in processors have their own creators). Parameter operations use the processor handle:

```c
// Parameters — string-based, by processor handle
float sq_get_param(SqEngine engine, SqProc proc, const char* name);
void  sq_set_param(SqEngine engine, SqProc proc, const char* name, float value);
char* sq_param_text(SqEngine engine, SqProc proc, const char* name);
int   sq_param_count(SqEngine engine, SqProc proc);
SqParamDescriptorList sq_param_descriptors(SqEngine engine, SqProc proc);

// Bypass
void sq_set_bypassed(SqEngine engine, SqProc proc, bool bypassed);
bool sq_is_bypassed(SqEngine engine, SqProc proc);
```

## Example Usage

### In-place effect (GainProcessor)

```cpp
class GainProcessor : public Processor {
    float gain_ = 1.0f;
public:
    GainProcessor() : Processor("Gain") {}

    void prepare(double, int) override {}

    void process(juce::AudioBuffer<float>& buffer) override {
        buffer.applyGain(gain_);
    }

    int getParameterCount() const override { return 1; }
    std::vector<ParamDescriptor> getParameterDescriptors() const override {
        return {{"gain", 1.0f, 0.0f, 1.0f, 0, true, false, "", ""}};
    }
    float getParameter(const std::string& name) const override {
        if (name == "gain") return gain_;
        return 0.0f;
    }
    void setParameter(const std::string& name, float value) override {
        if (name == "gain") gain_ = value;
    }
};
```

### MIDI-receiving processor (instrument)

```cpp
class TestSynthProcessor : public Processor {
public:
    TestSynthProcessor() : Processor("TestSynth") {}

    void prepare(double sampleRate, int blockSize) override {
        sampleRate_ = sampleRate;
    }

    void process(juce::AudioBuffer<float>& buffer) override {
        // No MIDI — generate silence
        buffer.clear();
    }

    void process(juce::AudioBuffer<float>& buffer,
                 const juce::MidiBuffer& midi) override {
        buffer.clear();
        // Generate audio based on MIDI events
        for (const auto metadata : midi) {
            auto msg = metadata.getMessage();
            if (msg.isNoteOn()) {
                // ... synthesize
            }
        }
    }

private:
    double sampleRate_ = 44100.0;
};
```
