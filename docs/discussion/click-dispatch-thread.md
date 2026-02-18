❯ Let's talk About sequencing on the python side. I think there should be a tempo clock
  with frequency demarcated in beats and optional latency in milliseconds. What do you
  think of that?

⏺ That's a solid foundation. A few thoughts:

  Beat-based frequency is the right unit. Musical sequencing thinks in beats, not
  milliseconds. A clock that fires every 0.25 beats (sixteenth notes at whatever tempo) is
  much more natural than converting to wall-clock time yourself. The engine/transport already
   owns tempo, so the clock just needs to know "how often in beats."

  The latency parameter is smart — it's really a lookahead buffer. The Python side runs on
  the control thread, which is inherently jittery compared to the audio thread. If the clock
  fires with, say, 50ms of lookahead, your Python callback has time to compute and push
  events into the engine before they actually need to render. Without it, you're always
  racing the audio thread.

  A few design questions worth nailing down:

  1. What drives the clock? Two common approaches:
    - Engine-driven: the audio thread posts "we just rendered up to beat X" back to Python,
  and the clock fires based on that. Tight coupling to actual playback position, but requires
   a callback mechanism from C++ to Python.
    - Python-driven: a Python thread polls transport position and fires when the next beat
  boundary is crossed. Simpler, but depends on polling frequency and introduces its own
  jitter (which the latency parameter mitigates).
  2. Relationship to Transport: The transport already tracks tempo and position. Is the clock
   a layer on top of Transport (reads position, fires callbacks), or does it extend Transport
   with scheduling capabilities?
  3. What does the callback receive? Presumably at minimum the current beat position, but
  maybe also the time window — "you're responsible for scheduling events between beat 4.0 and
   beat 4.25" — which makes the lookahead explicit and lets you batch-schedule everything in
  that window.
  4. Multiple clocks at different rates? One clock at 1/16th resolution firing a single
  callback, or multiple clocks (one at 1/4 for chord changes, one at 1/16 for arpeggiation)?
  The former is simpler, the latter is ergonomic.

  My instinct is that engine-driven with a time-window callback is the most robust pattern —
  it's essentially how JUCE's AudioPlayHead works, and what SuperCollider and Web Audio's
  scheduling models converge on. The Python clock would receive "fill the window from beat X
  to beat X+delta" and push events into the engine's event queue with sample-accurate
  timestamps.

  Want to sketch a spec for this, or hash out more of the design first?

✻ Brewed for 31s

❯ Engine-driven, somehow. The clock is a layer on top of transport. The callback just
  recieves beats at the frequency/resolution it requested (e.g., 1/4 a beat, 1/8 a
  beat). It can manage how far ahead it wants to schedule based on that. I think there
  could be multiple clocks and perhaps later on even clocks that fire based on wall-time
   frequency rather than beat frequency. Let's hash out the design first. How would you
  implement this beat-based callback/clock on the C++ side?

