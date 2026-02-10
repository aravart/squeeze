# EventQueue Specification

## Responsibilities

- Accept beat-timestamped events from the control thread (Lua)
- Deliver events to the audio thread, resolved to sample offsets within the current block
- Handle dispatch across loop boundaries
- Support multiple event types: MIDI messages, parameter changes, extensible to future types

## Event Types

```cpp
struct ScheduledEvent {
    double beatTime;        // PPQ timestamp (quarter notes from timeline origin)
    int targetNodeId;       // which node receives this event

    enum class Type { noteOn, noteOff, cc, paramChange };
    Type type;

    int channel;            // MIDI channel 1-16 (MIDI events only)

    // Interpretation depends on type:
    //   noteOn:      data1 = note (0-127), floatValue = velocity (0-127 as float)
    //   noteOff:     data1 = note (0-127)
    //   cc:          data1 = CC number (0-127), data2 = value (0-127)
    //   paramChange: data1 = paramIndex, floatValue = normalized value (0.0-1.0)
    int data1;
    int data2;
    float floatValue;
};
```

Trivially copyable. No heap allocation. Fixed size so it fits in an SPSC queue.

## Resolved Events

After time-matching and sample offset computation, events are delivered as:

```cpp
struct ResolvedEvent {
    int sampleOffset;       // sample position within the block [0, numSamples)
    int targetNodeId;
    ScheduledEvent::Type type;
    int channel;
    int data1;
    int data2;
    float floatValue;
};
```

Engine uses `type` to dispatch: MIDI types are written into the node's input MidiBuffer at `sampleOffset`. `paramChange` events trigger sub-block splitting so `setParameter()` takes effect at the correct sample. See [SampleAccurateDispatch](SampleAccurateDispatch.md).

## Interface

### Producer (control thread)

```cpp
bool schedule(const ScheduledEvent& event);
```

Pushes an event into the SPSC queue. Returns false if the queue is full (event dropped). Called from the control thread; serialized by Engine's `controlMutex_`.

### Consumer (audio thread)

```cpp
int retrieve(double blockStartBeats, double blockEndBeats,
             bool looped, double loopStartBeats, double loopEndBeats,
             int numSamples, double tempo, double sampleRate,
             ResolvedEvent* out, int maxOut);
```

Called once per `processBlock`. Returns the number of resolved events written to `out`. Events are sorted by `sampleOffset`.

Internally:

1. **Drain**: pop all available events from the SPSC queue into an internal staging buffer (fixed-capacity, audio-thread-local, pre-allocated).
2. **Match**: scan the staging buffer for events whose `beatTime` falls within the block's time range.
3. **Resolve**: convert `beatTime` to `sampleOffset` (see Position Resolution below).
4. **Output**: write matched events to `out`, sorted by `sampleOffset`. Remove them from the staging buffer.
5. **Expire**: discard events in the staging buffer whose forward distance exceeds the expiry threshold: one full loop length (when looping) or 16 beats (when not looping). Uses the forward-distance calculation (see Late Events). The 16-beat threshold is a fixed constant — EventQueue has no knowledge of time signature. Stale discards are logged at warn level.

### Housekeeping

```cpp
void clear();
```

Discards all events in both the SPSC queue and the staging buffer. Called on transport stop or seek (position discontinuity) to prevent stale events from firing at the wrong time.

## Position Resolution

### Normal case (no loop wrap)

Block covers `[blockStartBeats, blockEndBeats)`. An event at `beatTime` B is dispatched if `blockStartBeats <= B < blockEndBeats`.

```
sampleOffset = round((B - blockStartBeats) * sampleRate * 60.0 / tempo)
```

Clamped to `[0, numSamples - 1]`.

### Loop wrap case

When the block straddles a loop boundary, the time range is discontinuous: `[blockStartBeats, loopEndBeats)` then `[loopStartBeats, blockEndBeats)`.

```
wrapSample = round((loopEndBeats - blockStartBeats) * sampleRate * 60.0 / tempo)

Pre-wrap events  (B in [blockStartBeats, loopEndBeats)):
    sampleOffset = round((B - blockStartBeats) * sampleRate * 60.0 / tempo)

Post-wrap events (B in [loopStartBeats, blockEndBeats)):
    sampleOffset = wrapSample
                 + round((B - loopStartBeats) * sampleRate * 60.0 / tempo)
```

### Late events

An event is "late" if the transport has already passed its `beatTime` without dispatching it. To determine this, compute the forward distance from `blockStartBeats` to the event's `beatTime` through the loop:

```
if (!looping)
    ahead = beatTime - blockStartBeats          // negative means late

if (looping)
    if (beatTime >= blockStartBeats)
        ahead = beatTime - blockStartBeats
    else
        ahead = (loopEnd - blockStartBeats) + (beatTime - loopStart)
    // ahead > loopLength means the transport went all the way around — late
```

