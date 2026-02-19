
  Solo. You have gain, pan, bypass on sources and buses. But no solo. No solo-in-place, no solo-exclusive,
   no solo-safe. A mixer without solo isn't a mixer.

  State serialization. There's no way to save or restore anything. No plugin state
  (getStateInformation/setStateInformation), no mixer snapshot to disk, no session save/load. Every engine
   instance is ephemeral — close it and everything is gone.

  Offline bounce / export. render() exists for tests, but there's no "render the output to a WAV file"
  path. No faster-than-realtime export. No way to produce a final audio file.

  Audio input. AudioDevice plays engine output to hardware, but there's no path for hardware inputs into
  the engine. No AudioInputProcessor or live input source. You can't record, you can't process a mic or
  guitar, you can't do live input monitoring.

  Automation lanes. EventScheduler fires one-shot events at beat positions. But real automation is curves
  — ramps, S-curves, step, hold — that continuously control parameters from beat A to beat B. The
  difference between "set volume to -6dB at bar 5" and "fade volume from 0dB to -6dB across bars 5-8."

  Tempo maps. Transport holds a single tempo value. Real music has tempo changes, accelerandos,
  ritardandos, time signature changes at specific bar positions.

  Multi-output plugins. Drum machines, orchestral libraries, and many synths expose multiple stereo
  outputs. PluginProcessor appears to handle a single stereo pair. Without multi-out support you can't
  separately mix a kick vs snare from a drum plugin.

  Plugin scanning. PluginManager loads a pre-built cache, but there's no scanning process. No way to
  discover what plugins are installed, validate them, handle crashy plugins during scan (out-of-process
  scanning). Someone has to build that cache.

  Plugin crash isolation. When a plugin segfaults in processBlock, what happens? Right now — the engine
  dies. Production engines need to catch this and bypass the offending plugin rather than taking down the
  whole audio thread.

  VCA groups / channel linking. No way to link faders, gang mutes, or create mix groups. Essential once
  you're past ~8 channels.

  Audio file writing. BufferLibrary covers reading audio files. Nothing covers writing them. Needed for
  bounce, recording, freeze.

  Freeze. Render a source's chain to audio offline, then play back the flat file instead. Standard CPU
  management technique.

❯ how do you even do plugin crash isolation on the audio thread?

⏺ Honestly — you mostly don't. Not reliably.

  The core problem: A segfault in processBlock is a signal (SIGSEGV), not a C++ exception. You can't
  try/catch it. And you're on the audio thread, so you can't add latency or block.

  The approaches that exist:

  Signal handler + longjmp — Install a SIGSEGV/SIGBUS handler, setjmp before calling the plugin's
  processBlock, and longjmp back if it faults. Then mark the plugin bypassed and zero its output buffer.
  Some DAWs do this. It's awful because:
  - If the plugin corrupted the heap or stack before faulting, you're already dead — longjmp just delays
  the crash
  - Async-signal-safety rules mean you can barely do anything in the handler
  - It's technically undefined behavior on POSIX
  - On Windows, __try/__except (SEH) is less terrible but still can't save you from memory corruption

  Watchdog thread — A timer thread checks whether the audio callback returned within its deadline. Catches
   hangs (infinite loops, deadlocks) but not crashes. On the next callback you skip the hung plugin.
  Useful but orthogonal to crash isolation.

  Out-of-process hosting — The real answer. Each plugin (or group) runs in a separate process. Audio
  passes through shared memory ring buffers, control via IPC. If the process dies, the host is fine —
  bypass it, show an error, optionally restart. This is what Bitwig does. It works. It's also a massive
  amount of engineering: shared memory buffer management, IPC for parameter/state/GUI/MIDI, process
  lifecycle, and JUCE gives you zero help because AudioProcessor assumes in-process hosting.

  What most DAWs actually do: Run plugins in-process, accept the risk during playback, and isolate only
  during scanning (scan in a child process so a crashy plugin doesn't kill the host). If a plugin crashes
  during playback in Logic, Ableton, or Reaper — the engine goes down.

  For Squeeze, the pragmatic call is: out-of-process scanning (when you build the scanner), in-process
  hosting, and a crashing plugin crashes the engine. That's the industry norm. Out-of-process hosting is a
   feature you'd add much later if ever — it's practically its own project.