⏺ Good question. Here's how I'd approach it:

  Core idea: boundary detection + lock-free queue

  The audio thread already knows "this block advanced from beat X to beat Y" (from
  Transport). The clock's job on the audio thread is trivial — detect which beat boundaries
  were crossed, push them to a queue. Everything else happens on the control thread.

  The audio-thread side

  struct ClockTick {
      double beat;          // exact beat position (e.g., 4.0, 4.25, 4.5...)
  };

  class Clock {
      double resolution_;               // e.g., 0.25 = sixteenth notes
      SPSCQueue<ClockTick> ticks_;      // lock-free, audio → control

      // Called by engine each block
      void process(double oldBeat, double newBeat) {
          if (newBeat <= oldBeat) return;  // stopped or no advance

          int64_t oldSlot = (int64_t)floor(oldBeat / resolution_);
          int64_t newSlot = (int64_t)floor(newBeat / resolution_);

          for (int64_t t = oldSlot + 1; t <= newSlot; t++) {
              ticks_.push({ t * resolution_ });
          }
      }
  };

  That's the entire hot path. Integer division, a loop that almost always runs 0 or 1 times,
  and a lock-free push. Fully RT-safe.

  A ClockManager holds the clocks and is called by the engine after advancing transport:

  class ClockManager {
      // snapshot-swapped like the rest of the architecture
      std::vector<Clock*> clocks_;

      void process(double oldBeat, double newBeat) {
          for (auto* c : clocks_)
              c->process(oldBeat, newBeat);
      }
  };

  Getting ticks out: poll, don't callback

  The simplest FFI surface is poll-based:

  // Register a clock, get a handle
  SqClock sq_clock_create(SqEngine engine, double resolution_beats);
  void    sq_clock_destroy(SqClock clock);

  // Drain pending ticks — returns count, fills out_beats
  int sq_clock_poll(SqClock clock, double* out_beats, int max_count);

  Python runs a thread that polls. No C-to-Python callback mechanism needed, no GIL concerns,
   no callback registration in the FFI. The Python Clock class just spins in a thread calling
   sq_clock_poll, and when it gets ticks, it calls the user's Python callback.

  Key edge cases to think about

  Tempo changes mid-block. If Transport handles tempo ramps internally and just reports "we
  went from beat 3.8 to beat 4.1 this block," the clock doesn't care how we got there. It
  just sees the range. This keeps the clock simple.

  Transport looping. If the transport loops from beat 16 back to beat 1, that's two ranges in
   one block: [oldBeat, loopEnd] and [loopStart, newBeat]. Transport would need to report
  both, or the engine calls ClockManager::process twice per block for the two segments.
  Otherwise you'd miss ticks or fire spurious ones.

  High tempo + large block size. At 300 BPM with 2048-sample blocks (~46ms), a sixteenth-note
   clock could have 3-4 ticks per block. The loop handles that naturally, but the SPSC queue
  needs enough capacity.

  Transport stopped. Beat position doesn't advance → newBeat <= oldBeat → no ticks. Clean.

  What this doesn't include (intentionally)

  - Sample-accurate tick positions (which sample within the block the boundary fell on).
  Could add later if needed for tighter scheduling, but beat position alone lets Python
  schedule events "at beat N" and the engine resolves to samples.
  - Wall-time clocks. Different driver (real clock instead of transport), but same Clock
  interface and same poll mechanism. Slots in cleanly later.
  - Swing/groove. Would offset the tick positions before pushing. Could be a later addition
  to Clock itself.

  The main open question I see: does the polling rate on the Python side concern you, or is
  the latency parameter enough to absorb that jitter? At, say, 10ms poll intervals with 50ms
  lookahead, you'd have plenty of margin. But I'm curious if you had something else in mind
  for how the Python thread consumes ticks.

✻ Cogitated for 1m 8s

❯ Would it be better performance to have a clock on the JUCE side call Python (again not
   from audio or even from control thread)?

⏺ Yes, it's strictly better. Polling has an inherent tradeoff — either you waste CPU spinning
   tight, or you sleep and add up to one poll-interval of latency. A signal-driven dispatch
  thread eliminates both.

  The shape of it

  Audio thread                Dispatch thread              Python
  ───────────                 ───────────────              ──────
  push tick to SPSC queue ──→ sem_wait() wakes up    ──→  callback(beat)
  sem_post()                  drain queue
                              call function pointer

  Three threads, each with a clean role:

  - Audio thread: push ticks + sem_post(). Both are RT-safe (lock-free queue write, semaphore
   post is async-signal-safe).
  - Dispatch thread: sem_wait(), drain queue, invoke the registered C function pointer. This
  thread is neither audio nor control — it exists only for clock delivery. Blocked when idle,
   zero CPU cost.
  - Python: the ctypes callback fires, GIL is acquired automatically by ctypes, user code
  runs.

  Why it's better than polling

  - Latency: semaphore wake is typically <1ms vs. up to one full poll interval (10ms if
  you're polling at 100Hz).
  - CPU: blocked on sem_wait() = zero CPU when no ticks are pending. Polling always costs
  something.
  - Python side is simpler: no thread to manage, no poll loop, no sleep interval to tune.
  Just register a callback.

  FFI surface

  typedef void (*SqClockCallback)(SqClock clock, double beat, void* userData);

  SqClock sq_clock_create(SqEngine engine, double resolution_beats,
                           SqClockCallback callback, void* userData);
  void    sq_clock_destroy(SqClock clock);

  That's actually a smaller API than the poll approach — no sq_clock_poll at all.
  Registration is the interface.

  The one thing to be careful about

  Callback lifetime. If Python destroys a clock while the dispatch thread is mid-callback,
  that's a problem. The sq_clock_destroy implementation needs to:

  1. Remove the clock from the manager (so audio thread stops producing ticks)
  2. Join or synchronize with the dispatch thread to ensure no in-flight callback
  3. Then free

  Standard teardown discipline, nothing exotic. Each clock could own its own dispatch thread
  (simple, isolated), or they could share one dispatch thread with a small router (more
  efficient if there are many clocks). I'd start with one thread per clock and consolidate
  later if it matters.

  Does this match what you were thinking? The main design fork from here is the Python-side
  ergonomics — what the Clock class looks like to the user.

✻ Worked for 43s

❯ So I was thinking that the tick dispatch thread lives whether or not Python (or any
  other client) has registered a clock / requested callbacks). The thought here is that
  the audio thread pushes ticks, the tick dispatch thread drains them. And if there are
  any registered clocks (which Python clocks), then those clocks get callbacks at the
  appropriate time with the appropriate latency and the appropriate resolution, etc.

