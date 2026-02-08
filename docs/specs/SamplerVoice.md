# SamplerVoice Specification

## Overview

SamplerVoice is the DSP core of the sampler: a single monophonic voice that reads audio from a Buffer, applies pitch shifting via variable-rate playback, runs it through an amplitude envelope and an optional per-voice filter, and writes stereo output. It is **not a Node** — it is an internal component owned by VoiceAllocator and orchestrated by SamplerNode.

## Responsibilities

- Read audio from a Buffer at a variable playback rate (pitch shifting)
- Interpolate between samples using cubic Hermite interpolation
- Respect playback region boundaries (sample start / sample end)
- Implement loop modes: off (one-shot), forward, reverse, ping-pong
- Crossfade at loop boundaries to eliminate clicks
- Support forward and reverse playback direction
- Apply an AHDSR amplitude envelope with configurable per-stage curves
- Apply a per-voice state-variable filter (LP/HP/BP/notch) with its own ADSR envelope
- Apply volume, pan, and velocity scaling
- Track MIDI note for pitch calculation relative to a root note
- Manage voice state: idle, playing, releasing

## Types

```cpp
enum class LoopMode { off, forward, reverse, pingPong };
enum class PlayDirection { forward, reverse };
enum class FilterType { off, lowpass, highpass, bandpass, notch };
enum class EnvCurve { linear, exponential, logarithmic };
enum class VoiceState { idle, playing, releasing };
```

## Shared Parameters

SamplerVoice reads from a shared `SamplerParams` struct owned by SamplerNode. All voices in the pool share the same params pointer. Values are in natural/physical units — SamplerNode handles the mapping from the Node parameter system's normalized 0.0–1.0.

```cpp
struct SamplerParams {
    // Playback region (normalized to buffer length, 0.0–1.0)
    float sampleStart = 0.0f;
    float sampleEnd = 1.0f;
    int rootNote = 60;                          // MIDI note at original pitch

    // Loop
    float loopStart = 0.0f;                     // normalized to buffer length
    float loopEnd = 1.0f;
    LoopMode loopMode = LoopMode::off;
    float loopCrossfadeSec = 0.0f;              // crossfade length in seconds

    // Pitch & direction
    PlayDirection direction = PlayDirection::forward;
    int pitchSemitones = 0;                     // -48 to +48
    float pitchCents = 0.0f;                    // -100 to +100

    // Amplitude
    float volume = 1.0f;                        // linear gain 0.0–1.0
    float pan = 0.0f;                           // -1.0 (left) to +1.0 (right)
    float velSensitivity = 1.0f;                // 0.0–1.0

    // Amp envelope (times in seconds, sustain is level)
    float ampAttack = 0.001f;
    float ampHold = 0.0f;
    float ampDecay = 0.1f;
    float ampSustain = 1.0f;
    float ampRelease = 0.01f;
    EnvCurve ampAttackCurve = EnvCurve::linear;
    EnvCurve ampDecayCurve = EnvCurve::exponential;
    EnvCurve ampReleaseCurve = EnvCurve::exponential;

    // Filter
    FilterType filterType = FilterType::off;
    float filterCutoffHz = 20000.0f;            // 20–20000
    float filterResonance = 0.0f;               // 0.0–1.0
    float filterEnvAmount = 0.0f;               // -1.0 to +1.0

    // Filter envelope (times in seconds, sustain is level)
    float filterAttack = 0.001f;
    float filterDecay = 0.1f;
    float filterSustain = 1.0f;
    float filterRelease = 0.01f;
    EnvCurve filterAttackCurve = EnvCurve::linear;
    EnvCurve filterDecayCurve = EnvCurve::exponential;
    EnvCurve filterReleaseCurve = EnvCurve::exponential;
};
```

Field writes happen on the control thread; reads happen on the audio thread. Individual `float`/`int`/`enum` fields are naturally atomic on x86/ARM (aligned, single-word). No synchronization is needed — a voice may read a slightly stale value for one block, which is musically acceptable.

## Interface

