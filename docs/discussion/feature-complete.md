
  Must-Have

  - Latency compensation (PDC) — Plugins report latency; parallel signal paths need automatic delay
   alignment. Without this, phase coherence breaks on any non-trivial graph.
  - Offline render / bounce — Process the graph faster-than-realtime to a buffer or file. Essential
   for export, freeze, and testing.
  - Audio file I/O — Read/write WAV, AIFF, FLAC, etc. The BufferLibrary plans cover loading into
  memory, but you also need format decoding/encoding and potentially disk streaming for long files.
  - State persistence — Save/restore entire sessions: graph topology, connections, plugin state
  (opaque chunks), parameter values. Also plugin preset save/load.
  - Automation curves — The EventScheduler handles point events, but real automation needs
  envelopes with interpolation modes (linear, curve, step) evaluated per-sample across a time
  range.
  - Mixer primitives — Volume, pan, mute, solo per node. Mute/solo have interaction semantics
  (solo-in-place, solo-exclusive) that affect graph traversal.

  Important

  - Send/return routing — Aux buses for shared effects (reverb, delay). Probably a graph-level
  concept: a send tap node and a return injection point with wet/dry.
  - Tempo map — Variable tempo over time, time signature changes, tempo automation. Transport
  currently assumes a single BPM — real projects need a tempo curve.
  - Plugin crash isolation — A bad plugin shouldn't take down the engine. Options: out-of-process
  hosting, or at minimum exception guards + bypass-on-crash.
  - Parallel graph processing — Independent branches processed on a thread pool. Single-threaded
  processBlock won't scale to large graphs on modern hardware.

  Nice-to-Have

  - Undo/redo — Command-pattern stack for all graph mutations. Could live in the Python layer or in
   the engine.
  - MIDI learn / CC mapping — Bind MIDI CCs to plugin parameters generically.
  - Disk streaming — For long audio files that don't fit in RAM. Double-buffered background reads.
  - Multi-channel beyond stereo — Surround, Ambisonics, Atmos bus formats.
  - Oversampling — Per-node upsampling for nonlinear plugins that alias.

  Already Covered (just confirming)

  Plugin scanning/caching (PluginManager), metering (ScopeTap planned), MIDI routing/splitting
  (planned), recording (planned).

  ---
  The biggest architectural decisions are around PDC (it affects graph traversal order) and
  parallel processing (it affects the entire render pipeline). Worth speccing those early even if
  implementation comes later, since they can force structural changes if bolted on after the fact.
