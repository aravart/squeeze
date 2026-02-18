# Clock Dispatch Specification

## Responsibilities

- Provide a beat-driven callback mechanism for external clients (Python, etc.) to receive notifications ahead of upcoming beat boundaries
- Own a dedicated **clock dispatch thread** that is neither the audio thread nor the control thread
- Receive beat range updates from the audio thread via a lock-free queue (one push per block, regardless of how many clocks are registered)
- Perform all beat boundary detection and lookahead math on the clock dispatch thread — never on the audio thread
- Route tick callbacks to registered clock subscriptions at the resolution and latency each subscription requested
- Handle transport loop boundaries: detect when the lookahead window wraps past a loop end and fire ticks in the wrapped region
- **Prime** clocks on transport start (and seek-then-play): fire all callbacks for beat boundaries within each clock's lookahead window *before* the audio thread begins rendering, so clients can schedule events for early beats in time
- Start the clock dispatch thread when the Engine starts; stop it when the Engine is destroyed. The thread runs whether or not any clock subscriptions exist

## Overview

The clock dispatch system decouples beat-timed notifications from the audio thread. The audio thread's only additional work is one lock-free queue push per block containing the beat range it just rendered. The clock dispatch thread wakes on that push, converts the beat range into tick callbacks for each registered subscription, and invokes client-provided function pointers. Clients (typically Python) schedule events back to the engine on the control thread from within their callbacks.

Latency is functional, not advisory. A clock subscription with 50ms latency receives its callback ~50ms before the audio thread actually renders that beat. The clock dispatch thread achieves this by shifting the audio thread's beat window forward by each subscription's latency (converted to beats at the current tempo) and detecting boundary crossings in the shifted window.

Callbacks may call `sq_schedule_*` functions directly to schedule events. This is safe because clock management and event scheduling use independent mutexes with no circular dependency (see Thread Safety).

## Key Concepts

### Beat Range Update

Each audio block, the audio thread pushes one update to the clock dispatch thread:

```cpp
struct BeatRangeUpdate {
    double oldBeat;      // transport position at block start
    double newBeat;      // transport position at block end
    double tempo;        // BPM during this block
    bool   looping;      // is looping enabled?
    double loopStart;    // loop region start (beats), valid when looping
    double loopEnd;      // loop region end (beats), valid when looping
};
```

When the transport loops mid-block, the audio thread pushes **two** updates for the two sub-blocks (pre-wrap and post-wrap), matching how Engine already splits `processSubBlock` at loop boundaries.

### Clock Subscription

A subscription defines how a client wants to receive ticks:

```cpp
struct ClockSubscription {
    uint32_t id;
    double   resolution;    // beat interval (e.g., 0.25 = sixteenth notes)
    double   latencyMs;     // lookahead in milliseconds (0 = notify after the fact)
    SqClockCallback callback;
    void*    userData;
};
```

### Tick Detection (stateless)

For each beat range update `[oldBeat, newBeat)` and each subscription, the clock dispatch thread:

1. Converts latency to beats: `latencyBeats = latencyMs * (tempo / 60000.0)`
2. Shifts the window: `windowStart = oldBeat + latencyBeats`, `windowEnd = newBeat + latencyBeats`
3. Detects boundary crossings in the shifted window:
   ```
   startSlot = floor(windowStart / resolution)
   endSlot   = floor(windowEnd / resolution)
   for t in (startSlot + 1) .. endSlot:
       fire callback with beat = t * resolution
   ```

This is stateless — each beat range is processed exactly once, and consecutive shifted windows don't overlap because `[oldBeat, newBeat)` advances monotonically within a single playback run.

### Loop-Aware Lookahead

When looping is enabled and the shifted window extends past `loopEnd`, the entire window must be wrapped into the loop region. Two cases:

**Partial wrap** — `windowStart < loopEnd` and `windowEnd > loopEnd`:

1. Fire boundaries in `[windowStart, loopEnd]`
2. Wrap the tail: `overflow = windowEnd - loopEnd`
3. Fire boundaries in `[loopStart, loopStart + overflow)`

**Full wrap** — `windowStart >= loopEnd` (high latency shifts the entire window past the loop point):

1. Wrap both endpoints into the loop region:
   ```
   loopLen       = loopEnd - loopStart
   wrappedStart  = loopStart + fmod(windowStart - loopEnd, loopLen)
   wrappedEnd    = loopStart + fmod(windowEnd   - loopEnd, loopLen)
   ```