```cpp
class SamplerVoice {
public:
    explicit SamplerVoice(const SamplerParams& params);

    void prepare(double sampleRate, int blockSize);
    void release();

    // Trigger / release
    void noteOn(const Buffer* buffer, int midiNote, float velocity,
                int startSampleInBlock);
    void noteOff(int startSampleInBlock);

    // Render audio into output buffer (additive — adds to existing content)
    void render(juce::AudioBuffer<float>& output, int sampleOffset, int numSamples);

    // State queries (audio thread)
    VoiceState getState() const;
    int getCurrentNote() const;          // -1 if idle
    float getEnvelopeLevel() const;      // current amp envelope output, 0.0–1.0

    // For voice stealing decisions
    float getAge() const;                // seconds since noteOn
};
```

## Playback Engine

### Playback rate calculation

The playback rate determines how fast the read position advances through the buffer. It combines sample rate compensation, pitch parameters, and MIDI note tracking:

```
baseRate = buffer.sampleRate / engineSampleRate
totalSemitones = pitchSemitones + (pitchCents / 100.0) + (midiNote - rootNote)
rateMultiplier = pow(2.0, totalSemitones / 12.0)
playbackRate = baseRate * rateMultiplier
```

Direction is applied as a sign: forward = positive rate, reverse = negative rate.

### Playback region

`sampleStart` and `sampleEnd` define the playable region within the buffer, as normalized positions (0.0–1.0 of buffer length). The voice only reads samples within this region.

- On `noteOn`, the read position is set to `sampleStart` (forward) or `sampleEnd` (reverse)
- In one-shot mode (`loopMode == off`), playback stops when the position reaches `sampleEnd` (forward) or `sampleStart` (reverse), and the voice enters the release stage
- `sampleStart` must be < `sampleEnd` at all times. If violated, the voice treats the region as empty and remains idle

### Cubic Hermite interpolation

The read position is a `double` (integer part = sample index, fractional part = interpolation position). Four neighboring samples are used:

```
Given samples y[-1], y[0], y[1], y[2] and fractional position t in [0, 1):

a = -0.5*y[-1] + 1.5*y[0] - 1.5*y[1] + 0.5*y[2]
b =      y[-1] - 2.5*y[0] + 2.0*y[1] - 0.5*y[2]
c = -0.5*y[-1]             + 0.5*y[1]
d =               y[0]

result = ((a*t + b)*t + c)*t + d
```

At buffer boundaries, neighbor samples outside the buffer are clamped to the nearest valid sample (for one-shot) or wrapped (for loop modes).

### Stereo handling

- Mono buffer (1 channel): the same interpolated value is used for both output channels (before pan is applied)
- Stereo buffer (2 channels): each channel is interpolated independently at the same read position

## Loop Engine

### Loop region

`loopStart` and `loopEnd` define the loop region as normalized positions (0.0–1.0). The loop region must be within the playback region. If not, the loop region is clamped to the playback region boundaries.

### Loop modes

- **off**: one-shot playback. When the read position reaches `sampleEnd` (forward) or `sampleStart` (reverse), the amplitude envelope's release is triggered. The voice continues rendering during the release stage (from the last valid position or with the last sample held), then goes idle.

- **forward**: when the read position reaches `loopEnd`, it wraps to `loopStart`. Playback within the loop is always forward regardless of the `direction` parameter (direction only affects initial approach to the loop).

- **reverse**: when the read position reaches `loopStart`, it wraps to `loopEnd`. Playback within the loop is always reverse.

- **pingPong**: the read direction alternates each time a loop boundary is reached. On reaching `loopEnd`, direction flips to reverse. On reaching `loopStart`, direction flips to forward.

### Crossfade looping

When `loopCrossfadeSec > 0`, the loop transition is smoothed with an equal-power crossfade:

- The crossfade region length in samples = `loopCrossfadeSec * buffer.sampleRate`
- The crossfade length is clamped to at most half the loop length (to prevent overlap)
- As the read position enters the crossfade region near `loopEnd`, the output blends between the current position and the corresponding position near `loopStart`:

```
For forward loop, when position is within crossfadeSamples of loopEnd:
  distanceToEnd = loopEnd - position
  t = distanceToEnd / crossfadeLength          // 1.0 at crossfade start, 0.0 at loop point
  fadeOut = sqrt(t)                             // equal-power
  fadeIn = sqrt(1.0 - t)
  mirrorPos = loopStart + (crossfadeLength - distanceToEnd)
  output = fadeOut * sample(position) + fadeIn * sample(mirrorPos)
```

