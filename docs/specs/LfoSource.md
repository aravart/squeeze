# LfoSource Specification

## Overview

A built-in modulation source that generates standard LFO waveforms. Inherits from `ModSource`. Operates in either free-running mode (rate in Hz) or beat-synced mode (rate derived from Transport position). Produces a bipolar output buffer (-1.0 to +1.0) consumed by the modulation routing system.

Ships as part of Phase 6 (tier 25) alongside the Modulation infrastructure.

---

## Responsibilities

- Generate six standard waveform shapes at configurable rates
- Support free-running (Hz) and beat-synced (division) rate modes
- Maintain phase state across blocks (free-running mode)
- Derive phase from transport position (beat-sync mode)
- Expose all parameters through the string-based parameter interface
- Fill the output buffer with one value per sample per block

---

## Interface

```cpp
class LfoSource : public ModSource {
public:
    explicit LfoSource(const std::string& name);

    // --- Lifecycle ---
    void prepare(double sampleRate, int blockSize) override;
    void reset() override;

    // --- Processing (audio thread) ---
    void process(int numSamples, double tempo,
                 double blockStartBeats) override;

    // --- Parameters ---
    int getParameterCount() const override;               // 5
    std::vector<ParamDescriptor> getParameterDescriptors() const override;
    float getParameter(const std::string& name) const override;
    void setParameter(const std::string& name, float value) override;

private:
    // Parameters
    int shape_     = 0;       // 0–5
    float rate_    = 1.0f;    // Hz (free-running mode)
    bool sync_     = false;   // beat-sync mode
    int division_  = 4;       // 0–11 (beat-sync mode)
    float phase_   = 0.0f;   // phase offset 0.0–1.0

    // State
    double sampleRate_ = 48000.0;
    double freePhase_  = 0.0;    // free-running phase accumulator [0, 1)
    float randomValue_ = 0.0f;  // current S&H value
    // RNG state for random waveform (deterministic, no allocation)
};
```

---

## Parameters

| Name | Normalized Range | Default | numSteps | boolean | automatable | modulatable | label | Description |
|------|-----------------|---------|----------|---------|-------------|-------------|-------|-------------|
| shape | 0.0–1.0 | 0.0 | 6 | false | true | false | "" | Waveform: 0=sine, 1=tri, 2=saw up, 3=saw down, 4=square, 5=random |
| rate | 0.0–1.0 | 0.01 | 0 | false | true | true | "Hz" | Free-running rate. Native range 0.01–100.0 Hz, stored/set as normalized 0–1. |
| sync | 0.0 or 1.0 | 0.0 | 0 | true | true | false | "" | Beat-sync toggle |
| division | 0.0–1.0 | 0.364 | 12 | false | true | false | "" | Beat division index 0–11 (see table) |
| phase | 0.0–1.0 | 0.0 | 0 | false | true | true | "" | Phase offset (0 = no offset, 0.5 = 180 degrees) |