2. If `wrappedStart < wrappedEnd`: fire boundaries in `[wrappedStart, wrappedEnd)` (window fits within one loop iteration)
3. If `wrappedStart >= wrappedEnd`: the window spans a loop seam — fire boundaries in `[wrappedStart, loopEnd]`, then in `[loopStart, wrappedEnd)`

This handles the case where a clock's lookahead sees past the loop point before the audio thread has actually looped, including when high latency pushes the entire shifted window beyond `loopEnd`.

### Priming on Transport Start

When transport begins playback at beat `P` (play from stop, or seek then play):

1. The clock dispatch thread is notified of the start event *before* the audio thread begins rendering
2. For each subscription, fire all boundaries in `[P, P + latencyBeats)` immediately
3. The first audio block then posts `[P, P + delta)` and the shifted window `[P + latencyBeats, P + delta + latencyBeats)` picks up exactly where priming left off — no gap, no double-fire

This ensures clients can schedule events for beat 0 (or any start beat) despite the lookahead requirement.

### Priming on Seek During Playback

When the transport seeks to a new position while playing:

1. All pending tick state is implicitly invalidated (the shifted window jumps to the new position)
2. The clock dispatch thread receives a seek notification and primes as if playback were starting at the new position
3. Normal dispatch resumes from the next audio block

## Interface

### C++ (`squeeze::ClockDispatch`)

```cpp
namespace squeeze {

using SqClockCallback = void(*)(uint32_t clockId, double beat, void* userData);

class ClockDispatch {
public:
    ClockDispatch();
    ~ClockDispatch();  // stops the dispatch thread

    // --- Subscription management (control thread, internally synchronized) ---
    uint32_t addClock(double resolution, double latencyMs,
                      SqClockCallback callback, void* userData);
    void removeClock(uint32_t clockId);

    // --- Called by Engine (audio thread) ---
    void pushBeatRange(const BeatRangeUpdate& update);

    // --- Called by Engine on transport start/seek (control thread) ---
    void prime(double startBeat, double tempo,
               bool looping, double loopStart, double loopEnd);

    // --- Called by Engine on transport stop (control thread) ---
    void onTransportStop();
};

} // namespace squeeze
```

### C ABI (`squeeze_ffi.h`)

```c
typedef void* SqClock;
typedef void (*SqClockCallback)(uint32_t clock_id, double beat, void* user_data);

SqClock sq_clock_create(SqEngine engine, double resolution_beats,
                         double latency_ms,
                         SqClockCallback callback, void* user_data);
void    sq_clock_destroy(SqClock clock);

// Query
double sq_clock_get_resolution(SqClock clock);
double sq_clock_get_latency(SqClock clock);
```

### Python (`Clock` object via `Squeeze`)

```python
class Clock:
    """A beat-driven clock that fires a callback at a given resolution."""

    @property
    def resolution(self) -> float:
        """Beat interval (e.g., 0.25 for sixteenth notes)."""
        ...

    @property
    def latency_ms(self) -> float:
        """Lookahead in milliseconds."""
        ...

    def destroy(self) -> None:
        """Unsubscribe and release resources."""
        ...

# On Squeeze:
class Squeeze:
    def clock(self, resolution: float, latency_ms: float,
              callback: Callable[[float], None]) -> Clock:
        """Create a clock that calls `callback(beat)` at the given resolution.

        Args:
            resolution: Beat interval (e.g., 1/4 for quarter notes, 1/16 for sixteenths).
            latency_ms: How far ahead (in ms) the callback fires before the beat
                        is actually rendered. 0 = notify after the fact.
            callback:   Called with the beat position (float) on the clock dispatch thread.
        """
        ...
```

Usage:

```python
with Squeeze() as s:
    src = s.add_source("Synth", plugin="Diva.vst3")

    def arpeggio(beat):
        notes = [60, 64, 67, 72]
        note = notes[int(beat * 4) % len(notes)]
        src.note_on(beat=beat, channel=1, note=note, velocity=0.8)
        src.note_off(beat=beat + 0.2, channel=1, note=note)

    clk = s.clock(resolution=1/4, latency_ms=50, callback=arpeggio)

    s.transport.tempo = 120
    s.transport.playing = True
    s.start()
```

## Invariants

