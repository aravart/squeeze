# RecorderNode Specification

## Responsibilities

- Record audio from its input into a pre-allocated Buffer
- Pass audio through from input to output (unmodified, regardless of recording state)
- Track write position via Buffer's atomic `writePosition` for external queries
- Support one-shot (stop at buffer end) and loop (wrap to start) recording modes
- Reset write position to 0 on each new record-arm transition

## Interface

### Construction

```cpp
RecorderNode();
```

No constructor arguments. Buffer is assigned separately via `setBuffer()`.

### Node Interface

```cpp
void prepare(double sampleRate, int blockSize) override;
void process(ProcessContext& context) override;
void release() override;

std::vector<PortDescriptor> getInputPorts() const override;
std::vector<PortDescriptor> getOutputPorts() const override;
```

### Ports

| Name    | Direction | Type  | Channels |
|---------|-----------|-------|----------|
| `in`    | input     | audio | 2        |
| `out`   | output    | audio | 2        |

### Buffer Binding (control thread)

```cpp
void setBuffer(Buffer* buffer);
Buffer* getBuffer() const;
```

- `setBuffer()` assigns the target buffer for recording. Pass `nullptr` to unbind.
- Non-const pointer: RecorderNode writes into the buffer's audio data.
- Does NOT reset the buffer or its writePosition — caller manages buffer state.
- Engine is responsible for buffer lifecycle and nulling references on removal.

### Parameters

| Index | Name         | Type       | Range     | Default | Mapping              | Label | Modulatable |
|-------|--------------|------------|-----------|---------|----------------------|-------|-------------|
| 0     | `rec_enabled`| boolean    | 0.0 / 1.0| 0.0     | > 0.5 = on           |       | no          |
| 1     | `mode`       | discrete   | 0.0 / 1.0| 0.0     | 0 = one_shot, 1 = loop|      | no          |
| 2     | `input_gain` | continuous | 0.0–1.0  | 0.5     | linear: gain = value * 2.0 | x | yes         |

Total: 3 parameters. Normalized 0.0–1.0 at the Node boundary.

`input_gain` is modulatable: when a modulation source is routed to it, the gain varies per-sample via `readParam()` (see Modulation spec). `rec_enabled` and `mode` are not modulatable (boolean/discrete).

**Parameter text:**
- `rec_enabled`: "off" / "on"
- `mode`: "one_shot" / "loop"
- `input_gain`: formatted as gain multiplier, e.g. "1.00x" (at default 0.5)

## Process Behavior

`process()` runs on the audio thread and performs two independent operations:

### 1. Pass-through (always)

Copy `inputAudio` to `outputAudio` for all samples, unmodified. This happens regardless of recording state, buffer assignment, or parameter values. The recorder is transparent in the signal chain.

### 2. Recording (conditional)

Recording occurs when ALL of these are true:
- `rec_enabled` > 0.5
- A buffer is assigned (non-null)
- In one-shot mode: internal write position < buffer length

**On 0→1 transition of `rec_enabled`:**
- Reset internal write position to 0
- Store 0 to `buffer->writePosition` (relaxed ordering)

**Per sample while recording:**
1. Read `input_gain` via `readParam(PARAM_INPUT_GAIN, sampleIndex)` — returns per-sample modulated value if a mod source is connected, or the scalar base value if not
2. Map normalized gain to physical: `gain = readParam(...) * 2.0`
3. Apply gain to input sample and write to buffer at current write position (see Channel Mapping)
4. Advance write position by 1
5. If write position reaches buffer length:
   - **one_shot**: stop recording for the remainder of this block
   - **loop**: wrap write position to 0, continue

**On 1→0 transition of `rec_enabled`:**
- Stop recording (no write position reset — position reflects where recording stopped)

**After each `process()` call (if recording occurred):**
- Store final write position to `buffer->writePosition` (relaxed ordering)

### Channel Mapping

The number of channels written depends on the buffer, not the input:

- **Stereo input → stereo buffer**: write each channel directly
- **Stereo input → mono buffer**: write average of left and right: `(L + R) * 0.5`
- **Mono input → stereo buffer**: duplicate input to both buffer channels
- **Mono input → mono buffer**: write directly

In practice, input is always stereo (2-channel port). Buffer may be mono or stereo.

## Invariants

1. Output always equals input — recording never affects the signal path
2. A 0→1 transition of `rec_enabled` always resets write position to 0
3. In one-shot mode, write position never exceeds buffer length
4. In loop mode, write position wraps modulo buffer length
5. `buffer->writePosition` reflects the internal write position after each `process()` call
6. No audio is written when buffer is null, regardless of `rec_enabled`
7. No allocation, no blocking, no I/O in `process()`
8. Parameter values are clamped to 0.0–1.0

## Error Conditions

| Condition | Behavior |
|-----------|----------|
| No buffer assigned, rec_enabled = 1 | No recording, pass-through continues |
| Buffer length is 0 | No recording (one-shot: immediately "full"; loop: no-op) |
| rec_enabled set while already enabled | No effect (no transition detected) |
| Buffer removed by Engine during recording | Engine calls `setBuffer(nullptr)`, next `process()` stops recording |

No error codes or exceptions — all error conditions result in graceful no-ops.

## Does NOT Handle