**`shape` is not modulatable** — discrete/stepped parameter.
**`sync` is not modulatable** — boolean parameter.
**`division` is not modulatable** — discrete/stepped parameter.
**`rate` and `phase` are modulatable** — continuous parameters (though modulating an LFO's rate requires ModSource chaining, which is Phase 2).

### Division Index to Beat Mapping

| Index | Division | Beats (quarter notes) |
|-------|----------|----------------------|
| 0 | 4 bars | 16.0 |
| 1 | 2 bars | 8.0 |
| 2 | 1 bar | 4.0 |
| 3 | 1/2 | 2.0 |
| 4 | 1/4 | 1.0 |
| 5 | 1/8 | 0.5 |
| 6 | 1/16 | 0.25 |
| 7 | 1/32 | 0.125 |
| 8 | 1/4T | 2.0 / 3.0 |
| 9 | 1/8T | 1.0 / 3.0 |
| 10 | 1/16T | 0.5 / 3.0 |
| 11 | 1/32T | 0.25 / 3.0 |

Triplet values are exact fractions, not rounded floats.

---

## Waveforms

All waveforms are defined as a function of phase `p` where `p` is in [0, 1). Output range is [-1, +1].

| Shape | Index | Formula | Notes |
|-------|-------|---------|-------|
| Sine | 0 | `sin(2 * pi * p)` | Standard sine, starts at 0, peaks at +1 at p=0.25 |
| Triangle | 1 | `p < 0.5 ? (4*p - 1) : (3 - 4*p)` | Rises -1→+1 in first half, falls +1→-1 in second |
| Saw up | 2 | `2*p - 1` | Rises linearly from -1 to +1, resets at cycle boundary |
| Saw down | 3 | `1 - 2*p` | Falls linearly from +1 to -1, resets at cycle boundary |
| Square | 4 | `p < 0.5 ? 1.0 : -1.0` | +1 for first half, -1 for second half |
| Random | 5 | S&H: new random value at each cycle start | Held constant until next cycle boundary |

### Random (Sample & Hold)

At each phase wrap (when phase crosses from >= some threshold back below it, or on the first sample of a new cycle), a new random value is generated uniformly in [-1, +1]. The value is held constant for the entire cycle. This produces a staircase pattern at the LFO rate.

The RNG must be deterministic and RT-safe. Use a simple LCG or xorshift — no `std::random_device`, no `std::mt19937` (heap allocation in some implementations). Seed from a counter or fixed value. Exact sequence is not specified — only the distribution and timing constraints matter.

---

## Processing Modes

### Free-running Mode (sync = false)

Phase advances per sample by `rate / sampleRate`:

```
for s in 0..numSamples-1:
    output[s] = waveform(freePhase_ + phase_)
    freePhase_ += rate_ / sampleRate_
    if freePhase_ >= 1.0:
        freePhase_ -= 1.0
        if shape == random:
            randomValue_ = nextRandom()
```

Phase state (`freePhase_`) persists across blocks. `reset()` resets it to 0.

The `phase_` parameter is an offset added to `freePhase_` — changing it shifts the entire waveform without resetting the accumulator. The waveform function receives `frac(freePhase_ + phase_)`.

### Beat-sync Mode (sync = true)

Phase is derived directly from the transport position each block:

```
divisionBeats = divisionTable[division_]

for s in 0..numSamples-1:
    beatPos = blockStartBeats + s * (tempo / (60.0 * sampleRate))
    p = frac(beatPos / divisionBeats + phase_)
    output[s] = waveform(p)
```

No phase accumulator — the LFO is stateless with respect to the transport. Seeking, tempo changes, and loop wraps are automatically handled because phase is derived from absolute beat position.

**Random shape in beat-sync mode:** The random value changes at each cycle boundary. Since phase is derived from transport position, the cycle boundary is where `floor(beatPos / divisionBeats)` increments. The random value should be deterministic for a given cycle index so that looping produces consistent results. Seed the RNG with `floor(beatPos / divisionBeats)` to achieve this.

**Transport stopped:** When the transport is not playing, `blockStartBeats` does not advance. The LFO output is constant (the waveform evaluated at the static phase). This is correct — a beat-synced LFO should freeze when the transport stops.

---

## Lifecycle

### prepare(sampleRate, blockSize)

- Store `sampleRate_`
- Resize `outputBuffer_` to `blockSize`
- Do NOT reset phase (allows seamless sample rate changes during playback)

### reset()

- Reset `freePhase_` to 0.0
- Generate a new initial `randomValue_`

---

## Invariants

1. Output values are always in [-1.0, +1.0]
2. `freePhase_` is always in [0.0, 1.0)
3. All waveforms are continuous within a cycle (except square and random, which are piecewise constant)
4. Beat-sync phase is deterministic given the same transport position, tempo, division, and phase offset
5. `process()` writes exactly `numSamples` values to `outputBuffer_`
6. No allocation in `process()` — buffer is pre-allocated in `prepare()`
7. Random S&H value changes only at cycle boundaries, not mid-cycle
8. Phase offset changes take effect immediately (no smoothing)

---

## Error Conditions

| Condition | Behavior |
|-----------|----------|
| `setParameter` with unknown name | Ignored (no-op), logged at trace level |
| `rate` set to 0 or negative | Clamped to 0.01 Hz minimum |
| `shape` set out of range | Clamped to [0, 5] |
| `division` set out of range | Clamped to [0, 11] |
| `phase` set out of range | Wrapped to [0, 1) via `frac()` |
| `process()` called before `prepare()` | Output buffer may be empty/zero; no crash |
| `tempo` is 0 in beat-sync mode | Output is constant (phase derived from 0 tempo = no advance) |

---

## Thread Safety

| Operation | Thread | Notes |
|-----------|--------|-------|
| `prepare()` | Control | Called under Engine's controlMutex_ |
| `reset()` | Control | Called under Engine's controlMutex_ |
| `setParameter()` | Control | Under controlMutex_. Writes to plain member variables. |
| `getParameter()` | Control | Under controlMutex_. Reads plain member variables. |
| `process()` | Audio | Reads parameters set by control thread. See note below. |

**Parameter reads in `process()`:** LfoSource parameters (`shape_`, `rate_`, `sync_`, `division_`, `phase_`) are plain member variables written by the control thread and read by the audio thread. This is safe because:
- Parameter changes go through the Engine's control mutex and trigger a snapshot rebuild
- The snapshot swap ensures the audio thread sees a consistent state
- No tearing risk: `int`, `bool`, and `float` writes are atomic on all target platforms

If a parameter changes mid-block (between snapshot swaps), the audio thread reads the old value for that block and picks up the new value on the next block. This is acceptable — one block of latency on parameter changes is standard.

---

## Does NOT Handle

- **Retrigger on note-on:** Resetting phase when a MIDI note arrives — requires MIDI routing to mod sources, out of scope
- **One-shot mode:** Running a single cycle then holding (envelope-like behavior) — separate EnvelopeSource component
- **Waveform morphing:** Crossfading between shapes — Phase 2
- **Rate modulation by another ModSource:** Requires ModSource chaining — Phase 2
- **Smoothing on parameter changes:** Abrupt transitions are acceptable for LFO parameters; the modulation target's own smoothing handles artifacts
- **Polyphonic LFOs:** Per-voice phase tracking — separate concern for voice-level modulation
- **Custom waveforms:** User-defined waveshapes — Phase 2

---

## Dependencies

- `ModSource` (base class)
- `ParamDescriptor` (parameter descriptors)
- Standard math functions (`sin`, `fmod`/`frac`)
- No JUCE dependencies
- No external dependencies

---

## Example Usage

### C++ (standalone, for testing)

```cpp
LfoSource lfo("test_lfo");
lfo.prepare(48000.0, 512);

// Sine at 2 Hz
lfo.setParameter("shape", 0.0f);   // sine
lfo.setParameter("rate", 0.02f);   // normalized: maps to ~2 Hz
lfo.setParameter("sync", 0.0f);    // free-running

lfo.process(512, 120.0, 0.0);

const float* out = lfo.getOutput();
// out[0..511] contains one cycle of sine at 2 Hz (if block = 512 @ 48kHz)
```

### Beat-synced

```cpp
LfoSource lfo("synced");
lfo.prepare(48000.0, 512);

lfo.setParameter("sync", 1.0f);
lfo.setParameter("division", 0.364f);  // index 4 = 1/4 note
lfo.setParameter("shape", 0.0f);       // sine

// Process with transport at beat 0.0, 120 BPM
lfo.process(512, 120.0, 0.0);
// Phase is derived from beat position — 1/4 note = 1 beat cycle at 120 BPM = 2 Hz
```

### Through Engine (typical usage)

```cpp
auto* lfo = engine.addLfo("filter_mod");
lfo->setParameter("shape", 0.0f);    // sine
lfo->setParameter("rate", 0.04f);    // ~4 Hz
lfo->setParameter("phase", 0.25f);   // 90 degree offset

std::string err;
int route = engine.modRoute(lfo->getHandle(), filterProc->getHandle(),
                             "cutoff", 0.4f, err);
```
