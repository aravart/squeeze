# PlaybackCursor Specification

## Responsibilities

- Read audio from a Buffer at a fractional sample position with cubic Hermite interpolation
- Advance the read position per sample at a variable rate
- Handle loop boundaries (forward, ping-pong) with crossfade
- Compensate for sample rate mismatch between Buffer and Engine
- Provide current position for external queries

PlaybackCursor is a **non-Processor internal component** — a small, RT-safe building block used by PlayerProcessor and SamplerVoice. It has no parameter system, no handles, no FFI surface. It is not user-facing.

## Interface

### C++ (`squeeze::PlaybackCursor`)

```cpp
namespace squeeze {

enum class LoopMode { off, forward, pingPong };

class PlaybackCursor {
public:
    PlaybackCursor();
    ~PlaybackCursor();

    // Non-copyable, movable
    PlaybackCursor(const PlaybackCursor&) = delete;
    PlaybackCursor& operator=(const PlaybackCursor&) = delete;
    PlaybackCursor(PlaybackCursor&&) noexcept;
    PlaybackCursor& operator=(PlaybackCursor&&) noexcept;

    // --- Configuration (control thread or before render) ---
    void prepare(double engineSampleRate);
    void reset();

    // --- Render (audio thread, RT-safe) ---

    /// Render numSamples into destL/destR from the buffer at the current position.
    /// rate: playback rate multiplier (1.0 = normal, negative = reverse).
    /// Returns the number of samples actually rendered (may be < numSamples if
    /// playback reaches end with looping off).
    int render(const Buffer* buffer, float* destL, float* destR,
               int numSamples, double rate,
               LoopMode loopMode, double loopStart, double loopEnd,
               double fadeSamples);

    // --- Position (lock-free) ---

    /// Set the playback position (normalized 0.0–1.0). Triggers crossfade if
    /// called while rendering (fadeSamples > 0).
    void seek(double normalizedPosition, const Buffer* buffer, double fadeSamples);

    /// Get the current position (normalized 0.0–1.0 relative to buffer length).
    /// Returns 0.0 if no buffer.
    double getPosition(const Buffer* buffer) const;

    /// Get the raw sample position (fractional).
    double getRawPosition() const;

    /// Set the raw sample position directly (used for retrigger, buffer change).
    void setRawPosition(double samplePosition);

    /// Returns true if the cursor has reached the end and stopped
    /// (only possible when loopMode == off).
    bool isStopped() const;

private:
    double position_ = 0.0;         // fractional sample position
    double engineSampleRate_ = 44100.0;
    bool stopped_ = false;
    int direction_ = 1;             // +1 or -1 for ping-pong

    // Crossfade state for seeks
    bool crossfading_ = false;
    double crossfadePosition_ = 0.0;
    double crossfadeRemaining_ = 0.0;
    double crossfadeLength_ = 0.0;
};

} // namespace squeeze
```

### Design Rationale

All playback parameters (`rate`, `loopMode`, `loopStart`, `loopEnd`, `fadeSamples`) are passed per-render-call rather than stored as state. This keeps PlaybackCursor stateless with respect to playback configuration — the caller owns all parameters. The cursor only maintains position and crossfade state.

This design supports both use cases:
- **PlayerProcessor**: passes parameter values directly from its parameter storage
- **SamplerVoice**: computes rate from MIDI note pitch ratio, and controls loop points per voice

## Render Behavior

### Reading samples

For each output sample, PlaybackCursor reads from the Buffer at the current fractional position using **cubic Hermite interpolation** across 4 neighboring samples (positions floor-1, floor, floor+1, floor+2). At buffer boundaries, out-of-range samples are clamped to the nearest valid sample.

### Advancing position

After reading, the position advances by:

```
position_ += rate * direction_ * (buffer->getSampleRate() / engineSampleRate_)
```

Where `direction_` is +1 normally, and flips to -1 during ping-pong reverse segments. When `rate` is negative, the effective direction reverses (rate sign and direction_ combine).

### Loop handling

`loopStart` and `loopEnd` are normalized (0.0–1.0) and converted to sample positions using `buffer->getLengthInSamples()`.

**Loop mode off**: When position passes the buffer end (or start, if going in reverse), rendering stops. `isStopped()` returns true. Remaining output samples are filled with silence.

**Loop mode forward**: When position reaches `loopEnd`, it wraps to `loopStart`. If `fadeSamples > 0`, a crossfade is applied: the cursor simultaneously reads from the old position (fading out) and the new position (fading in) over `fadeSamples` samples using an equal-power crossfade.

**Loop mode ping-pong**: When position reaches `loopEnd`, `direction_` flips to -1. When position reaches `loopStart`, `direction_` flips to +1. No crossfade is applied at ping-pong boundaries (the audio is continuous — the direction simply reverses).

### Loop region validation

If `loopStart >= loopEnd` (in samples), the full buffer is used as the loop region (equivalent to `loopStart = 0.0`, `loopEnd = 1.0`).

## Seek Behavior

`seek()` sets a new playback position. If `fadeSamples > 0` and the cursor is currently being rendered, a crossfade is initiated: the old position continues reading (fading out) while the new position starts reading (fading in), over `fadeSamples` output samples. If `fadeSamples == 0`, the position jumps immediately.

