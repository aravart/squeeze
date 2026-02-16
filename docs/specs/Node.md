# Node Specification

## Responsibilities
- Define the abstract interface for anything that processes audio or MIDI in the graph
- Declare input and output ports (via `PortDescriptor`)
- Process audio and MIDI data provided by Engine through `ProcessContext`
- Expose parameters by name with rich metadata (`ParameterDescriptor`)
- Support lifecycle management: prepare, process, release

## Interface

```cpp
namespace squeeze {

struct ProcessContext {
    juce::AudioBuffer<float>& inputAudio;
    juce::AudioBuffer<float>& outputAudio;
    juce::MidiBuffer& inputMidi;
    juce::MidiBuffer& outputMidi;
    int numSamples;  // may be < blockSize (sub-block parameter splitting)
};

struct ParameterDescriptor {
    std::string name;       // unique identifier — the only key used in the public API
    float defaultValue;
    int numSteps;           // 0 = continuous, >0 = stepped
    bool automatable;
    bool boolean;
    std::string label;      // unit: "dB", "Hz", "%", ""
    std::string group;      // "" = ungrouped
};

class Node {
public:
    virtual ~Node() = default;

    // --- Lifecycle (control thread) ---
    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void release() = 0;

    // --- Processing (audio thread, RT-safe) ---
    virtual void process(ProcessContext& context) = 0;

    // --- Port declaration ---
    virtual std::vector<PortDescriptor> getInputPorts() const = 0;
    virtual std::vector<PortDescriptor> getOutputPorts() const = 0;

    // --- Parameters (string-based) ---
    virtual std::vector<ParameterDescriptor> getParameterDescriptors() const { return {}; }
    virtual float getParameter(const std::string& name) const { return 0.0f; }
    virtual void setParameter(const std::string& name, float value) {}
    virtual std::string getParameterText(const std::string& name) const { return ""; }
};

} // namespace squeeze
```

## ProcessContext

Engine provides pre-allocated buffers to each node via `ProcessContext`. A node receives one audio input buffer, one audio output buffer, one MIDI input buffer, and one MIDI output buffer.

- `inputAudio` — read-only source; for effect nodes, contains upstream audio. For instrument nodes (no audio input port), empty.
- `outputAudio` — write destination. Node must fill `numSamples` of output.
- `inputMidi` — MIDI events for this block. Multiple MIDI sources are merged by Engine before delivery.
- `outputMidi` — MIDI events the node emits.
- `numSamples` — number of samples to process. **May be less than blockSize** due to sub-block parameter splitting. Nodes must not assume `numSamples == blockSize`.

For nodes with multiple audio input ports (e.g. sidechain), the Engine maps ports to channel ranges within the single `inputAudio` buffer (JUCE convention). The first port's channels come first, then the second port's, etc. Same for `outputAudio`. Nodes that need to distinguish ports use the channel offsets derived from their port declarations.

When audio fan-in is active (multiple connections to one input port), the Engine sums all sources into the relevant channel range before calling `process()`.

## Lifecycle

```
construct → prepare() → [process()...] → release() → destroy
                ↑                             |
                └─────────────────────────────┘
                (may cycle on device restart)
```

- `prepare(sampleRate, blockSize)` — called on the control thread before the first `process()`. Store sample rate and block size, allocate any pre-sized state. Must handle being called multiple times (e.g., device restart).
- `process(context)` — called on the audio thread. **Must be RT-safe.** Called repeatedly between `prepare()` and `release()`.
- `release()` — called on the control thread before destruction or device shutdown. Free resources. May be a no-op.

