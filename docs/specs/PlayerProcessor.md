# PlayerProcessor Specification

## Responsibilities

- Implement the Processor interface as a Source generator
- Play audio from an assigned Buffer with a single read position
- Provide transport-like controls (play, stop, seek, loop) via the parameter system
- Support variable-speed playback (varispeed — pitch follows speed)
- Handle sample rate mismatch between Buffer and Engine automatically
- Crossfade at loop boundaries, seeks, and play/stop transitions to avoid clicks

## Overview

PlayerProcessor is a buffer playback engine for continuous audio playback — loops, stems, backing tracks, and Octatrack-style flex machine scenarios. Unlike SamplerProcessor (a MIDI-triggered polyphonic instrument with per-voice envelopes and filters), PlayerProcessor is a single-playhead tape machine controlled entirely through parameters.

All controls are parameters, so they work with EventScheduler (beat-synced play/stop), Modulation (LFO on speed), and any future parameter lock / scene system with no new API surface.

## Interface

### C++ (`squeeze::PlayerProcessor`)

```cpp
namespace squeeze {

class PlayerProcessor : public Processor {
public:
    PlayerProcessor();
    ~PlayerProcessor() override;

    // Processor interface
    void prepare(double sampleRate, int blockSize) override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void release() override;
    void reset() override;

    // Parameters (string-based, inherited from Processor)
    int getParameterCount() const override;
    ParamDescriptor getParameterDescriptor(int index) const override;
    std::vector<ParamDescriptor> getParameterDescriptors() const override;
    float getParameter(const std::string& name) const override;
    void setParameter(const std::string& name, float value) override;
    std::string getParameterText(const std::string& name) const override;
    int getLatencySamples() const override;  // always 0

    // PlayHead (control thread, called by Engine)
    void setPlayHead(juce::AudioPlayHead* playHead) override;

    // Buffer assignment (control thread)
    void setBuffer(const Buffer* buffer);
    const Buffer* getBuffer() const;
};

} // namespace squeeze
```

PlayerProcessor does not override `process(AudioBuffer&, const MidiBuffer&)`. It ignores MIDI — the base class default delegates to `process(AudioBuffer&)`.

## Parameters

All controls are exposed through the standard parameter system. No special commands, no new API surface.

| Name | Min | Max | Default | Steps | Label | Group | Description |
|------|-----|-----|---------|-------|-------|-------|-------------|
| playing | 0.0 | 1.0 | 0.0 | 2 | | Playback | 0 = stopped, 1 = playing |
| position | 0.0 | 1.0 | 0.0 | 0 | | Playback | Normalized buffer position |
| speed | -4.0 | 4.0 | 1.0 | 0 | x | Playback | Playback speed (varispeed) |
| loop_mode | 0.0 | 2.0 | 0.0 | 3 | | Loop | 0 = off, 1 = forward, 2 = ping-pong |
| loop_start | 0.0 | 1.0 | 0.0 | 0 | | Loop | Loop region start (normalized) |
| loop_end | 0.0 | 1.0 | 1.0 | 0 | | Loop | Loop region end (normalized) |
| fade_ms | 0.0 | 50.0 | 5.0 | 0 | ms | Playback | Crossfade time for transitions |
| tempo_lock | 0.0 | 1.0 | 0.0 | 2 | | Playback | 0 = off, 1 = lock speed to engine tempo / buffer tempo |
| transpose | -24.0 | 24.0 | 0.0 | 0 | st | Playback | Semitone pitch shift (modifies speed) |

9 parameters. No volume or pan — the Source's gain and pan handle that.

### Parameter Behavior

**`playing`**: Setting to 1.0 starts playback from the current `position`. Setting to 0.0 stops. A fade of `fade_ms` duration is applied on both transitions to avoid clicks. When playback reaches the end of the buffer and `loop_mode` is off, `playing` automatically becomes 0.0.

