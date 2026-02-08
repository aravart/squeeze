# VoiceAllocator Specification

## Overview

VoiceAllocator manages a pool of SamplerVoice instances and handles note-on/note-off routing, polyphony modes, voice stealing, and choke groups. It decouples voice management logic from the DSP (SamplerVoice) and from the Node interface (SamplerNode).

## Responsibilities

- Own and manage a pre-allocated pool of SamplerVoice instances
- Route note-on events to available voices
- Route note-off events to the correct playing voices
- Implement polyphony modes: mono, poly, legato
- Steal voices when the pool is exhausted (poly mode)
- Implement choke groups (Phase 2)
- Render all active voices into a summed output buffer

## Types

```cpp
enum class VoiceMode { mono, poly, legato };
enum class StealPolicy { oldest, quietest };
```

## Interface

```cpp
class VoiceAllocator {
public:
    VoiceAllocator(int maxVoices, const SamplerParams& params);

    void prepare(double sampleRate, int blockSize);
    void release();

    // Note events (audio thread, called by SamplerNode between renderBlock segments)
    // These take effect immediately — SamplerNode is responsible for calling them
    // at the correct point between sub-block render segments.
    void noteOn(const Buffer* buffer, int midiNote, float velocity);
    void noteOff(int midiNote);
    void allNotesOff();

    // Render all active voices into output (additive)
    void renderBlock(juce::AudioBuffer<float>& output, int startSample,
                     int numSamples);

    // Configuration (control thread)
    void setMode(VoiceMode mode);
    void setStealPolicy(StealPolicy policy);
    void setMaxActiveVoices(int count);

    // Phase 2: choke groups
    // void setChokeGroup(int midiNote, int group);

    // State queries
    VoiceMode getMode() const;
    int getMaxVoices() const;
    int getActiveVoiceCount() const;
};
```

## Voice Pool

The pool is a fixed-size `std::vector<SamplerVoice>` allocated once in the constructor. Pool size equals `maxVoices` (constructor argument). No allocation occurs after construction.

All voices share the same `SamplerParams&` reference (passed through from SamplerNode). When SamplerNode updates a parameter, all voices see the new value on the next render call.

### Pool sizing

Phase 1 default: `maxVoices = 1` (mono behavior). Phase 2 extends to user-configurable values (1–32). The implementation pre-allocates all voices regardless of `maxActiveVoices` — `setMaxActiveVoices()` limits how many can be simultaneously active, not how many exist in memory.

## Voice Modes

### Mono

One voice at a time. A new note-on always retriggers:

1. If a voice is currently playing/releasing, it is immediately retriggered (SamplerVoice::noteOn resets position and envelopes, starting a fresh attack from zero)
2. The voice plays the new note, velocity, and buffer

This is the SP-1200 / Octatrack per-track behavior. Essential for bass lines, chopped beats, and any context where only one sound should play. The new note's attack ramp handles the transition from the old sound — no explicit fade-out is needed because the retrigger resets the envelope to 0.

### Poly (Phase 2)

Multiple simultaneous voices up to `maxActiveVoices`:

1. On note-on, find an idle voice and trigger it
2. If no idle voice is available, apply the steal policy (see below)
3. On note-off, release voices in `playing` state that match the note number (voices already in `releasing` state are not affected)

Note-off matches by MIDI note number against voices in `playing` state only. Voices already in `releasing` state have already received their note-off and are ignored.

### Legato (Phase 2)

Like mono but does not retrigger the envelope on overlapping notes:

1. If a voice is in sustain and a new note arrives before note-off, the voice changes pitch to the new note without retriggering envelopes. The read position continues from where it was.
2. If no voice is active, behaves like mono (full retrigger)
3. On note-off, if another note is still held (note stack), glide to that note. If no notes are held, enter release.

Legato requires a note stack — a small fixed-size list of currently held notes (most recent on top). Max stack depth = 16.

## Voice Stealing (Phase 2)

When poly mode has no idle voices and a new note-on arrives:

### Oldest policy

Steal the voice with the greatest age (`voice.getAge()`). This is the most common policy — it prioritizes the most recent musical events.

### Quietest policy

Steal the voice with the lowest current amplitude envelope level (`voice.getEnvelopeLevel()`). This minimizes audible artifacts — a voice that's nearly silent is less noticeable when cut.

### Steal procedure

1. Select victim voice according to policy
2. The victim voice is immediately retriggered (same as mono retrigger — SamplerVoice::noteOn resets everything)

A refinement for Phase 2+: instead of hard retrigger, the victim could be given a very short fade-out (e.g., 5ms). This requires a "stealing" sub-state on the voice. Deferred — hard retrigger is acceptable for Phase 1–2.

## Choke Groups (Phase 2)

A choke group is an integer tag assigned to MIDI note numbers. When a note in choke group G is triggered, all currently playing voices in group G are immediately released (enter release stage, not hard cut).

Classic use: open hihat (note 46) and closed hihat (note 42) in the same choke group — playing closed hihat cuts the open hihat's tail.

