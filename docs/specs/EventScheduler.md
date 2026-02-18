# EventScheduler Specification

## Responsibilities
- Accept beat-timestamped events from the control thread
- Deliver events to the audio thread, resolved to sample offsets within the current block
- Manage a persistent staging buffer for events scheduled ahead of the current block
- Support event types: MIDI messages (noteOn, noteOff, CC) and parameter changes

## Overview

EventScheduler is the beat-synced event delivery system. It accepts events timestamped in beats (PPQ / quarter notes), holds them in a staging buffer, and resolves them to sample-accurate offsets within the current audio block. Engine calls `retrieve()` once per block (or per sub-block at loop boundaries), passing the current beat range, and receives a sorted array of resolved events ready for dispatch.

EventScheduler has no knowledge of Transport, sources, loops, or audio processing. It is a pure time-resolution engine: beats in, sample offsets out. Engine provides the timing context and interprets the results.

### Loop handling

EventScheduler does **not** handle loop wrapping. When Transport loops, Engine splits the block at the loop boundary and calls `retrieve()` twice — once for the pre-wrap segment and once for the post-wrap segment. Each call sees a simple monotonic beat range (`blockStartBeats < blockEndBeats`). This is the same sub-block splitting mechanism Engine uses for parameter changes.

Loop awareness is the client's responsibility. Clients schedule events just-in-time and re-schedule from the loop start as needed.

### What flows through EventScheduler

| Event type | Fields used | Engine dispatches as |
|---|---|---|
| `noteOn` | channel, data1=note, floatValue=velocity (0.0–1.0) | `juce::MidiMessage::noteOn` at sampleOffset |
| `noteOff` | channel, data1=note | `juce::MidiMessage::noteOff` at sampleOffset |
| `cc` | channel, data1=CC number, data2=value (0–127) | `juce::MidiMessage::controllerEvent` at sampleOffset |
| `paramChange` | data1=paramToken (opaque), floatValue=normalized value (0.0–1.0) | Sub-block split: `setParameter()` between sub-blocks |

### What does NOT flow through EventScheduler

- Infrastructure commands (CommandQueue)
- MIDI device input (MidiRouter)
- Immediate parameter changes (set directly on control thread)

## Interface

```cpp
namespace squeeze {

struct ScheduledEvent {
    double beatTime;        // PPQ timestamp (quarter notes from origin)
    int targetHandle;       // source or processor handle

    enum class Type { noteOn, noteOff, cc, paramChange };
    Type type;

    int channel;            // MIDI channel 1–16 (MIDI events only)
    int data1;              // note number, CC number, or param token
    int data2;              // CC value (0–127)
    float floatValue;       // velocity (0.0–1.0) or param value (0.0–1.0)
};

struct ResolvedEvent {
    int sampleOffset;       // sample position within the block [0, numSamples)
    int targetHandle;
    ScheduledEvent::Type type;
    int channel;
    int data1;
    int data2;
    float floatValue;
};

class EventScheduler {
public:
    EventScheduler() = default;
    ~EventScheduler() = default;

    // Non-copyable
    EventScheduler(const EventScheduler&) = delete;
    EventScheduler& operator=(const EventScheduler&) = delete;

    // --- Control thread ---
    bool schedule(const ScheduledEvent& event);

    // --- Audio thread ---
    int retrieve(double blockStartBeats, double blockEndBeats,
                 int numSamples, double tempo, double sampleRate,
                 ResolvedEvent* out, int maxOut);

    void clear();

private:
    static constexpr int kQueueCapacity   = 4096;
    static constexpr int kStagingCapacity = 4096;

    SPSCQueue<ScheduledEvent, kQueueCapacity> queue_;
    std::array<ScheduledEvent, kStagingCapacity> staging_;
    int stagingCount_ = 0;
};

} // namespace squeeze
```

## ScheduledEvent

Trivially copyable POD struct. No heap allocation. Fixed size for SPSC queue storage.

| Field | Type | Description |
|-------|------|-------------|
| `beatTime` | `double` | PPQ timestamp — quarter notes from timeline origin |
| `targetHandle` | `int` | Source or processor handle (Engine resolves to the target) |
| `type` | `Type` | Event type: noteOn, noteOff, cc, paramChange |
| `channel` | `int` | MIDI channel 1–16 (MIDI events only; ignored for paramChange) |
| `data1` | `int` | Note number (0–127), CC number (0–127), or param token (opaque int) |
| `data2` | `int` | CC value (0–127); unused for other types |
| `floatValue` | `float` | Velocity (0.0–1.0 normalized) or parameter value (0.0–1.0 normalized) |

### Parameter tokens