Calling `process()` before `prepare()` is undefined behavior (caller's responsibility).

## Port Declaration

Nodes declare ports via `getInputPorts()` and `getOutputPorts()`. Ports are returned as `std::vector<PortDescriptor>` on each call.

- **Most nodes** declare fixed ports at construction — these methods always return the same values.
- **GroupNode** supports dynamic ports via `exportPort()` / `unexportPort()`, so its return values can change over time (control thread only).
- All declared ports must pass `isValid()`.
- A node must not have two ports with the same direction and name.

## Parameter System

Parameters are identified by **name only**. No index appears in the public API (Node, C ABI, or Python). This is a deliberate v2 design: the C ABI is the primary interface, and FFI callers work with names, not opaque integers.

### ParameterDescriptor

| Field | Description |
|-------|-------------|
| `name` | Unique string identifier — the only key |
| `defaultValue` | Normalized default (0.0–1.0) |
| `numSteps` | 0 = continuous, >0 = discrete steps |
| `automatable` | Whether EventQueue can target this parameter |
| `boolean` | Two-state parameter (0.0 or 1.0) |
| `label` | Unit string for display: `"dB"`, `"Hz"`, `"%"`, `""` |
| `group` | Grouping label for UI: `"filter"`, `"envelope"`, `""` |

### String-based virtuals

- `getParameterDescriptors()` — returns all parameter metadata. Default: empty (no parameters).
- `getParameter(name)` — returns normalized 0.0–1.0 value. Default: 0.0f.
- `setParameter(name, value)` — sets normalized value. Default: no-op.
- `getParameterText(name)` — human-readable display string (e.g. `"-6.0 dB"`). Default: `""`.

### Storage is private

Subclasses store parameters however they choose. Common patterns:

| Node type | Storage |
|-----------|---------|
| Simple (GainNode) | Single `float` member, `if (name == "gain")` |
| SamplerNode | `std::unordered_map<std::string, float>` or flat array with name→index map |
| PluginNode | Delegates to `juce::AudioProcessor` via name→JUCE-index map |

### RT-safe parameter dispatch (Engine concern)

Sample-accurate parameter automation (EventQueue) requires RT-safe dispatch on the audio thread. Since `std::string` operations are not RT-safe, the **Engine** pre-resolves parameter names to internal tokens at scheduling time (control thread). The audio thread dispatches using these tokens. This is an Engine/EventQueue implementation detail — Node's public interface stays string-based.

## Node Identity

Node does **not** store its own ID. IDs are assigned by `Graph::addNode()` as monotonically increasing integers (never reused). Engine is the true owner of node objects (`std::unique_ptr<Node>`); Graph holds non-owning raw pointers indexed by ID.

## Invariants
- `process()` is RT-safe: no allocation, no blocking, no unbounded work
- `prepare()` is called before the first `process()` call
- `release()` is called before destruction
- All declared ports pass `isValid()`
- A node has no two ports with the same `(direction, name)` pair
- Node does not own or allocate its processing buffers
- Parameter values are normalized 0.0–1.0
- Parameter names are unique within a node

## Error Conditions
- `getParameter()` with unknown name: returns 0.0f
- `setParameter()` with unknown name: no-op
- `getParameterText()` with unknown name: returns `""`

## Does NOT Handle
- Buffer allocation (Engine)
- Position in execution order (Graph computes via topological sort)
- Connection logic or cycle detection (Graph)
- Thread safety of control-thread mutations (Engine's controlMutex_)
- Deferred deletion (Engine manages via pendingDeletions_)
- RT-safe parameter dispatch optimization (Engine/EventQueue)

## Dependencies
- Port (`PortDescriptor`, `PortDirection`, `SignalType`)
- JUCE (`juce::AudioBuffer<float>`, `juce::MidiBuffer`)

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| `prepare()` | Control | Called with controlMutex_ held |
| `release()` | Control | Called with controlMutex_ held |
| `process()` | Audio | Must be RT-safe |
| `getInputPorts()` / `getOutputPorts()` | Control | Mutable for GroupNode; audio thread reads only via snapshot |
| `getParameter()` | Control | Audio-thread param reads happen inside `process()` via node's internal state |
| `setParameter()` | Control | Called with controlMutex_ held |
| `getParameterDescriptors()` | Control | Metadata query |

## C ABI

Node creation is type-specific (`sq_add_plugin`, `sq_add_sampler`, etc.). Generic node operations are string-based — no parameter index in the public API:

```c
// Lifecycle
bool sq_remove_node(SqEngine engine, int node_id);

// Query
char* sq_node_name(SqEngine engine, int node_id);      // caller frees
SqPortList sq_get_ports(SqEngine engine, int node_id);

// Parameters — string-based only
SqParamDescriptorList sq_param_descriptors(SqEngine engine, int node_id);
float sq_get_param(SqEngine engine, int node_id, const char* name);
bool  sq_set_param(SqEngine engine, int node_id, const char* name, float value);
char* sq_param_text(SqEngine engine, int node_id, const char* name);
```

## Example Usage

### Minimal node (no parameters)

```cpp
class PassthroughNode : public Node {
public:
    void prepare(double, int) override {}
    void release() override {}

    void process(ProcessContext& ctx) override {
        for (int ch = 0; ch < ctx.outputAudio.getNumChannels(); ++ch)
            ctx.outputAudio.copyFrom(ch, 0, ctx.inputAudio, ch, 0, ctx.numSamples);
        ctx.outputMidi = ctx.inputMidi;
    }

    std::vector<PortDescriptor> getInputPorts() const override {
        return {{"in", PortDirection::input, SignalType::audio, 2},
                {"midi_in", PortDirection::input, SignalType::midi, 1}};
    }
    std::vector<PortDescriptor> getOutputPorts() const override {
        return {{"out", PortDirection::output, SignalType::audio, 2},
                {"midi_out", PortDirection::output, SignalType::midi, 1}};
    }
};
```

### Node with parameters

```cpp
class GainNode : public Node {
    float gain_ = 1.0f;
public:
    void prepare(double, int) override {}
    void release() override {}

    void process(ProcessContext& ctx) override {
        for (int ch = 0; ch < ctx.outputAudio.getNumChannels(); ++ch) {
            ctx.outputAudio.copyFrom(ch, 0, ctx.inputAudio, ch, 0, ctx.numSamples);
            ctx.outputAudio.applyGain(ch, 0, ctx.numSamples, gain_);
        }
    }

    std::vector<PortDescriptor> getInputPorts() const override {
        return {{"in", PortDirection::input, SignalType::audio, 2}};
    }
    std::vector<PortDescriptor> getOutputPorts() const override {
        return {{"out", PortDirection::output, SignalType::audio, 2}};
    }

    std::vector<ParameterDescriptor> getParameterDescriptors() const override {
        return {{"gain", 1.0f, 0, true, false, "", ""}};
    }
    float getParameter(const std::string& name) const override {
        if (name == "gain") return gain_;
        return 0.0f;
    }
    void setParameter(const std::string& name, float value) override {
        if (name == "gain") gain_ = value;
    }
    std::string getParameterText(const std::string& name) const override {
        if (name == "gain") return std::to_string(gain_);
        return "";
    }
};
```