## Amplitude Envelope (AHDSR)

Five stages: Attack, Hold, Decay, Sustain, Release.

### Stage behavior

| Stage   | Behavior |
|---------|----------|
| Attack  | Ramp from 0.0 to 1.0 over `ampAttack` seconds using `ampAttackCurve` |
| Hold    | Stay at 1.0 for `ampHold` seconds |
| Decay   | Ramp from 1.0 to `ampSustain` over `ampDecay` seconds using `ampDecayCurve` |
| Sustain | Hold at `ampSustain` until `noteOff` |
| Release | Ramp from current level to 0.0 over `ampRelease` seconds using `ampReleaseCurve` |

`noteOff` transitions to Release from any stage. The release always starts from the current envelope level (not necessarily `ampSustain` — it could be mid-attack or mid-decay).

When the release stage completes (level reaches 0.0), the voice transitions to `idle`.

### Curve shapes

Each stage's curve is independently configurable:

- **linear**: `output = t` (straight line)
- **exponential**: `output = t^k` where k > 1 (slow start, fast finish — natural for attack)
- **logarithmic**: `output = 1 - (1-t)^k` (fast start, slow finish — natural for decay/release)

The exponent `k` is a fixed constant (e.g., 3.0 or 4.0) chosen for musical feel. A single constant for all curves keeps the interface simple while providing audibly distinct shapes. The curves are unipolar (0.0–1.0) and applied to the stage's start→end value range.

### Envelope reads parameters continuously

The envelope reads its time and curve parameters from `SamplerParams` every sample (or every block if optimizing). This means envelope times can be modulated in real-time — changing `ampAttack` while a voice is in the attack stage will affect the remainder of the attack.

### Robustness under parameter changes

The envelope tracks progress through each stage as a normalized `position` (0.0–1.0). The increment per sample is `(1.0 / sampleRate) / stageTime`. Because the increment is recomputed from the current parameter value every sample, drastic changes are handled naturally:

- **Time shrinks drastically** (e.g. 2.0s → 0.001s mid-attack): the increment becomes very large, `position` overshoots 1.0, the `>= 1.0` check fires, and the stage completes immediately on the next sample. The level jumps to the stage's target value (e.g. 1.0 for attack). This is musically correct — no click, since the level moves in the direction it was already heading.

- **Time grows drastically** (e.g. 0.001s → 2.0s mid-attack): the increment becomes very small, and the stage continues slowly from wherever `position` was. Smooth, no discontinuity.

- **Time becomes zero mid-stage**: division by zero produces infinity, `position` goes to infinity, the `>= 1.0` check completes the stage immediately. Same as the "shrinks drastically" case.

- **Negative stage times**: all stage times are clamped to `max(0.0, value)` before use. A negative time is treated as instant (0.0), triggering the cascade logic that advances through zero-length stages in a single call.

- **NaN protection**: `applyCurve` clamps its input to [0.0, 1.0]. The `max(0.0, ...)` clamp on stage times also blocks NaN propagation from parameters (since NaN comparisons return false, `std::max(0.0f, NaN)` returns 0.0f).

The same protections apply to the filter ADSR envelope.

## Filter

### State-variable filter

When `filterType != off`, the voice runs audio through a state-variable topology-preserving transform (TPT) filter. Implementation uses `juce::dsp::StateVariableTPTFilter`.

Filter types:
- **lowpass**: standard 12 dB/oct lowpass
- **highpass**: 12 dB/oct highpass
- **bandpass**: 12 dB/oct bandpass
- **notch**: band-reject

### Cutoff modulation

The effective filter cutoff combines the base cutoff with the filter envelope:

```
envValue = filterEnvelope.currentLevel()    // 0.0–1.0
modulation = filterEnvAmount * envValue     // -1.0 to +1.0
effectiveCutoff = filterCutoffHz * pow(2.0, modulation * 10.0)  // ~10 octaves range
effectiveCutoff = clamp(effectiveCutoff, 20.0, 20000.0)
```

The filter envelope is an ADSR (no hold stage) triggered simultaneously with the amplitude envelope. It has its own attack, decay, sustain, release times and curve shapes.

### Filter reset on noteOn

The filter state is reset on each `noteOn` to prevent bleed from previous notes. This avoids clicks from stale filter state but means there's no filter "memory" across notes.