The `paramChange` event type stores an opaque `int` token in `data1`, not a parameter name. Engine pre-resolves parameter names to tokens at schedule time on the control thread (where string operations are safe). On the audio thread, Engine uses the token to dispatch the parameter change. This is an Engine implementation detail — EventScheduler treats `data1` as an opaque int regardless of event type.

### Velocity normalization

Velocity is stored as a normalized float (0.0–1.0), not MIDI 0–127. Engine converts to MIDI velocity when building `juce::MidiMessage` for dispatch. This is consistent with the parameter normalization convention used throughout v2.

## ResolvedEvent

Same fields as `ScheduledEvent` but with `beatTime` replaced by `sampleOffset` — the sample position within the current block where the event should take effect.

`sampleOffset` is clamped to `[0, numSamples - 1]`.

## Control Thread API

### `schedule(event)`

Pushes an event onto the SPSC queue for the audio thread.

- Returns `true` on success
- Returns `false` if the queue is full — the event is **dropped**
- Logs at warn level on failure
- Serialized by Engine's `controlMutex_` (SPSC single-producer invariant)

## Audio Thread API

### `retrieve(blockStartBeats, blockEndBeats, numSamples, tempo, sampleRate, out, maxOut)`

Resolves beat-timestamped events to sample offsets for the current block. Returns the number of resolved events written to `out`.

**Preconditions:**
- `tempo > 0`, `sampleRate > 0` (undefined behavior otherwise — division by zero)
- `blockStartBeats < blockEndBeats` (caller must not pass wrapped ranges — Engine splits at loop boundaries before calling)

**Algorithm:**

#### Phase 1: Drain SPSC into staging

Pop all events from the SPSC queue into the staging buffer. Discard events with `beatTime` that is NaN or negative. If the staging buffer is full, drop incoming events with a warning (`SQ_WARN_RT`).

#### Phase 2: Scan staging, match to block

For each event in the staging buffer (reverse scan for O(1) swap-remove):

1. **Compute distance**: `ahead = beatTime - blockStartBeats`
   - Positive: event is in the future (or within this block)
   - Negative: event is in the past

2. **Expiry check** — remove stale events that will never fire:
   - Expire if `ahead < -kExpiryBeats` (more than 16 beats in the past)
   - Expired events are removed from staging with a warning (`SQ_WARN_RT`)

3. **Match to block** — compute `sampleOffset`:
   ```
   samplesPerBeat = sampleRate * 60.0 / tempo
   ```
   Match if `beatTime` is in `[blockStartBeats, blockEndBeats)`.
   `sampleOffset = round((beatTime - blockStartBeats) * samplesPerBeat)`

4. **Late event rescue** — if the event didn't match the block window but is only slightly late:
   - Dispatch at `sampleOffset = 0` if `0 < backward <= kLateToleranceBeats` (1.0 beat)
   - Where `backward = blockStartBeats - beatTime`
   - Log at warn level: `SQ_WARN_RT("late event rescued: beatTime=%.3f, blockStart=%.3f, late by %.3f beats")`

5. **Emit or skip**:
   - If matched and output has room (`outCount < maxOut`): write to `out`, remove from staging
   - If matched but output is full: **keep in staging** (do not consume — the event will be retried next block). Log at warn level: `SQ_WARN_RT("output buffer full, postponing event to next block")`
   - If no match and not late enough: keep in staging for future blocks

#### Phase 3: Sort output

Insertion sort on the output array (small N per block):
- Primary key: `sampleOffset` ascending
- Secondary key: type priority ascending

**Type priority order:** `noteOff (0) < cc (1) < paramChange (2) < noteOn (3)`

This guarantees: noteOff fires before noteOn at the same beat (correct phrase transitions), and paramChange fires before noteOn (plugin state set before note triggers).

### `clear()`

Discards all events: drains the SPSC queue (discard all) and resets `stagingCount_ = 0`.