After a seek, `stopped_` is cleared (the cursor is ready to render from the new position).

## Channel Handling

`render()` writes to `destL` and `destR` (always stereo output):

- **Mono buffer**: Same sample written to both channels
- **Stereo buffer**: Channel 0 → destL, channel 1 → destR
- **N-channel buffer (N > 2)**: First two channels used
- **Null buffer**: Fills with silence, returns 0

## Interpolation

Cubic Hermite interpolation using 4 samples: `s[i-1], s[i], s[i+1], s[i+2]` where `i = floor(position)` and `t = frac(position)`:

```
a0 = -0.5*s[i-1] + 1.5*s[i] - 1.5*s[i+1] + 0.5*s[i+2]
a1 =      s[i-1] - 2.5*s[i] + 2.0*s[i+1] - 0.5*s[i+2]
a2 = -0.5*s[i-1]             + 0.5*s[i+1]
a3 =                    s[i]

output = ((a0*t + a1)*t + a2)*t + a3
```

At buffer boundaries, out-of-range sample indices are clamped to `[0, length-1]`.

## Invariants

- `render()` is RT-safe: no allocation, no blocking, no unbounded loops
- Buffer data is read-only — PlaybackCursor never modifies the Buffer
- When buffer is nullptr, render fills output with silence and returns 0
- `getPosition()` returns a value in [0.0, 1.0]
- Crossfade reads never access outside buffer bounds (clamped interpolation)
- `prepare()` must be called before the first `render()`
- `reset()` sets position to 0.0, clears stopped and crossfade state, preserves engineSampleRate
- After `isStopped()` returns true, subsequent `render()` calls output silence until `seek()` or `reset()`

## Error Conditions

- `render()` with nullptr buffer: fills silence, returns 0
- `render()` with numSamples <= 0: returns 0
- `render()` with destL or destR nullptr: undefined behavior (caller's responsibility)
- `loopStart >= loopEnd`: treated as full buffer loop region
- `fadeSamples < 0`: treated as 0

## Does NOT Handle

- **Parameter storage** — callers own all playback parameters
- **Play/stop state** — PlayerProcessor manages this via its `playing` parameter
- **Envelopes** — SamplerVoice applies amplitude/filter envelopes after rendering
- **Filters** — SamplerVoice applies filter after rendering
- **MIDI** — SamplerVoice translates MIDI note to rate
- **Time-stretching** — TimeStretchEngine is a separate post-processing stage
- **Buffer lifecycle** — callers ensure buffer validity

## Dependencies

- Buffer (read-only audio data access)
- No JUCE dependency (operates on raw float pointers)

## Thread Safety

PlaybackCursor is **not thread-safe**. It is designed to be owned and called from a single thread (the audio thread) during `render()`, with configuration (`seek()`, `reset()`, `setRawPosition()`) happening either on the control thread when the audio thread is not rendering, or mediated by the owning component's synchronization (e.g., atomic flags in PlayerProcessor).

| Method | Thread | Notes |
|--------|--------|-------|
| `prepare()` | Control | Before first render |
| `reset()` | Control | Or audio thread (by owner, when not mid-render) |
| `render()` | Audio | RT-safe |
| `seek()` | Audio | Called by owner during render (from atomic seek flag) |
| `getPosition()` | Any | Reads a double — atomic on 64-bit platforms |
| `getRawPosition()` | Any | Reads a double |
| `setRawPosition()` | Control | Or audio thread (by owner) |
| `isStopped()` | Any | Reads a bool |

## Example Usage

### Inside PlayerProcessor::process()

```cpp
void PlayerProcessor::process(juce::AudioBuffer<float>& buffer) {
    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : L;
    int numSamples = buffer.getNumSamples();

    if (!playing_ || !buffer_) {
        buffer.clear();
        // ... handle fade out ...
        return;
    }

    // Check for pending seek
    if (seekPending_.exchange(false)) {
        cursor_.seek(seekTarget_, buffer_, fadeSamples_);
    }

    int rendered = cursor_.render(buffer_, L, R, numSamples,
                                   speed_, loopMode_, loopStart_, loopEnd_,
                                   fadeSamples_);

    if (cursor_.isStopped()) {
        playing_ = false;
    }

    // Clear any remaining samples
    if (rendered < numSamples) {
        std::fill(L + rendered, L + numSamples, 0.0f);
        std::fill(R + rendered, R + numSamples, 0.0f);
    }
}
```

### Inside SamplerVoice::render()

```cpp
void SamplerVoice::renderBlock(float* destL, float* destR, int numSamples) {
    // Rate from MIDI note pitch
    double rate = pitchRatio_ * pitchBendFactor_;

    int rendered = cursor_.render(buffer_, destL, destR, numSamples,
                                   rate, loopMode_, loopStart_, loopEnd_,
                                   loopCrossfadeSamples_);

    // Apply amplitude envelope
    for (int i = 0; i < rendered; i++) {
        float env = ampEnvelope_.tick();
        destL[i] *= env;
        destR[i] *= env;
    }

    if (ampEnvelope_.isIdle()) {
        active_ = false;
    }
}
```