Late events (within one bar tolerance) are dispatched at `sampleOffset = 0` and logged at warn level (`SQ_LOG_RT_WARN`). This handles minor scheduling jitter gracefully — better to play a note slightly late than to drop it.

## Staging Buffer

The audio thread maintains a fixed-capacity staging buffer (`std::array<ScheduledEvent, N>`) that persists across blocks. This is necessary because:

- The SPSC queue is FIFO — events can only be popped in order, not selectively by time.
- Lua schedules events for a lookahead window that may span multiple blocks. Events popped now may not be dispatched until future blocks.

The staging buffer is scanned linearly each block. With a typical lookahead of 1-2 bars, the buffer holds at most a few hundred events. Linear scan is negligible.

**Capacity**: 4096 events. If the staging buffer is full when draining the SPSC queue, new events are dropped (logged as a warning). This indicates the lookahead window is too large or events aren't being consumed.

## Invariants

- Events are always dispatched in `sampleOffset` order within a block
- Each event is dispatched exactly once, then consumed
- The SPSC queue has exactly one producer (control thread, serialized by `controlMutex_`) and one consumer (audio thread)
- The staging buffer is only accessed by the audio thread — no synchronization needed
- `retrieve()` never allocates
- `clear()` is called from the audio thread (via Scheduler command on transport stop/seek)

## Error Conditions

- `schedule()` with full SPSC queue: returns false, event dropped
- Staging buffer full: events from SPSC queue are dropped during drain, logged
- `beatTime` is NaN or negative: event is discarded during matching
- `targetNodeId` doesn't exist: Engine skips the event during dispatch (not EventQueue's concern)

## Does NOT Handle

- What events mean — Engine interprets types and dispatches to nodes
- Pattern storage or repetition — client's responsibility
- Loop-awareness in scheduling — client schedules across loop boundaries; EventQueue only handles dispatch
- Transport state — Engine passes block timing parameters; EventQueue has no Transport dependency

## Dependencies

- `SPSCQueue` (from core, already exists)
- Standard library only (no JUCE dependency)

## Thread Safety

- `schedule()`: control thread only (serialized by Engine's `controlMutex_`)
- `retrieve()`, `clear()`: audio thread only
- SPSC queue enforces single-producer / single-consumer
- Staging buffer is audio-thread-local, no synchronization

## Relationship to Existing Scheduler

The existing `Scheduler` handles **instant infrastructure commands**: graph swaps, parameter knob turns. These execute at the top of `processBlock` (sample offset 0) with no musical timing.

`EventQueue` handles **beat-timed musical events**: notes, automation, parameter locks. These are resolved to specific sample offsets within blocks for sample-accurate timing.

Both use SPSC queues for control → audio transfer. They are separate components with different purposes:

| | Scheduler | EventQueue |
|---|---|---|
| **Purpose** | Infrastructure commands | Musical events |
| **Timing** | Instant (block boundary) | Beat-synced (sample-accurate) |
| **Examples** | Graph swap, knob turn | Note on, parameter lock |
| **Consumed by** | Engine directly | Engine → dispatched to nodes |

## Example Usage

```cpp
EventQueue eq;

// Lua schedules a note and a parameter change for beat 4.0
eq.schedule({4.0, samplerNodeId, ScheduledEvent::Type::noteOn,
             /*channel*/1, /*data1=note*/60, /*data2*/0, /*velocity*/100.0f});
eq.schedule({4.0, samplerNodeId, ScheduledEvent::Type::paramChange,
             /*channel*/0, /*data1=paramIdx*/2, /*data2*/0, /*value*/0.3f});

// In processBlock — block covers beats 3.95 to 4.07
ResolvedEvent resolved[64];
int n = eq.retrieve(3.95, 4.07,
                    /*looped*/false, 0, 0,
                    /*numSamples*/512, /*tempo*/120.0, /*sr*/44100.0,
                    resolved, 64);
// n == 2
// resolved[0]: noteOn at sampleOffset ≈ 57
// resolved[1]: paramChange at sampleOffset ≈ 57
// Engine delivers: MidiBuffer.addEvent(noteOn, 57),
//                  node->setParameter(2, 0.3f)

// Loop wrap example — block covers beats 15.9 to 0.1, loop [0, 16)
n = eq.retrieve(15.9, 0.1,
                /*looped*/true, 0.0, 16.0,
                512, 120.0, 44100.0,
                resolved, 64);
// Events at beat 15.95 get low sample offsets (pre-wrap)
// Events at beat 0.05 get high sample offsets (post-wrap)
```