Called from the audio thread (inside CommandQueue's handler) on transport stop or seek. Not called on pause — pausing preserves staged events for resume.

## Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `kQueueCapacity` | 4096 | SPSC queue capacity |
| `kStagingCapacity` | 4096 | Staging buffer capacity |
| `kLateToleranceBeats` | 1.0 | Late event rescue window (beats) |
| `kExpiryBeats` | 16.0 | Expiry threshold (beats behind blockStart) |

## Staging Buffer

The staging buffer is a fixed-size `std::array<ScheduledEvent, 4096>` that persists across `retrieve()` calls. It exists because:

- The SPSC queue is FIFO — events can't be selectively popped by time
- Events may be scheduled for beats many blocks in the future
- Events must survive in staging until their beat time falls within a block

The staging buffer is **audio-thread-local only**. No synchronization needed. Removal uses swap-with-last during reverse iteration — O(1) per removal.

`clear()` resets `stagingCount_ = 0` without zeroing the array.

## Invariants

- `blockStartBeats < blockEndBeats` — EventScheduler never sees wrapped ranges
- Events are dispatched in `sampleOffset` order with deterministic type priority for same-offset ties
- Each event is dispatched exactly once, then consumed from staging
- Events that don't fit in `maxOut` are **not consumed** — they remain in staging for the next block
- SPSC: single producer (control thread), single consumer (audio thread)
- Staging buffer is audio-thread-local only
- `retrieve()` never allocates — all storage is pre-allocated
- `clear()` is called from the audio thread only (via CommandQueue handler)
- `ScheduledEvent` is trivially copyable

## Error Conditions

- `schedule()` with full SPSC queue: returns `false`, event dropped, logged at warn
- Staging buffer full during drain: incoming events dropped, logged at warn (`SQ_WARN_RT`)
- `beatTime` is NaN or negative: event discarded during drain (never enters staging)
- `targetHandle` doesn't exist: Engine skips the event (not EventScheduler's concern — source/processor may have been removed)
- `tempo <= 0` or `sampleRate <= 0`: undefined behavior (caller must guard)

## Does NOT Handle

- What events mean — Engine interprets and dispatches (MIDI → MidiBuffer, paramChange → sub-block split)
- Loop wrapping — Engine splits blocks at loop boundaries before calling `retrieve()`
- Pattern storage, repetition, or loop scheduling — client's responsibility
- Event cancellation — once scheduled, events cannot be removed; client must schedule compensating events
- Stuck note prevention — `clear()` discards pending noteOffs; Engine sends all-notes-off (CC 123) on stop/seek to compensate (see Engine spec)
- Transport state — Engine passes timing parameters; EventScheduler has no Transport dependency
- Source/processor lookup or parameter name resolution — Engine's concern
- MIDI device input (MidiRouter)
- Infrastructure commands (CommandQueue)

## Dependencies

- SPSCQueue (lock-free ring buffer)

No dependency on Source, Bus, Processor, Transport, or Engine. EventScheduler is a pure time-resolution component.

## Thread Safety

| Method | Thread | Serialization |
|--------|--------|---------------|
| `schedule()` | Control | Engine's `controlMutex_` ensures single producer |
| `retrieve()` | Audio | Single consumer by design (one audio callback) |
| `clear()` | Audio | Called inside CommandQueue handler on audio thread |

EventScheduler has no mutexes. Thread safety relies on the SPSC contract (one producer, one consumer) and the fact that staging is audio-thread-local.

## C ABI

EventScheduler has no direct C ABI surface. Events are scheduled through Engine-level functions:

```c
// Schedule beat-timed events (MIDI events target a Source, param changes target a Processor)
bool sq_schedule_note_on(SqEngine engine, SqSource src, double beat_time,
                         int channel, int note, float velocity);
bool sq_schedule_note_off(SqEngine engine, SqSource src, double beat_time,
                          int channel, int note);
bool sq_schedule_cc(SqEngine engine, SqSource src, double beat_time,
                    int channel, int cc_num, int cc_val);
bool sq_schedule_param_change(SqEngine engine, SqProc proc, double beat_time,
                              const char* param_name, float value);
```

Note: `sq_schedule_param_change` takes a `param_name` string. Engine resolves the name to an internal token on the control thread before pushing the event to EventScheduler. The string never reaches the audio thread.

## Python API

```python
engine.schedule_note_on(synth, beat_time=4.0, channel=1, note=60, velocity=0.8)
engine.schedule_note_off(synth, beat_time=4.5, channel=1, note=60)
engine.schedule_cc(synth, beat_time=4.0, channel=1, cc=1, value=64)
engine.schedule_param_change(synth, beat_time=4.0, param="cutoff", value=0.7)
```

## Example: Engine processBlock Integration

```cpp
// 1. CommandQueue drained (may clear EventScheduler on stop/seek)
commandQueue_.processPending(handler);

// 2. Transport advanced
transport_.advance(numSamples);

// 3. Resolve events for this block (or sub-block if loop split)
//    Engine handles loop boundaries by splitting the block:
//    - If no loop wrap: single retrieve() for the full block
//    - If loop wrap: retrieve() for [blockStart, loopEnd), then
//                    retrieve() for [loopStart, blockEnd)
int resolvedCount = 0;
if (transport_.isPlaying() && transport_.getSampleRate() > 0.0) {
    resolvedCount = eventScheduler_.retrieve(
        blockStartBeats, blockEndBeats,
        numSamples,
        transport_.getTempo(),
        transport_.getSampleRate(),
        resolvedEvents_, kMaxResolvedEvents);
}

// 4. Engine dispatches resolved events:
//    - MIDI types → source MidiBuffers at sampleOffset
//    - paramChange → sub-block splitting
```
