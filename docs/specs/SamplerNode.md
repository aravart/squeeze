# SamplerNode Specification

## Overview

SamplerNode is a Node subclass that implements a sample-playback instrument. It receives MIDI note events, plays audio from a Buffer via a pool of SamplerVoice instances managed by a VoiceAllocator, and exposes all sampler parameters through the standard Node parameter interface. It is the user-facing component — SamplerVoice and VoiceAllocator are internal.

## Responsibilities

- Implement the Node interface (ports, parameters, process)
- Own a `SamplerParams` struct and a `VoiceAllocator`
- Assign a Buffer (by ID, resolved via Engine) for playback
- Parse MIDI note-on/note-off from `ProcessContext::inputMidi`
- Split processing into sub-blocks at MIDI event boundaries for sample-accurate triggering
- Map all sampler parameters to the Node parameter system (normalized 0.0–1.0, index-based)
- Provide `getParameterText()` with human-readable display values

## Interface

```cpp
class SamplerNode : public Node {
public:
    explicit SamplerNode(int maxVoices = 1);

    // Node interface
    void prepare(double sampleRate, int blockSize) override;
    void process(ProcessContext& context) override;
    void release() override;
    std::vector<PortDescriptor> getInputPorts() const override;
    std::vector<PortDescriptor> getOutputPorts() const override;

    // Parameters
    std::vector<ParameterDescriptor> getParameterDescriptors() const override;
    float getParameter(int index) const override;
    void setParameter(int index, float value) override;
    std::string getParameterText(int index) const override;
    int findParameterIndex(const std::string& name) const override;

    // Buffer assignment (control thread)
    void setBuffer(const Buffer* buffer);
    const Buffer* getBuffer() const;
};
```

## Ports

```cpp
std::vector<PortDescriptor> getInputPorts() const override {
    return {
        { "midi_in", PortDirection::input, SignalType::midi, 1 }
    };
}

std::vector<PortDescriptor> getOutputPorts() const override {
    return {
        { "out", PortDirection::output, SignalType::audio, 2 }
    };
}
```

- **midi_in**: receives MIDI note-on, note-off (and eventually CC for parameter modulation)
- **out**: stereo audio output (summed voices)
- No audio input in Phase 1 (audio input is for recording, Phase 3)

## Buffer Assignment

The buffer is assigned via `setBuffer(const Buffer* buffer)` on the control thread. SamplerNode stores the raw pointer. The caller (Engine / Lua binding) is responsible for ensuring the Buffer outlives the SamplerNode or is set to null before the Buffer is destroyed.