- The audio thread pushes at most **one** `BeatRangeUpdate` per sub-block (two per block only when a loop wraps mid-block)
- The audio thread never reads or writes subscription state
- Each beat boundary is delivered to a given subscription **exactly once** — no gaps, no duplicates
- Priming delivers all boundaries in `[startBeat, startBeat + latencyBeats)` before the audio thread renders its first block
- After priming, the first shifted window begins at `startBeat + latencyBeats` — contiguous with the primed range
- Callbacks are invoked on the clock dispatch thread, never on the audio thread or the control thread
- `removeClock` guarantees no in-flight callback for that clock after it returns
- The clock dispatch thread is idle (blocked on semaphore) when no beat range updates are pending — zero CPU cost

## Error Conditions

- `sq_clock_create` with `resolution <= 0`: returns `NULL`
- `sq_clock_create` with `latency_ms < 0`: returns `NULL`
- `sq_clock_create` with `NULL` callback: returns `NULL`
- `sq_clock_destroy(NULL)`: no-op
- Beat range queue overflow (audio thread pushes faster than dispatch thread drains): drop the oldest update and log `SQ_WARN_RT`. In practice this should not happen — the dispatch thread's work per update is trivial compared to a block period
- Callback throws/crashes: the dispatch thread catches all exceptions (`catch(...)`) around each callback invocation, logs `SQ_WARN("clock %d callback threw — skipping", clockId)`, and continues to the next subscription. The faulting callback is not retried for this beat range update. This prevents a single misbehaving callback (especially plausible from Python/ctypes) from permanently locking `subscriptionMutex_` and bricking the dispatch thread. Callbacks should still not throw — but if they do, the system survives

## Does NOT Handle

- Scheduling events into the engine — that is the client's responsibility from within the callback, using existing `sq_schedule_*` functions (which acquire `controlMutex_` independently; see Thread Safety)
- Tempo changes — the clock dispatch thread uses whatever tempo the audio thread reports in each `BeatRangeUpdate`. Tempo ramps, automation, or tap tempo are Transport/Engine concerns
- Wall-time clocks — future extension. Same dispatch thread, different trigger source (real clock instead of transport beat position). Not specified here
- Swing/groove quantization — future extension. Would offset tick positions before firing callbacks
- Sub-sample-accurate tick timing — ticks report beat positions only. Sample-accurate resolution happens when the client schedules events back through `EventScheduler`

## Dependencies

- `SPSCQueue` — lock-free queue for `BeatRangeUpdate` (audio → dispatch thread)
- `Engine` — owns the `ClockDispatch` instance, calls `pushBeatRange` from `processBlock` and `prime`/`onTransportStop` from transport state changes
- `Transport` — provides beat position, tempo, loop state (read by Engine, forwarded to `ClockDispatch`)

## Thread Safety

### Thread Roles

| Thread | Operations |
|--------|-----------|
| Audio thread | `pushBeatRange()` only — one lock-free queue push per sub-block, then `sem_post` |
| Clock dispatch thread | `sem_wait()`, drain queue, iterate subscriptions, fire callbacks. Owns all tick detection logic. Callbacks may call `sq_schedule_*` functions. |
| Control thread | `addClock()`, `removeClock()`, `prime()`, `onTransportStop()`. Internally synchronized with the dispatch thread via `subscriptionMutex_`. |

### Lock Hierarchy

Two mutexes exist, and they are **independent** — no code path acquires both:

| Mutex | Purpose | Acquired by | Never acquired by |
|-------|---------|-------------|-------------------|
| `controlMutex_` (Engine) | Protects Engine state, serializes `sq_schedule_*` and other `sq_*` calls | Control thread, dispatch thread (from within callbacks calling `sq_schedule_*`) | Audio thread, clock management functions |
| `subscriptionMutex_` (ClockDispatch) | Protects subscription list, synchronizes `addClock`/`removeClock` with dispatch thread iteration | Control thread (`addClock`, `removeClock`), dispatch thread (iteration) | Audio thread, `sq_schedule_*` functions |

**Why no deadlock is possible:**

- `sq_clock_create` and `sq_clock_destroy` acquire `subscriptionMutex_` only — never `controlMutex_`.
- `sq_schedule_*` functions acquire `controlMutex_` only — never `subscriptionMutex_`.
- No code path acquires both mutexes simultaneously, so no circular dependency can form.