## Velocity

Velocity (0–127 from MIDI) is stored per-voice at `noteOn` and applied as a volume modifier:

```
velocityGain = 1.0 - velSensitivity * (1.0 - velocity / 127.0)
```

At `velSensitivity = 0.0`: velocity has no effect (gain always 1.0).
At `velSensitivity = 1.0`: full velocity response (gain ranges 0.0–1.0 with velocity).

## Pan

Pan uses two modes depending on the source buffer's channel count:

### Mono buffers — constant-power pan

```
angle = (pan + 1.0) * pi / 4.0     // 0 to pi/2
leftGain = cos(angle)
rightGain = sin(angle)
```

At `pan = 0.0` (center): ~0.707 per channel (constant power).
At `pan = -1.0`: full left. At `pan = +1.0`: full right.

### Stereo buffers — balance control

```
leftGain  = pan <= 0.0 ? 1.0 : 1.0 - pan
rightGain = pan >= 0.0 ? 1.0 : 1.0 + pan
```

At `pan = 0.0` (center): both channels at unity (1.0), preserving the stereo image.
At `pan = -1.0`: left at 1.0, right at 0.0. At `pan = +1.0`: left at 0.0, right at 1.0.

### Rationale

Constant-power pan applied to stereo samples causes a -3 dB level drop at center position (each channel scaled by ~0.707). Balance control preserves the original stereo image at center and only attenuates the opposite channel when panning, which is the expected behavior for stereo samples.

## Render Pipeline

Per sample (within a render call):

```
1. Read from buffer at readPosition using cubic Hermite interpolation
   (handle stereo: interpolate each channel independently)
2. Apply loop crossfade if in crossfade region
3. Apply filter (if filterType != off):
   a. Advance filter envelope
   b. Compute effective cutoff
   c. Filter the sample(s)
4. Advance amplitude envelope
5. Apply amplitude envelope * velocity gain * volume
6. Apply pan (constant-power)
7. ADD to output buffer (voices are summed, not overwritten)
8. Advance read position by playbackRate
9. Handle loop wrapping / one-shot end detection
10. If amp envelope finished release → set state to idle
```

### Sample-accurate triggering

`noteOn` and `noteOff` accept a `startSampleInBlock` parameter. The voice's `render()` method handles this:

- A voice triggered at sample offset N within a block produces silence for samples 0..N-1 and begins playback at sample N
- A voice released at sample offset N continues sustaining through sample N-1 and enters release at sample N
- VoiceAllocator calls `render()` in sub-block segments to handle multiple events per block (see VoiceAllocator spec)

## Invariants

- `render()` is RT-safe: no allocation, no blocking, no I/O
- `render()` is additive — it adds to the output buffer, never clears it
- A voice in `idle` state produces zero output (render returns immediately)
- `noteOn` on a non-idle voice retriggers: resets position and envelopes, starts fresh
- The buffer pointer provided at `noteOn` must remain valid for the voice's lifetime (until it returns to `idle`)
- Read position never leaves the `[sampleStart, sampleEnd]` region (clamped or wrapped)
- Envelope level is always in [0.0, 1.0]
- Interpolation accesses at most 2 samples beyond the playback region boundary (for cubic neighbors); these are clamped to the nearest valid index
- Filter state is reset on each noteOn

## Numerical Stability & DSP Edge Cases

### Extreme playback rates

MIDI note 127 with rootNote 0 produces +127 semitones = 2^(127/12) ≈ 1534x playback rate. At this rate, each output sample skips ~1534 buffer samples, potentially crossing the loop region multiple times.

**Multi-wrap**: Loop wrapping uses `fmod(overshoot, loopLength)` to correctly compute the final position regardless of how many times the loop boundary is crossed. This handles forward and reverse loop modes. PingPong mode computes the number of boundary crossings to determine the final direction (odd crossings = reversed, even = same).

The audio output at extreme rates is ultrasonic and musically meaningless, but the implementation must not crash, produce NaN, or get stuck.

### Very short loop regions

A 3-sample loop with crossfade clamped to half the loop length = 1 sample of crossfade. This is technically correct but the crossfade is inaudible — the output is a buzzy waveform at the loop's fundamental frequency. No special handling needed; the existing clamp logic keeps crossfade within bounds.