⏺ Ah, that's a cleaner split. Let me make sure I have it right:

  Audio thread does even less now — it just pushes the beat range each block, once,
  regardless of how many clocks exist:

  // Audio thread, every block:
  dispatchQueue_.push({ oldBeat, newBeat });
  sem_post(&dispatchSem_);

  Dispatch thread is an engine component, always alive. It owns all the boundary detection
  logic and clock routing:

  Engine starts → dispatch thread starts (blocks on semaphore)

  Audio thread posts {oldBeat, newBeat} each block
      → dispatch thread wakes
      → drains queue
      → for each registered clock:
          does this range cross a boundary at this clock's resolution?
          if yes → fire callback with the beat position
      → back to sem_wait()

  Clocks are just subscriptions on the dispatch side. Python registers one, gets callbacks.
  No clocks registered? Dispatch thread still wakes, drains the queue, does nothing, goes
  back to sleep. Cheap.

  This is better than what I had for a few reasons:

  - Audio thread is O(1) — one push per block, always. Doesn't know or care about clocks.
  - No snapshot swapping for the clock list — registration and iteration both happen on the
  dispatch thread (or are trivially synchronized since the dispatch thread is the only
  reader).
  - Boundary detection is off the audio thread. It's just integer math, but it still
  shouldn't be there if it doesn't have to be.

  The thing I want to make sure I understand: where does latency fit? Two possible
  interpretations:

  A. Latency is informational/advisory — the dispatch thread fires immediately, and the
  latency value tells the Python user "you have this many ms before the beat actually hits
  the speakers, schedule accordingly." Simple, but the user has to trust and manage the
  margin themselves.

  B. Latency is functional — the dispatch thread actively manages timing. A clock with 50ms
  latency gets its callback fired 50ms before playback. A clock with 100ms latency gets it
  100ms before. The dispatch thread would need to know the device output latency to calculate
   wall-clock fire times, and might hold or advance callbacks accordingly.

  Which of these were you thinking? (A) keeps the dispatch thread simple, (B) gives the user
  a more precise contract.

✻ Brewed for 44s

❯ (B). The dispatch thread knows the current tempo and the latency requirement (in ms)
  of each subscription. So when the audio thread says beat [0.491, 0.494) happened (or
  whatever) and a clock which wants to be notified at beat 0.5 because of resolution 1/4
   and latency in ms that is appropriate, it can then send the callback telling the
  client that 0.5 is happening. The client can at that point schedule on the control
  thread for events that should happen at beat 0.5. Complexity is on the dispatcher side
   but not in the audio thread or in the user code of the Python callback.

