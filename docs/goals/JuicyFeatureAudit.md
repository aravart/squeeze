# Juicy Feature Audit

An audit of features in the juicy codebase that would be valuable to have in squeeze. PluginScanner, OSC, Recorder, and Transport are excluded (already known).

---

## Large Features

### Bus/Send/Return System

Juicy has a full aux bus architecture: channels have sends (with per-send level, pre/post fader, enable), buses accumulate audio from senders, and "return channels" read from buses via a `BusInput` instrument. The `AudioCallback` does a topological sort (Kahn's algorithm) to ensure senders process before receivers. This enables reverb sends, parallel compression, group buses — none of which squeeze can do currently.

### Channel Strip DSP

Each channel has built-in input trim, a high-pass filter (`StateVariableTPTFilter`), and a 3-band EQ (low shelf, parametric mid, high shelf) with separate L/R filter instances and coefficient-update throttling. Applied between instrument and insert chain.

### Insert Chain with Lock-Free Operations

An 8-slot effects chain per channel. Uses a `LockFreeQueue<PendingOperation>` for message-to-audio thread handoff (Load/Remove/Clear ops) and a separate queue for deferred plugin deletion. Per-slot bypass. Squeeze's PluginNode is a single plugin, not a chain.

### Sampler

A 32-voice polyphonic sampler with: ADSR envelopes, per-voice filter (LP/HP/BP with its own ADSR + key tracking + velocity modulation), choke groups, one-shot mode, root note pitch shifting, hermite cubic interpolation, voice stealing. 16 automatable parameters. Far beyond squeeze's Buffer.

### ClipPlayer

Transport-synced audio clip playback. Two activation modes (immediate one-shot / scheduled beat-synced looping). Four tempo modes: raw, fixed tempo, DJ mode (pitch shift), time-stretch (OLA with Hann window). `playToEnd()` converts looping to one-shot at current position. Progress reporting.

### HostPlayHead

Full `juce::AudioPlayHead` implementation providing tempo, PPQ position, time signature, bar start, and playing/recording state to hosted plugins. Essential for any tempo-synced plugin (delays, arpeggiators, LFOs).

---

## Medium Features

### ScopeBuffer (Shared Memory Oscilloscope)

POSIX `shm_open`/`mmap` ring buffer (8192 samples/channel) with lock-free writes from the audio thread. Named per-channel and master. External processes can open the same region to render waveforms — designed for a web-based scope UI.

### StateBroadcaster

Background thread at 30Hz that serializes engine state to JSON: CPU load, xrun count, sample rate, buffer size, per-channel peaks, scope buffer names, clip progress. Ready-made protocol for building a remote UI.

### MIDI Router with Three-Tier Priority

Device-to-channel routes, MIDI channel-to-channel routes, and an "armed channel" fallback. More flexible than squeeze's direct MIDI node wiring.

### Performance Metrics

`AudioCallback` measures execution time with `steady_clock`, computes load ratio (callback time / buffer time), detects xruns when load > 1.0. Exposed as atomics.

---

## Small Quality-of-Life

### Channel Soft-Delete with Fade-Out

`startFadeOut(ms)` applies a per-sample linear ramp, then marks the channel as deleted. Audio thread skips deleted channels. Memory is deferred (never freed during session in juicy's case). Prevents glitches on removal.

### Pre-Fader Buffer Capture

After the insert chain, the channel copies audio to `preFaderBuffer` so pre-fader sends can tap the signal before gain/pan. Simple but necessary for a real mixer.

### MIDI ScopedTryLock Pattern

Channel's `process()` uses `ScopedTryLock` to swap MIDI buffers. If contended, processes with empty MIDI rather than blocking the audio thread. Pragmatic RT safety.

### Atomic Processing-Order Dirty Flag

`processingOrderDirty` is an `atomic<bool>` checked with `exchange(false)` — only rebuilds the topo sort when the graph actually changed. Cheap per-block check.

### Cycle Detection Fallback

If the topological sort doesn't include all channels (cycle detected), logs a warning and appends remaining channels anyway to avoid silence.

### ListParams Standalone Tool

A separate JUCE app that loads a plugin and dumps all its parameters, with `--json` output. Useful for scripting and debugging.

---

## Priority Assessment

The biggest gaps relative to juicy are the **bus/send architecture**, **channel strip DSP**, **insert chains**, and **HostPlayHead**. These are what turn a graph-of-nodes engine into something that feels like a mixer/DAW. The sampler and clip player are large standalone features that depend on project direction.