When the buffer pointer is null:
- `noteOn` events are ignored (no voice is triggered)
- Already-playing voices continue rendering from the old buffer until they go idle (the old buffer must not be freed while voices reference it — this is Engine's responsibility via deferred deletion)

Buffer assignment is not a parameter — it's a control-plane operation separate from the 0.0–1.0 parameter system. It is exposed to Lua as a dedicated method, not via `set_param`.

## Parameters

All parameters are normalized 0.0–1.0 at the Node interface boundary. SamplerNode maps to physical/natural units internally (stored in `SamplerParams`). The mapping functions are documented per-parameter.

### Parameter Table

| Index | Name | Default | Steps | Label | Group | Mapping |
|-------|------|---------|-------|-------|-------|---------|
| 0 | sample_start | 0.0 | 0 | | Playback | direct (0.0–1.0 of buffer) |
| 1 | sample_end | 1.0 | 0 | | Playback | direct (0.0–1.0 of buffer) |
| 2 | root_note | 0.472 | 128 | | Playback | round(value * 127) → 0–127, default 60 |
| 3 | loop_start | 0.0 | 0 | | Loop | direct (0.0–1.0 of buffer) |
| 4 | loop_end | 1.0 | 0 | | Loop | direct (0.0–1.0 of buffer) |
| 5 | loop_mode | 0.0 | 4 | | Loop | 0=off, 1=forward, 2=reverse, 3=pingPong |
| 6 | loop_crossfade | 0.0 | 0 | s | Loop | exponential: value^2 * 0.5 → 0–500ms |
| 7 | direction | 0.0 | 2 | | Playback | 0=forward, 1=reverse |
| 8 | pitch_coarse | 0.5 | 97 | st | Pitch | round(value * 96) - 48 → -48 to +48 semitones |
| 9 | pitch_fine | 0.5 | 0 | ct | Pitch | (value - 0.5) * 200 → -100 to +100 cents |
| 10 | volume | 0.8 | 0 | dB | Amp | value^2 → 0.0–1.0 gain (quadratic taper) |
| 11 | pan | 0.5 | 0 | | Amp | (value - 0.5) * 2 → -1.0 to +1.0 |
| 12 | vel_sensitivity | 1.0 | 0 | | Amp | direct (0.0–1.0) |
| 13 | amp_attack | 0.0 | 0 | s | Amp Env | exponential: 0.001 * (10000^value) → 1ms–10s |
| 14 | amp_hold | 0.0 | 0 | s | Amp Env | linear: value * 10.0 → 0–10s |
| 15 | amp_decay | 0.15 | 0 | s | Amp Env | exponential: 0.001 * (10000^value) → 1ms–10s |
| 16 | amp_sustain | 1.0 | 0 | | Amp Env | direct (0.0–1.0) |
| 17 | amp_release | 0.05 | 0 | s | Amp Env | exponential: 0.001 * (10000^value) → 1ms–10s |
| 18 | amp_attack_curve | 0.0 | 3 | | Amp Env | 0=linear, 1=exponential, 2=logarithmic |
| 19 | amp_decay_curve | 0.333 | 3 | | Amp Env | 0=linear, 1=exponential, 2=logarithmic |
| 20 | amp_release_curve | 0.333 | 3 | | Amp Env | 0=linear, 1=exponential, 2=logarithmic |
| 21 | filter_type | 0.0 | 5 | | Filter | 0=off, 1=LP, 2=HP, 3=BP, 4=notch |
| 22 | filter_cutoff | 1.0 | 0 | Hz | Filter | exponential: 20 * (1000^value) → 20Hz–20kHz |
| 23 | filter_resonance | 0.0 | 0 | | Filter | direct (0.0–1.0, mapped to Q internally by JUCE) |
| 24 | filter_env_amount | 0.5 | 0 | | Filter | (value - 0.5) * 2 → -1.0 to +1.0 |
| 25 | filter_attack | 0.0 | 0 | s | Filter Env | exponential: 0.001 * (10000^value) → 1ms–10s |
| 26 | filter_decay | 0.15 | 0 | s | Filter Env | exponential: 0.001 * (10000^value) → 1ms–10s |
| 27 | filter_sustain | 1.0 | 0 | | Filter Env | direct (0.0–1.0) |
| 28 | filter_release | 0.05 | 0 | s | Filter Env | exponential: 0.001 * (10000^value) → 1ms–10s |
| 29 | filter_attack_curve | 0.0 | 3 | | Filter Env | 0=linear, 1=exponential, 2=logarithmic |
| 30 | filter_decay_curve | 0.333 | 3 | | Filter Env | 0=linear, 1=exponential, 2=logarithmic |
| 31 | filter_release_curve | 0.333 | 3 | | Filter Env | 0=linear, 1=exponential, 2=logarithmic |

### Time mapping formula

Envelope times use the formula `0.001 * pow(10000, value)`:
- value 0.0 → 0.001s (1ms)
- value 0.25 → 0.01s (10ms)
- value 0.5 → 0.1s (100ms)
- value 0.75 → 1.0s
- value 1.0 → 10.0s

This gives a logarithmic feel that matches how musicians think about envelope times — more resolution at short times, less at long times.

### Frequency mapping formula

Filter cutoff uses `20 * pow(1000, value)`:
- value 0.0 → 20 Hz
- value 0.5 → ~632 Hz
- value 1.0 → 20,000 Hz

### Display text

`getParameterText(index)` returns human-readable strings:
- Time parameters: `"1.0 ms"`, `"250 ms"`, `"2.5 s"`
- Frequency: `"440 Hz"`, `"2.1 kHz"`
- Semitones: `"+7 st"`, `"-12 st"`, `"0 st"`
- Cents: `"+50 ct"`, `"-25 ct"`
- Volume: `"-6.0 dB"`, `"-inf dB"` (at 0.0)
- Pan: `"C"` (center), `"L50"`, `"R100"`
- Discrete: `"Off"`, `"Forward"`, `"LP"`, `"Exponential"`, etc.

## MIDI Handling

### Sub-block splitting

`process()` iterates through `inputMidi` and splits the block at each relevant MIDI event's sample offset. Between events, `VoiceAllocator::renderBlock()` is called to render audio.

```cpp
void SamplerNode::process(ProcessContext& ctx) {
    ctx.outputAudio.clear(0, ctx.numSamples);
    int currentSample = 0;

    for (const auto metadata : ctx.inputMidi) {
        const auto msg = metadata.getMessage();
        const int eventSample = metadata.samplePosition;

        // Render up to this event
        if (eventSample > currentSample) {
            allocator_.renderBlock(ctx.outputAudio, currentSample,
                                   eventSample - currentSample);
            currentSample = eventSample;
        }

        // Handle the event
        if (msg.isNoteOn()) {
            allocator_.noteOn(buffer_, msg.getNoteNumber(),
                              msg.getFloatVelocity() * 127.0f);
        } else if (msg.isNoteOff()) {
            allocator_.noteOff(msg.getNoteNumber());
        }
        // CC handling deferred to Phase 2+
    }

    // Render remaining samples
    if (currentSample < ctx.numSamples) {
        allocator_.renderBlock(ctx.outputAudio, currentSample,
                               ctx.numSamples - currentSample);
    }
}
```

### Supported MIDI messages (Phase 1)

- **Note On** (status 0x90, velocity > 0): triggers a voice
- **Note Off** (status 0x80, or Note On with velocity 0): releases a voice

### Deferred MIDI

- **CC**: parameter modulation via CC mapping (Phase 2+)
- **Pitch Bend**: could map to pitch_fine (Phase 2+)
- **All Notes Off** (CC 123): `allocator_.allNotesOff()`

## Engine Integration

### Adding a SamplerNode

```cpp
// In Engine (new method or via existing addNode)
int nodeId = engine.addNode(std::make_unique<SamplerNode>(1), "sampler");

// Assign a buffer
auto* node = dynamic_cast<SamplerNode*>(engine.getNode(nodeId));
Buffer* buf = engine.getBuffer(bufferId);
node->setBuffer(buf);
```

Engine's existing `addNode()` / `removeNode()` work unchanged. Buffer assignment is a separate step. Engine may provide a convenience method:

```cpp
bool Engine::setSamplerBuffer(int nodeId, int bufferId);
```

This resolves the buffer ID, downcasts the node, and calls `setBuffer()`. Returns false if the node isn't a SamplerNode or the buffer doesn't exist.

### Buffer lifecycle

When `Engine::removeBuffer(id)` is called:
1. Engine checks if any SamplerNode references this buffer
2. If so, it calls `node->setBuffer(nullptr)` on each
3. Currently playing voices finish their release naturally (they hold a pointer to the buffer's audio data, which is not freed until the Buffer object is destroyed)
4. Engine defers Buffer destruction via `pendingDeletions_` (same pattern as node removal) to ensure the audio thread finishes reading before the memory is freed

## Lua API

Registered in LuaBindings (or main.cpp app-layer):

```lua
-- Create a sampler node
local s, err = sq.add_sampler("my_sampler")
-- Returns a node object (same pattern as sq.add_plugin)

-- Assign a buffer
sq.set_sampler_buffer(s.id, buffer_id)

-- Parameters use the standard parameter API
sq.set_param(s.id, "filter_cutoff", 0.7)
sq.set_param(s.id, "amp_attack", 0.3)
sq.set_param(s.id, "loop_mode", 0.333)    -- forward (1/3 of 3 steps)
-- Or by index
sq.set_param_i(s.id, 22, 0.7)             -- filter_cutoff

-- Query
local info = sq.param_info(s.id)
local text = sq.param_text(s.id, "filter_cutoff")   -- "1.4 kHz"

-- Connect MIDI
sq.connect(midi_node.id, s.id)
```

### Convenience: Lua node object methods

Following the existing pattern from main.cpp bootstrap:

```lua
-- Methods on the sampler node object
s:set_buffer(buffer_id)
s:param("filter_cutoff")           -- get current value
s:set("filter_cutoff", 0.7)        -- set parameter
s:info()                            -- all parameter descriptors
```

### sq.add_sampler implementation

```lua
-- Registered in LuaBindings or main.cpp
sq.add_sampler = function(name, max_voices)
    max_voices = max_voices or 1
    local id, err = sq._add_sampler(name, max_voices)  -- C++ binding
    if not id then return nil, err end
    -- Return node object with methods (same metatable pattern as add_plugin)
    return setmetatable({ id = id, name = name }, SamplerNode_mt)
end
```

## Invariants

- `process()` is RT-safe: no allocation, no blocking
- `process()` clears `outputAudio` before rendering (voices add into it)
- MIDI events are processed in sample-offset order (JUCE MidiBuffer guarantees this)
- All parameters are normalized 0.0–1.0 at the Node interface
- Parameter indices are contiguous 0–31 and stable for the node's lifetime
- `setParameter()` clamps input to [0.0, 1.0] and maps to `SamplerParams` immediately
- `getParameter()` reverse-maps from `SamplerParams` to normalized
- Port configuration is fixed: one MIDI input, one stereo audio output
- `setBuffer(nullptr)` is safe and stops new note triggers
- Sub-block splitting ensures MIDI events are sample-accurate within the block

## Error Conditions

- `setBuffer()` with a buffer that is later removed: Engine must call `setBuffer(nullptr)` before freeing the buffer
- `setParameter()` with out-of-range index: no-op
- `getParameter()` with out-of-range index: returns 0.0f
- `getParameterText()` with out-of-range index: returns ""
- MIDI events with out-of-range note numbers (>127): ignored
- Process called before prepare: undefined behavior (caller's responsibility, same as all Nodes)

## Does NOT Handle

- Buffer loading / creation (Engine)
- Buffer memory management (Engine)
- Audio device I/O (Engine)
- Graph topology / connections (Graph)
- Timestretching (Phase 3 — separate component injected into voice)
- Slice management (Phase 2 — will extend this node)
- Input recording (Phase 3 — will add audio input port)
- MIDI CC → parameter mapping (Phase 2 — Lua layer or built-in mapping table)
- Disk streaming for large files (Phase 3)
- Parameter smoothing / ramping (Scheduler handles this generically — see Parameters spec)

## Dependencies

- `Node` (base class)
- `VoiceAllocator` (voice management)
- `SamplerVoice` (DSP, via VoiceAllocator)
- `SamplerParams` (shared parameter struct)
- `Buffer` (audio data source)
- `Port` (`PortDescriptor`)
- `juce::MidiBuffer` / `juce::MidiMessage` (MIDI parsing)
- `juce::AudioBuffer<float>` (audio output)
- `Logger` — `SQ_LOG()` for debug messages

## Thread Safety

- **Control thread**: constructor, `prepare()`, `release()`, `setParameter()`, `getParameter()`, `getParameterText()`, `getParameterDescriptors()`, `setBuffer()`, `getBuffer()`
- **Audio thread**: `process()` (which calls VoiceAllocator methods internally)
- `getInputPorts()`, `getOutputPorts()`: any thread (immutable)
- `SamplerParams` writes (from `setParameter` on control thread) and reads (from voice `render` on audio thread) are safe without synchronization for individual aligned scalar fields

## Example Usage

```cpp
// Create and configure
auto sampler = std::make_unique<SamplerNode>(1);
int nodeId = engine.addNode(std::move(sampler), "drums");

// Load a sample and assign it
int bufId = engine.loadBuffer("/path/to/break.wav", error);
engine.setSamplerBuffer(nodeId, bufId);

// Set up as a one-shot drum hit with filter
engine.setParameterByName(nodeId, "loop_mode", 0.0f);       // off
engine.setParameterByName(nodeId, "amp_attack", 0.0f);       // 1ms
engine.setParameterByName(nodeId, "amp_decay", 0.3f);        // ~50ms
engine.setParameterByName(nodeId, "amp_sustain", 0.0f);      // no sustain
engine.setParameterByName(nodeId, "filter_type", 0.2f);      // lowpass
engine.setParameterByName(nodeId, "filter_cutoff", 0.6f);    // ~500Hz
engine.setParameterByName(nodeId, "filter_env_amount", 0.85f); // positive mod

// Connect MIDI
engine.connect(midiNodeId, "midi_out", nodeId, "midi_in");

// Now MIDI notes trigger the sample through the filter
```
