# SamplerNode — Full Feature Set

A comprehensive sampler node for Squeeze, drawing from the Octatrack, MPC, SP-1200, Ableton Simpler/Sampler, and Kontakt.

---

## Core Playback Engine

### File handling

Load WAV, AIFF, FLAC (JUCE gives us all of these for free). Short samples live in RAM. Long samples (stems, field recordings, full tracks) stream from disk. Pick based on file size or let the user override.

### Playback region

Sample start and end points, independent of the file boundaries. This is the fundamental Octatrack gesture — sweeping the start point of a breakbeat in real-time. These must be modulatable parameters, not just static settings.

### Loop region

Loop start and loop end, independent of the playback region. Loop modes: off (one-shot), forward, reverse, ping-pong. Crossfade looping to eliminate clicks at the loop boundary — a crossfade length parameter controls how many samples are blended.

### Direction

Forward and reverse playback. Switchable in real-time (the Octatrack does this — reverse a loop mid-pattern for a tape-stop feel).

### Pitch

Coarse (semitones) and fine (cents). At the engine level this is playback rate — a ratio applied to the read position increment. Interpolation quality matters: linear is cheap but aliasy, cubic is the sweet spot, sinc for high-quality offline or if CPU allows.

---

## Timestretching

Independent control of pitch and time. This is what separates a modern sampler from an S-950.

### Beat-sync mode

Tell it the sample's original BPM (or detect it), and it stretches to match the master clock. Pitch stays constant. This is how you mix loops at different tempos without pitch shifting.

### Free mode

Set a stretch ratio manually. Useful for sound design — slow a vocal to half speed without pitch drop.

### Algorithms

- **Granular** — chop into overlapping grains, reposition. Good for pads, textures. Artifacts on transients.
- **WSOLA** (waveform similarity overlap-add) — smarter grain placement that preserves waveform shape. Better for rhythmic material.
- **Phase vocoder** — FFT-based. Cleanest for pitched material but smears transients.

Realistically, start with one (granular or WSOLA) and it covers 80% of cases.

---

## Slicing

### Slice points

A list of positions within the sample that divide it into regions. Three ways to create them:

- **Manual** — user places them
- **Grid** — divide evenly into N slices (8, 16, 32, 64)
- **Transient detection** — analyze the waveform and place slices at attacks

### Slice playback

Each MIDI note triggers a different slice. Note 0 = slice 0, note 1 = slice 1, etc. Each slice plays from its start point to either the next slice or the sample end. This is how you chop a breakbeat and rearrange it from a sequencer.

### Slice lock

The slice index itself is a modulatable parameter. In Elektron terms, you parameter-lock which slice plays on each step. In Squeeze terms, Lua sets it per event.

---

## Voice Management

### Polyphony modes

- **Mono** — one voice, retriggers on new notes. Essential for bass, leads, chopped beats.
- **Polyphonic** — N voices, configurable limit. For chords, layered pads.
- **Legato** — mono but doesn't retrigger envelope, glides pitch.

### Voice stealing

When polyphony is full: last note priority (most common), lowest, highest. Steal the oldest voice or the quietest.

### Choke groups

Voices in the same group cut each other. Open hihat chokes on closed hihat. The Octatrack doesn't have this natively (it's per-track mono), but the MPC does and it's essential for drum kits.

---

## Amplitude Envelope

### AHDSR

The Octatrack uses attack-hold-decay (no sustain/release — it's a sampler, not a synth). A full AHDSR is more flexible:

- Attack, decay, sustain, release at minimum
- Hold time (useful for one-shots that should play a fixed duration)
- Envelope curves (linear, exponential, logarithmic per stage)

### Volume and pan

Both modulatable. Pan is critical for placing slices across the stereo field.

---

## Built-in Filter

Debatable whether this belongs in the SamplerNode or as a separate node. The Octatrack builds it in (multimode filter per track). Arguments for building it in:

- Filter envelope triggered per-note is tightly coupled to the voice
- Key tracking (filter follows pitch) only works inside the voice
- Per-voice filtering is different from post-mix filtering

If included: multimode (lowpass, highpass, bandpass, notch), 12dB and 24dB slopes, resonance, cutoff as a modulatable parameter. Cutoff envelope (separate from amp envelope) with its own ADSR.

---

## Recording

This is where the Octatrack's "pickup machine" lives.

### Input recording

Record from the node's audio input into a buffer. Quantized start (wait for next beat/bar). Fixed-length or free-length.

### Overdub

Record over existing material, mixing new input with what's already there. This is loop-pedal territory.

### Replace

Like overdub but erases what was there.

### Buffer-as-sample

Once recorded, the buffer behaves identically to a loaded file — you can slice it, pitch it, timestretch it, reverse it. This unification is one of the Octatrack's best design decisions.

---

## Modulation Targets

Everything modulatable, exposed as a parameter:

- Sample start, sample end
- Loop start, loop end, crossfade length
- Pitch (coarse + fine)
- Playback direction
- Timestretch ratio
- Slice index
- Volume, pan
- Filter cutoff, resonance
- Amp envelope stages (AHDSR)
- Filter envelope stages
- Playback speed / rate

In the Octatrack, these are modulated by LFOs and parameter locks. In Squeeze, they're modulated by Lua — which is strictly more powerful.

---

## Incremental Build Plan

### Phase 1 — Playable (SP-1200)

Load file, play region, pitch, one-shot/loop, forward/reverse, mono voice, amp envelope, volume/pan. All parameters exposed to Lua.

### Phase 2 — Performable (MPC)

Slicing (grid + manual), polyphony, choke groups, crossfade looping.

### Phase 3 — Octatrack territory

Timestretching, beat-sync, input recording, overdub. Built-in filter if desired.

### Phase 4 — Beyond hardware

Granular playback mode (not just for stretching — actual granular synthesis with spray/density/grain size). Wavetable mode. FFT freeze. Things hardware can't do but Lua + a fast engine can.
