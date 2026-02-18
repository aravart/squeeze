What a tracker needs

A tracker sequences note/effect commands on a time grid, triggers samples or synths, mixes channels, and
renders audio. The engine requirements break into tiers:

Tier 1 — Can't do anything without these

Need: Transport
Status: Stubbed — API complete, logic is no-op
Notes: Play/stop, tempo, position tracking, beat advancement. The entire timing backbone.
────────────────────────────────────────
Need: EventScheduler
Status: Stubbed — API complete, always returns false
Notes: Schedule note-on/off/CC/param at beat positions. This is the sequencer from the engine's perspective.

These two are the critical path. The API surface already exists end-to-end (C++, FFI, Python), but the audio
  thread does nothing with them. Until Transport advances beats and EventScheduler resolves beat-timed events
  to sample offsets, there is no playback.

Tier 2 — Need for a sample-based tracker

Need: BufferLibrary
Status: Spec only
Notes: Load WAV/AIFF/FLAC into memory, assign to nodes by ID.
────────────────────────────────────────
Need: SamplerNode
Status: No spec, no code
Notes: Play buffers with pitch control, ADSR, multi-voice. The core instrument of a tracker.

A plugin-only tracker (triggering VSTi instruments) could skip these — the existing PluginNode +
EventScheduler would suffice. But a traditional tracker is sample-centric.

Tier 3 — Need for production use

Need: Offline render / bounce
Status: Nothing
Notes: Process graph faster-than-realtime to a buffer or file. Essential for export. render() exists but
  processes a single block — no file output, no multi-block batch.
────────────────────────────────────────
Need: Mixer primitives
Status: Partial
Notes: GainNode exists. No pan, mute, or solo. A tracker with per-track volume/pan needs these.
────────────────────────────────────────
Need: State persistence
Status: Nothing
Notes: Save/restore graph topology, connections, plugin state, parameter values. Without this, every session

  is ephemeral.

Tier 4 — Nice to have for a good tracker

┌────────────────┬─────────┬───────────────────────────────────────────────────────────────────────────────┐
│      Need      │ Status  │                                     Notes                                     │
├────────────────┼─────────┼───────────────────────────────────────────────────────────────────────────────┤
│ GroupNode      │ Spec    │ Channel strips, sub-mixes, effects racks. Not blocking, but makes complex     │
│                │ only    │ routing manageable.                                                           │
├────────────────┼─────────┼───────────────────────────────────────────────────────────────────────────────┤
│ PDC            │ Spec    │ Matters once you have parallel effect chains. Not day-one critical.           │
│                │ only    │                                                                               │
├────────────────┼─────────┼───────────────────────────────────────────────────────────────────────────────┤
│ Automation     │         │ EventScheduler does point events; real automation needs interpolated          │
│ curves         │ Nothing │ envelopes. Trackers often use per-row effect commands instead, which          │
│                │         │ EventScheduler can handle.                                                    │
├────────────────┼─────────┼───────────────────────────────────────────────────────────────────────────────┤
│ RecordingNode  │ Spec    │ Record audio input. Secondary for a tracker.                                  │
│                │ only    │                                                                               │
└────────────────┴─────────┴───────────────────────────────────────────────────────────────────────────────┘

Shortest path to a working tracker

The minimal sequence, respecting dependency order:

1. Transport — implement beat advancement, tempo, loop, seek in the audio thread
2. EventScheduler — resolve beat-timed events to sample offsets, dispatch in processBlock
3. At this point: plugin-based tracker works (trigger VSTi instruments from Python on a beat grid)
4. BufferLibrary — load samples into memory
5. SamplerNode — play samples with pitch/voice management
6. At this point: sample-based tracker works
7. Offline render, state persistence, mixer primitives for production quality

Transport and EventScheduler are the bottleneck — everything else builds on top of working beat-accurate
playback.