⏺ That clicks. The dispatch thread is the smart layer — it looks ahead based on tempo and
  each clock's latency to fire callbacks early enough that the client can schedule in time.
  The audio thread and the Python callback are both dumb and happy.

  Let me make sure I have the mechanics right:

  Dispatch thread logic

  Each clock subscription tracks its next pending boundary:

  struct ClockSubscription {
      double resolution;       // e.g., 0.25 beats
      double latencyMs;        // lookahead window
      double nextBoundary;     // next beat to fire (advances after each callback)
      SqClockCallback cb;
      void* userData;
  };

  When the audio thread posts {oldBeat, newBeat}:

  double beatsPerMs = currentTempo / 60000.0;

  for (auto& clock : subscriptions_) {
      // How far ahead can this clock see, in beats?
      double horizon = newBeat + (clock.latencyMs * beatsPerMs);

      // Fire all boundaries that have entered the window
      while (clock.nextBoundary <= horizon) {
          clock.cb(clock.id, clock.nextBoundary, clock.userData);
          clock.nextBoundary += clock.resolution;
      }
  }

  So with tempo 120 BPM, a 1/4 beat resolution clock with 50ms latency:

  beatsPerMs = 0.002
  lookahead  = 50 * 0.002 = 0.1 beats

  Audio posts [0.391, 0.394)  → horizon = 0.494  → 0.5 not reached, no fire
  Audio posts [0.394, 0.397)  → horizon = 0.497  → still no
  Audio posts [0.397, 0.400)  → horizon = 0.500  → fire! callback(0.5)
  ...50ms later audio thread actually renders beat 0.5,
     events are already queued and waiting

  What each layer sees

  Layer: Audio thread
  Responsibility: Push {oldBeat, newBeat} once per block
  Complexity: Near zero
  ────────────────────────────────────────
  Layer: Dispatch thread
  Responsibility: Lookahead math, boundary detection, callback routing
  Complexity: All of it
  ────────────────────────────────────────
  Layer: Python callback
  Responsibility: Receives beat=0.5, schedules events at beat 0.5
  Complexity: None — just "do stuff at this beat"

  The Python user writes something like:

  def on_beat(beat):
      source.midi_note_on(beat=beat, note=60, vel=100)

  clock = engine.clock(resolution=1/4, latency_ms=50, callback=on_beat)

  They never think about lookahead, jitter, or timing. They just get told a beat and schedule
   at that beat.

  A few things that fall out of this design

  Latency = 0 is valid. Horizon = newBeat, so you only get notified of boundaries the audio
  thread has already crossed. Late notification, but still useful for visualization/UI sync.

  Tempo changes work automatically. The horizon recalculates each block with the current
  tempo. If tempo doubles, the lookahead window (in beats) doubles, so boundaries enter the
  window sooner. The callback just fires earlier — the client doesn't care why.

  Clock with huge latency fires boundaries far in advance. A 500ms lookahead at 120 BPM sees
  a full beat ahead. That's fine — the client just gets more warning.

  Does this match what you had in mind? If so, I think the next question is the FFI surface
  and the Python Clock class ergonomics.

✻ Worked for 55s

❯ Yes, why do we need nextBoundary? The dispatcher just needs to know if the current
  beat interval, if advanced by the latency (changed to beats) cross a (fractional) beat
   where the clock wants notification. Also the clock dispatch thread needs to be loop
  aware.