**Concrete scenario — callback schedules events during `removeClock`:**

```
Dispatch thread                        Control thread
───────────────                        ──────────────
holds subscriptionMutex_ (iterating)
  fires callback
    calls sq_schedule_param_change()
      acquires controlMutex_ ✓         sq_clock_destroy()
      resolves name → token              tries subscriptionMutex_
      pushes to EventScheduler             blocked (dispatch holds it)
      releases controlMutex_               ... waits, no deadlock
  callback returns
releases subscriptionMutex_            acquires subscriptionMutex_ ✓
                                         proceeds with removal
```

**SPSC queue safety:** EventScheduler's queue is SPSC (single producer, single consumer). Both the control thread and the dispatch thread may call `sq_schedule_*`, but `controlMutex_` serializes all pushes, so only one thread produces at a time. The single-producer invariant is maintained by the mutex.

**RT safety:** The audio thread never acquires any mutex. `pushBeatRange` is a lock-free write + semaphore post — fully RT-safe.

### Restrictions

- **Do not call `sq_clock_create` or `sq_clock_destroy` from within a clock callback.** The dispatch thread holds `subscriptionMutex_` during iteration; attempting to acquire it again from a callback would self-deadlock. If dynamic clock management from callbacks is needed, defer it (e.g., set a flag and handle it after the callback returns).

## Implementation Notes

### Clock Dispatch Thread Lifecycle

1. Created in `ClockDispatch` constructor, blocks on semaphore immediately
2. Woken by `sem_post` from `pushBeatRange` (audio thread) or from `prime`/`onTransportStop` (control thread signaling via the same semaphore)
3. On wake: drain the beat range queue, process each update against all subscriptions
4. On `ClockDispatch` destructor: set a stop flag, post the semaphore, join the thread

### Subscription Synchronization

`addClock` and `removeClock` are called from the control thread. The dispatch thread iterates subscriptions. Both use `subscriptionMutex_` — a mutex internal to `ClockDispatch`, independent of Engine's `controlMutex_`:

- The dispatch thread holds `subscriptionMutex_` while iterating **all** subscriptions and firing **all** callbacks in a single pass.
- `addClock` acquires `subscriptionMutex_`, adds the subscription, releases.
- `removeClock` acquires `subscriptionMutex_`, marks the subscription for removal, and waits for the dispatch thread to finish its current iteration before returning — guaranteeing no in-flight callback on return. **Known constraint:** because `subscriptionMutex_` is held for the entire iteration pass, `removeClock` for clock A is blocked by callbacks on *all* clocks in the current pass, not just clock A's. A slow callback on an unrelated clock delays removal. This is acceptable — per-clock locking would add complexity for minimal practical gain, since callbacks should be fast (just scheduling work, not doing it).

### Semaphore Choice

`sem_post` is async-signal-safe on POSIX and RT-safe. On macOS, use `dispatch_semaphore_t` or a lightweight alternative. On Windows, use `CreateSemaphore`. JUCE's `WaitableEvent` (non-manual-reset) is another option.

## Example: Full Lifecycle

```
1. Engine creates ClockDispatch → dispatch thread starts, blocks on semaphore

2. Python: clk = s.clock(resolution=1/4, latency_ms=50, callback=fn)
   → sq_clock_create → ClockDispatch::addClock
   → subscription added to list

3. Python: s.transport.playing = True
   → Engine::transportPlay()
   → Engine calls ClockDispatch::prime(startBeat=0, tempo=120, ...)
   → dispatch thread fires fn(0.0) immediately (beat 0 is within lookahead)

4. Audio thread starts rendering blocks:
   → processBlock: transport advances [0.0, 0.003)
   → pushBeatRange({0.0, 0.003, 120, ...})
   → dispatch thread wakes, shifts window by latencyBeats=0.1
   → shifted window [0.1, 0.103): no new boundary → back to sleep

5. Many blocks later, shifted window crosses 0.25:
   → dispatch thread fires fn(0.25)
   → Python callback: src.note_on(beat=0.25, ...)
   → event enters EventScheduler, resolved on audio thread when beat 0.25 arrives

6. Python: clk.destroy()
   → sq_clock_destroy → ClockDispatch::removeClock
   → waits for no in-flight callback, removes subscription

7. Engine destroyed → ClockDispatch destructor stops dispatch thread
```