### Interface (Phase 2)

```cpp
void setChokeGroup(int midiNote, int group);   // 0 = no group
void clearChokeGroups();
```

Choke group mappings are stored in a fixed-size array: `std::array<int, 128> chokeGroups` (one entry per MIDI note, default 0 = no group).

## Rendering

### Sub-block rendering

SamplerNode splits each audio block at MIDI event boundaries and calls VoiceAllocator methods between segments. `noteOn` and `noteOff` take effect immediately — they do not take sample offset parameters. Sample-accurate timing is achieved by SamplerNode calling them at the right point between `renderBlock` segments:

```
Block of 512 samples with events at offsets 100 and 300:

1. renderBlock(output, 0, 100)        // render samples 0–99
2. noteOn(buffer, 60, 100)            // trigger (takes effect now)
3. renderBlock(output, 100, 200)      // render samples 100–299 (voice is playing)
4. noteOff(60)                        // release (takes effect now)
5. renderBlock(output, 300, 212)      // render samples 300–511 (voice is releasing)
```

VoiceAllocator passes `startSampleInBlock = 0` to SamplerVoice for all noteOn/noteOff calls, since the sub-block splitting has already placed the event at the correct boundary.

### Summing

`renderBlock()` iterates all voices and calls `voice.render()` for each non-idle voice. Since `SamplerVoice::render()` is additive, multiple voices naturally sum into the output buffer. The caller (SamplerNode) is responsible for clearing the output buffer before the first render call in a block.

```cpp
void VoiceAllocator::renderBlock(AudioBuffer<float>& output,
                                  int startSample, int numSamples) {
    for (auto& voice : voices_) {
        if (voice.getState() != VoiceState::idle) {
            voice.render(output, startSample, numSamples);
        }
    }
}
```

## Invariants

- All methods called on the audio thread (`noteOn`, `noteOff`, `allNotesOff`, `renderBlock`) are RT-safe: no allocation, no blocking
- Voice pool size is fixed after construction
- In mono mode, at most one voice is in `playing` state (releasing voices may overlap during transitions)
- `noteOff` only affects voices in `playing` state — voices already in `releasing` state are not matched
- `noteOff` for a note that has no matching `playing` voice is a no-op
- `allNotesOff` releases all non-idle voices (enters release stage, does not hard-cut)
- `renderBlock` is additive — it adds to the output buffer
- The buffer pointer passed to `noteOn` must remain valid until all voices using it return to idle
- `setMode`, `setStealPolicy`, `setMaxActiveVoices` are control-thread operations; they take effect on the next note-on, not retroactively on playing voices

## Error Conditions

- `noteOn` with null buffer: ignored, no voice triggered
- `noteOn` with `midiNote` outside 0–127: ignored
- `noteOff` with no matching playing voice: no-op
- `maxVoices < 1` in constructor: clamped to 1
- `setMaxActiveVoices(n)` where n > pool size: clamped to pool size
- `setMaxActiveVoices(n)` where n < current active count: excess voices are not killed, but no new voices are allocated until count drops below n

## Does NOT Handle

- MIDI parsing (SamplerNode extracts note-on/off from MidiBuffer)
- Parameter management (SamplerNode owns SamplerParams)
- Buffer lifecycle (Engine owns Buffers; SamplerNode resolves buffer ID → pointer)
- Slice-to-note mapping (SamplerNode maps MIDI notes to slices in Phase 2)
- Sub-block event splitting (SamplerNode segments the block; VoiceAllocator receives pre-split calls)
- Audio output routing (SamplerNode writes VoiceAllocator's output to ProcessContext)

## Dependencies

- `SamplerVoice` — voice instances
- `SamplerParams` — shared parameter struct (read-only reference)
- `Buffer` — passed through to voices on noteOn
- `juce::AudioBuffer<float>` — output buffer

## Thread Safety

- **Constructor**: control thread
- `prepare()`, `release()`: control thread
- `setMode()`, `setStealPolicy()`, `setMaxActiveVoices()`: control thread. These write to fields that the audio thread reads. The values are atomic-sized scalars; a one-block delay in visibility is acceptable.
- `noteOn()`, `noteOff()`, `allNotesOff()`, `renderBlock()`: audio thread only
- `getMode()`, `getMaxVoices()`, `getActiveVoiceCount()`: safe from any thread (reads of atomic-sized scalars)

## Example Usage

```cpp
SamplerParams params;
VoiceAllocator allocator(1, params);  // mono, 1 voice
allocator.setMode(VoiceMode::mono);
allocator.prepare(44100.0, 512);

// In SamplerNode::process(), after sub-block splitting:
Buffer* buf = engine.getBuffer(bufferId);

allocator.noteOn(buf, 60, 100);              // trigger
allocator.renderBlock(output, 0, 256);       // render first half

allocator.noteOff(60);                       // release
allocator.renderBlock(output, 256, 256);     // render second half (voice in release)
```