⏺ You're right on both counts.

  No nextBoundary — just shift the window

  The beat range [oldBeat, newBeat) already advances monotonically. Shift it forward by the
  latency (converted to beats) and check for boundary crossings in the shifted window:

  double latencyBeats = clock.latencyMs * (tempo / 60000.0);
  double windowStart = oldBeat + latencyBeats;
  double windowEnd   = newBeat + latencyBeats;

  int64_t startSlot = (int64_t)floor(windowStart / clock.resolution);
  int64_t endSlot   = (int64_t)floor(windowEnd / clock.resolution);

  for (int64_t t = startSlot + 1; t <= endSlot; t++) {
      clock.cb(clock.id, t * clock.resolution, clock.userData);
  }

  Stateless. Each beat range is processed exactly once, the shifted windows don't overlap
  because [oldBeat, newBeat) doesn't overlap between consecutive blocks. No bookkeeping, no
  drift risk.

  Loop awareness

  This is where it gets interesting. When transport loops from beat 16 back to beat 0, the
  monotonic assumption breaks. Two problems:

  1. The beat range itself wraps. The audio thread renders the tail end of the loop and the
  beginning in one block. It should post two ranges:

  {oldBeat: 15.97, newBeat: 16.0}   // up to loop end
  {oldBeat: 0.0,   newBeat: 0.03}   // from loop start

  Dispatch thread processes them in order, boundary detection works normally on each.

  2. The lookahead window can see past the loop point. We're at beat 15.9, lookahead is 0.2
  beats, so the horizon is 16.1 — which doesn't exist. It should wrap to 0.1. So boundaries
  at both 16.0 (if the loop end is a boundary) and 0.0 need to fire.

  So the dispatch thread needs to know the loop bounds. When the shifted window overshoots
  loopEnd, it wraps:

  // If looping and window crosses loop end:
  if (looping && windowEnd > loopEnd) {
      // Fire boundaries in [windowStart, loopEnd]
      fireBoundaries(windowStart, loopEnd, clock);
      // Wrap and fire boundaries in [loopStart, remainder]
      double overflow = windowEnd - loopEnd;
      fireBoundaries(loopStart, loopStart + overflow, clock);
  } else {
      fireBoundaries(windowStart, windowEnd, clock);
  }

  3. What the audio thread posts to the dispatch thread. Needs to include loop context:

  struct TickUpdate {
      double oldBeat;
      double newBeat;
      double tempo;
      bool   looped;       // did we wrap this block?
      double loopStart;    // loop region bounds
      double loopEnd;
  };

  Or the dispatch thread reads loop state from transport directly, since it's not on the
  audio thread and can safely read transport config.

  The key insight is that the dispatch thread handles all the wrapping complexity — the audio
   thread just says what happened, the Python callback just gets clean beat numbers. If the
  loop is [0, 16) and the clock resolution is 1/4, the callback just sees ...15.75, 16.0,
  0.0, 0.25... — it doesn't care that a loop happened.

  Want to move toward speccing this out, or are there more design questions to settle first?

✻ Cooked for 44s

❯ the last issue is what happens at beats at 0 or near 0 for latency > 0. i think these
  have to actually run before the transport actually begins playback.

⏺ Right. If the clock has 50ms of lookahead and you hit play at beat 0, the audio thread is
  about to render beat 0 immediately — but the whole point of the latency was to give the
  client 50ms of warning. Beat 0 (and any other boundaries within the lookahead window) need
  their callbacks fired before the first audio block renders.

  Priming

  When transport starts playback at beat P, the dispatch thread pre-fires before the audio
  thread begins:

  // On transport start, before audio thread runs:
  double latencyBeats = clock.latencyMs * (tempo / 60000.0);

  // Fire all boundaries in [P, P + latencyBeats)
  fireBoundaries(P, P + latencyBeats, clock);

  At 120 BPM with 50ms latency, that's [0.0, 0.1) beats — so beat 0.0 fires immediately. The
  client schedules its events at beat 0, and by the time the audio thread renders its first
  block, those events are already queued.

  Then the first audio block posts [0.0, 0.003) and the shifted window is [0.1, 0.103) —
  picks up exactly where priming left off. No gap, no double-fire.

  Same issue applies to seek

  If the user seeks to beat 8 and hits play, priming covers [8.0, 8.0 + latencyBeats). Same
  logic.

  And to loop restarts during playback

  Actually — I think the wrap logic we already discussed handles this naturally during
  playback, since the audio thread is already running and the shifted window sees past the
  loop point. The priming case is specifically about playback starting from a stopped state,
  where there's no prior audio block to shift forward from.

  Sequencing

  The transport start sequence becomes:

  1. User hits play (control thread)
  2. Dispatch thread primes all clocks for [startBeat, startBeat + latencyBeats)
  3. Audio thread begins rendering
  4. Normal block-driven dispatch takes over

  The dispatch thread needs to know about the play event before the audio thread starts.
  Either transport notifies the dispatch thread first, or the engine orchestrates the
  ordering — prime, then start audio.

  This is the kind of complexity that's right to put on the dispatch thread. The audio thread
   doesn't know about it, the Python callback doesn't know about it. They both just see beats
   arriving in order.