- **Disk recording**: writing to files is a separate component (background thread + FIFO)
- **Overdub / mix mode**: mixing new audio with existing buffer content (Phase 2)
- **MIDI-triggered start/stop**: use Lua scripting to bridge MIDI → parameter changes
- **Quantized start/stop**: transport-level concern, not node-level
- **Sample rate conversion**: buffer should be created at the engine's sample rate
- **Punch-in/out**: recording always starts from position 0 (Phase 2: arbitrary start position)
- **Buffer allocation or resizing**: buffer is pre-allocated by Engine, managed externally

## Dependencies

- `Node` (base class — includes modulation buffer infrastructure from Modulation spec)
- `Buffer` (write target — uses `getWritePointer()`, `writePosition`, `getLengthInSamples()`, `getNumChannels()`)
- No other components. Engine integration and modulation routing are external to this class.

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| Constructor | Control | |
| `prepare()` | Control | Called before first `process()` |
| `process()` | Audio | RT-safe, writes to buffer data and `writePosition` atomic |
| `release()` | Control | Called after last `process()` |
| `setBuffer()` | Control | Must not be called concurrently with `process()` — Engine serializes via graph update |
| `getBuffer()` | Control | |
| `setParameter()` | Control | Writes to aligned scalar fields read by audio thread |
| `getParameter()` | Any | Reads normalized parameter array |
| Port queries | Any | Immutable after construction |

`setBuffer()` safety: Engine calls `setBuffer()` on the control thread. The new pointer becomes visible to the audio thread on the next `process()` call. Since Engine serializes control operations and graph updates ensure a memory fence, no additional synchronization is needed (same pattern as `SamplerNode::setBuffer()`).

## Example Usage

### Lua

```lua
-- Create a 4-second stereo buffer at engine sample rate
local buf = sq.create_buffer(2, 44100 * 4, 44100, "rec1")

-- Create recorder node
local rec = sq.add_recorder("my_recorder")

-- Assign buffer
sq.set_recorder_buffer(rec.id, buf)

-- Connect a synth's output to the recorder's input
sq.connect(synth.id, "out", rec.id, "in")

-- Start recording
rec:set_param("rec_enabled", 1.0)

-- ... time passes ...

-- Stop recording
rec:set_param("rec_enabled", 0.0)

-- Check how many samples were recorded
print(sq.buffer_info(buf).write_position)

-- Assign the buffer to a sampler for playback
sq.set_sampler_buffer(sampler.id, buf)
```

### Lua (with modulated input gain)

```lua
-- Record with an LFO modulating the input gain (tremolo-style recording)
local rec = sq.add_recorder("my_recorder")
local lfo = sq.add_lfo("rec_trem", { shape = "sine", rate = 2.0 })
sq.set_recorder_buffer(rec.id, buf)
sq.connect(synth.id, "out", rec.id, "in")

-- LFO modulates input_gain with depth 0.3
sq.mod_route(lfo.id, "out", rec.id, "input_gain", 0.3)

rec:set_param("rec_enabled", 1.0)
```

### C++ (test)

```cpp
RecorderNode recorder;
recorder.prepare(44100.0, 512);
recorder.prepareModulation(512);

auto buffer = Buffer::createEmpty(2, 2048, 44100.0, "test");
recorder.setBuffer(buffer.get());

// Arm recording
recorder.setParameter(0, 1.0f);  // rec_enabled

// Process a block (no modulation — readParam returns scalar base value)
Node::ProcessContext ctx{inputAudio, outputAudio, midiIn, midiOut, 512};
recorder.process(ctx);

// Verify: output == input (pass-through)
// Verify: buffer contains recorded audio at position 0..511
// Verify: buffer->writePosition == 512
```

### C++ (test with modulated gain)

```cpp
RecorderNode recorder;
recorder.prepare(44100.0, 512);
recorder.prepareModulation(512);

auto buffer = Buffer::createEmpty(1, 512, 44100.0, "test");
recorder.setBuffer(buffer.get());
recorder.setParameter(0, 1.0f);  // rec_enabled
recorder.setParameter(2, 0.5f);  // input_gain base = unity

// Simulate render loop filling mod buffer with an LFO signal
float* modBuf = recorder.getModBuffer(2);  // input_gain
recorder.setModActive(2, true);
for (int s = 0; s < 512; ++s)
    modBuf[s] = 0.5f + 0.3f * std::sin(2.0f * M_PI * s / 512.0f);  // base + LFO

Node::ProcessContext ctx{inputAudio, outputAudio, midiIn, midiOut, 512};
recorder.process(ctx);

// Verify: buffer contains input * (modBuf[s] * 2.0) per sample
// Verify: gain varies sinusoidally across the block
```

## Engine Integration (informational — not part of RecorderNode itself)

Following the SamplerNode pattern:

```cpp
// Engine convenience methods
int addRecorder(const std::string& name, std::string& errorMessage);
bool setRecorderBuffer(int nodeId, int bufferId);

// removeNode: calls setBuffer(nullptr) before deferred deletion
// removeBuffer: nulls RecorderNode references before deferred buffer deletion
```

## Phase 2 Candidates

These are explicitly out of scope but noted for future design:

- **Overdub mode**: `pre_level` parameter controls how much existing content is retained when overwriting (0.0 = replace, 1.0 = full overdub)
- **MIDI trigger**: note-on starts recording, note-off stops (sample-accurate via sub-block splitting)
- **Arbitrary start position**: parameter or API to set recording start offset within buffer
- **Threshold trigger**: start recording when input exceeds a level threshold
- **Auto-stop callback**: notify Lua when one-shot recording completes