### Read position precision

`readPosition_` is a `double` (52-bit mantissa, 2^52 ≈ 4.5×10^15). At 48 kHz for 24 hours of continuous one-shot playback, the position reaches ~4.15×10^9 samples. This leaves ~43 bits of fractional precision — more than sufficient for cubic interpolation (which only needs the fractional part to ~16 bits). In looping modes, the position wraps within the loop region and never accumulates.

### Filter resonance

`filterResonance` (0.0–1.0) maps to JUCE's `setResonance` parameter as `0.707 + resonance * 19.3`:

- At 0.0: resonance parameter = 0.707 (Butterworth, flat response, standard 12 dB/oct)
- At 1.0: resonance parameter ≈ 20 (Q ≈ 20, +26 dB peak at cutoff frequency)

The TPT (topology-preserving transform) filter structure is unconditionally stable — it uses trapezoidal integration which preserves the analog prototype's BIBO stability. At Q=20 the output amplitude near the resonant frequency can be 20x the input level, but the filter will not self-oscillate, diverge, or produce NaN/inf. No output clamping is applied; gain management is the responsibility of the user or a downstream limiter.

### Filter cutoff at boundaries

After envelope modulation, effective cutoff is clamped to [20 Hz, 20000 Hz]. At the extremes:
- 20 Hz lowpass: essentially passes nothing audible but produces no numerical issues
- 20000 Hz lowpass: passes everything, filter is effectively bypassed
- The JUCE TPT filter handles all frequencies within [0, sampleRate/2] correctly

## Error Conditions

- `noteOn` with null buffer: voice remains idle, no crash
- `sampleStart >= sampleEnd`: treated as empty region, voice remains idle
- `loopStart >= loopEnd`: loop is disabled (treated as `loopMode::off`)
- Buffer with 0 samples: voice remains idle
- `ampAttack/ampDecay/ampRelease` of 0: the stage is instantaneous (completes in one sample)
- Negative envelope stage times: clamped to 0 (treated as instant)
- Filter cutoff out of [20, 20000] after modulation: clamped silently
- Extreme playback rates (>1000x): loop wrapping uses fmod, output is finite but ultrasonic

## Does NOT Handle

- Timestretching (Phase 3 — separate TimeStretchEngine component)
- Slicing / slice selection (VoiceAllocator / SamplerNode concern)
- Voice allocation or stealing (VoiceAllocator concern)
- MIDI parsing (SamplerNode concern)
- Parameter normalization (SamplerNode concern)
- Disk streaming (Phase 3 — all samples are in-memory via Buffer)
- Multi-output routing (single stereo output per voice)
- Choke groups (VoiceAllocator concern)

## Dependencies

- `Buffer` — audio data source (read-only access via `getReadPointer()`, `getLengthInSamples()`, `getNumChannels()`, `getSampleRate()`)
- `juce::dsp::StateVariableTPTFilter` — per-voice filter
- `juce::AudioBuffer<float>` — output buffer
- `<cmath>` — `pow`, `sqrt`, `cos`, `sin` for pitch/pan/curves
- `Logger` — `SQ_LOG_RT()` for debug tracing (gated, near-zero cost when off)

## Thread Safety

- `prepare()`, `release()`: control thread only
- `noteOn()`, `noteOff()`, `render()`, `getState()`, `getCurrentNote()`, `getEnvelopeLevel()`, `getAge()`: audio thread only
- Reads from `SamplerParams`: audio thread reads, control thread writes. Safe without synchronization for individual aligned scalar fields.

## Example Usage

```cpp
SamplerParams params;
params.loopMode = LoopMode::forward;
params.ampAttack = 0.01f;
params.ampRelease = 0.1f;
params.filterType = FilterType::lowpass;
params.filterCutoffHz = 2000.0f;

SamplerVoice voice(params);
voice.prepare(44100.0, 512);

// In audio callback
Buffer* buf = engine.getBuffer(bufferId);
voice.noteOn(buf, 60, 100, 0);  // middle C, velocity 100, at sample 0

juce::AudioBuffer<float> output(2, 512);
output.clear();
voice.render(output, 0, 512);
// output now contains one block of audio

// Later, on note-off
voice.noteOff(0);
// Voice enters release stage, continues rendering, eventually goes idle
```