**`position`**: Normalized to buffer length (0.0 = first sample, 1.0 = last sample). Writing seeks the playhead — if playing, a crossfade of `fade_ms` is applied from the old position to the new one. Reading returns the current playback position, which advances continuously during playback.

**`speed`**: Playback rate multiplier. 1.0 = original speed and pitch. 2.0 = double speed, one octave up. -1.0 = original speed, reversed. 0.0 = frozen (outputs silence, position doesn't advance). This is varispeed — pitch changes proportionally with speed.

**`loop_mode`**:
- Off (0): Play to end of buffer, then stop
- Forward (1): When position reaches `loop_end`, jump to `loop_start` (with crossfade)
- Ping-pong (2): When position reaches `loop_end`, reverse direction; when it reaches `loop_start`, reverse again

**`loop_start` / `loop_end`**: Define the loop region as normalized buffer positions. Only affect behavior when `loop_mode` is not off. If `loop_start >= loop_end`, the full buffer is used as the loop region.

**`fade_ms`**: Applied at loop boundaries (crossfade between end and start of loop), on play/stop transitions (fade in/out), and on seeks while playing (crossfade from old to new position). At 0.0, no fading — useful for samples with pre-edited seamless loop points.

**`tempo_lock`**: When enabled (>= 0.5), the effective playback speed is scaled by `engine_tempo / buffer_tempo`. This synchronizes playback to the engine's tempo -- if the buffer was recorded at 120 BPM and the engine is running at 240 BPM, playback runs at 2x speed. When the buffer has no tempo metadata (0.0), tempo_lock has no effect (base rate remains 1.0). Requires Engine PlayHead wiring via `setPlayHead()`.

**`transpose`**: Semitone pitch shift applied on top of the effective speed. `transpose = 12` doubles the speed (one octave up), `transpose = -12` halves it (one octave down). Formula: `speed *= 2^(transpose / 12)`. Combines with both manual speed and tempo_lock.

### Display Text

| Parameter | Examples |
|-----------|----------|
| playing | "Stopped", "Playing" |
| position | "0.0%", "50.0%", "100.0%" |
| speed | "1.0x", "-0.5x", "2.0x" |
| loop_mode | "Off", "Forward", "Ping-pong" |
| loop_start | "0.0%", "25.0%" |
| loop_end | "100.0%", "75.0%" |
| fade_ms | "5.0 ms", "0.0 ms" |
| tempo_lock | "Off", "On" |
| transpose | "+3.0 st", "-12.0 st", "0.0 st" |

## Effective Speed

The final playback speed combines tempo_lock, manual speed, and transpose:

```
base_rate = (tempo_lock && buffer.tempo > 0) ? engine_tempo / buffer.tempo : 1.0
effective_speed = base_rate * speed * 2^(transpose / 12)
```

When tempo_lock is off and transpose is 0, this reduces to just `speed`.

## Sample Rate Handling

PlayerProcessor automatically compensates for sample rate mismatch between Buffer and Engine. The read increment per output sample is:

```
increment = effective_speed * (buffer.sampleRate / engineSampleRate)
```

`speed = 1.0` (with no tempo_lock or transpose) always produces original-pitch, original-duration playback regardless of sample rate mismatch.

## Channel Handling

- Mono buffer, stereo output: both output channels receive the same signal
- Stereo buffer, stereo output: left and right channels play directly
- N-channel buffer (N > 2), stereo output: first two channels are used
- No buffer assigned: output silence

## Interpolation

Delegated to PlaybackCursor, which uses cubic Hermite interpolation. This provides good quality at variable playback speeds with minimal CPU cost.

## Buffer Assignment

`setBuffer(const Buffer* buffer)` assigns a buffer for playback. Called from the control thread (FFI layer orchestrates via buffer ID lookup).

- Setting a new buffer resets `position` to 0.0 and `playing` to 0.0
- Setting to nullptr: `process()` outputs silence; parameters remain readable/writable
- The Buffer must outlive the PlayerProcessor or be set to nullptr first — the FFI layer handles this via deferred deletion
- The `Buffer*` is stored atomically for audio-thread visibility

## Lifecycle

**`prepare(sampleRate, blockSize)`**: Store sample rate, pre-allocate crossfade buffer.

**`process(buffer)`**: Read from the assigned Buffer into the output buffer. Advance the playhead. Handle loop boundaries and crossfades. Clear output to silence when not playing or no buffer assigned.

**`reset()`**: Clear crossfade state and any in-progress fade. Preserve all parameter values including `position` and `playing`. Preserve buffer assignment. This matches the Processor convention — reset clears transient processing state, not user-controlled state.

**`release()`**: Release pre-allocated state.

## C ABI

### Source creation

```c
SqSource sq_add_source_player(SqEngine engine, const char* name, char** error);
```

Creates a Source with a PlayerProcessor as its generator. Returns the Source handle, or NULL on failure.

### Buffer assignment

```c
bool sq_source_set_buffer(SqEngine engine, SqSource source, int buffer_id);
```

Looks up the buffer by ID, calls `setBuffer()` on the Source's generator. Returns false if the buffer ID is not found or the generator is not a PlayerProcessor.

### Playback control

All playback control goes through existing parameter functions — no new API:

```c
SqProc gen = sq_source_generator(engine, source);

sq_set_param(engine, gen, "playing", 1.0f);
sq_set_param(engine, gen, "speed", -0.5f);
sq_set_param(engine, gen, "position", 0.25f);
sq_set_param(engine, gen, "loop_mode", 1.0f);

// Schedule play at beat 1
sq_schedule_param_change(engine, gen, "playing", 1.0f, 1.0);
```

## Python API

```python
src = sq.add_source("loop1", player=True)
buf_id = sq.load_buffer("/path/to/loop.wav")   # tier 21 (BufferLibrary)
src.set_buffer(buf_id)

# Control via parameters — same as any processor
src["playing"] = 1.0
src["speed"] = 0.5
src["loop_mode"] = 1   # forward
src["loop_start"] = 0.25
src["loop_end"] = 0.75

# Read current position
print(src["position"])  # e.g. 0.42

# Schedule play at beat 1
sq.schedule(src.generator, "playing", 1.0, beat=1.0)
```

## Invariants

- `process()` is RT-safe: no allocation, no blocking, no unbounded loops
- Buffer data is read-only during playback — PlayerProcessor never modifies the Buffer
- When buffer is nullptr, `process()` outputs silence
- `position` is always in [0.0, 1.0] when read
- `speed = 0.0` outputs silence and does not advance position
- Sample rate compensation is automatic — `speed = 1.0` = original pitch at any engine sample rate
- Crossfade reads never access outside buffer bounds
- Play/stop transitions apply fade to avoid clicks
- Seeks while playing apply crossfade to avoid clicks
- When `loop_mode = off` and playback reaches buffer end, `playing` automatically becomes 0.0
- `getLatencySamples()` returns 0
- After `setBuffer()`, position resets to 0.0 and playing becomes 0.0
- `reset()` clears crossfade state but preserves all parameters and buffer assignment
- All parameters are automatable via EventScheduler

## Error Conditions

- `setBuffer()` with a buffer that is later freed without clearing: undefined behavior (caller's responsibility — FFI handles via deferred deletion)
- `setParameter()` with unknown name: no-op (inherited from Processor)
- `setParameter("position", x)` with x outside [0.0, 1.0]: clamped
- `setParameter("speed", x)` with x outside [-4.0, 4.0]: clamped
- `process()` called before `prepare()`: undefined behavior (caller's responsibility)
- `loop_start >= loop_end` with `loop_mode != off`: loop region treated as full buffer
- `sq_source_set_buffer()` with unknown buffer ID: returns false, no-op
- `sq_source_set_buffer()` on a non-PlayerProcessor source: returns false, no-op

## Does NOT Handle

- **Buffer loading / creation** — BufferLibrary (tier 21) or Buffer FFI (tier 16)
- **Buffer lifetime management** — FFI layer + deferred deletion
- **MIDI input** — PlayerProcessor ignores MIDI. Use SamplerProcessor for MIDI-triggered playback
- **Per-voice envelopes / filters** — use chain inserts on the Source
- **Time-stretching** — future enhancement via TimeStretchEngine. Currently varispeed only (speed changes pitch)
- **Recording** — RecordingProcessor (separate component)
- **Slicing** — use multiple Sources with different `loop_start`/`loop_end` regions on the same buffer
- **Beat grid / warp markers** — higher-level concern
- **Volume / pan** — Source gain and pan handle this

## Dependencies

- Processor (base class)
- PlaybackCursor (buffer reading, interpolation, loop handling)
- Buffer (audio data, read-only access — via PlaybackCursor)
- Logger (`SQ_*` macros)

No dependency on VoiceAllocator, SamplerVoice, or TimeStretchEngine.

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| Constructor / destructor | Control | |
| `prepare()` / `release()` | Control | |
| `process()` | Audio | Reads buffer data, advances position |
| `reset()` | Audio | Called by Engine on un-bypass; clears crossfade state; RT-safe |
| `setParameter()` | Control | Atomic writes for audio-thread-visible state |
| `getParameter()` | Control | `position` reads current playback position atomically |
| `setBuffer()` | Control | Atomic pointer store |
| `getBuffer()` | Control | |

Parameter writes (control thread) and reads (audio thread `process()`) are safe for individual aligned scalar fields without explicit synchronization — same pattern used throughout the codebase.

## Tier Placement

PlayerProcessor depends only on Buffer (tier 16) and Processor (tier 1). It can be implemented immediately after Buffer, before the SamplerVoice/VoiceAllocator/SamplerProcessor stack (tiers 17–20). Suggested placement: **tier 16b** (after Buffer, before SamplerVoice).

Time-stretch support can be added as a parameter mode (`stretch_mode`) once TimeStretchEngine (tier 18) is available, following the same pattern as SamplerProcessor.

## Example Usage

### C ABI

```c
SqEngine engine = sq_create(44100.0, 512, &error);

// Create a buffer with audio data
int buf_id = sq_create_buffer(engine, 2, 44100 * 4, 44100.0, "loop", &error);
// ... sq_buffer_write() to fill it ...

// Create a player source
SqSource src = sq_add_source_player(engine, "break", &error);
sq_source_set_buffer(engine, src, buf_id);

// Get generator handle for parameter control
SqProc gen = sq_source_generator(engine, src);

// Set up looping
sq_set_param(engine, gen, "loop_mode", 1.0f);     // forward
sq_set_param(engine, gen, "loop_start", 0.0f);
sq_set_param(engine, gen, "loop_end", 0.5f);       // loop first half
sq_set_param(engine, gen, "fade_ms", 10.0f);

// Start playback
sq_set_param(engine, gen, "playing", 1.0f);

// Half speed while playing
sq_set_param(engine, gen, "speed", 0.5f);

// Schedule stop at beat 8
sq_schedule_param_change(engine, gen, "playing", 0.0f, 8.0);

sq_destroy(engine);
```

### Python

```python
from squeeze import Squeeze

s = Squeeze()

buf_id = s.load_buffer("/samples/break.wav")
src = s.add_source("break", player=True)
src.set_buffer(buf_id)

# Loop the first half
src["loop_mode"] = 1
src["loop_end"] = 0.5
src["fade_ms"] = 10.0

# Play
src["playing"] = 1.0

# Half speed, pitched down
src["speed"] = 0.5

# Reverse
src["speed"] = -1.0

# Schedule stop at beat 8
s.schedule(src.generator, "playing", 0.0, beat=8.0)

s.close()
```
