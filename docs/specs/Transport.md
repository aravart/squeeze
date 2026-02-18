# Transport Specification

## Responsibilities

- Track playback state (stopped, playing, paused)
- Maintain sample-accurate position (ground truth in samples)
- Derive musical position (PPQ / quarter-note beats, bars) from sample position and tempo
- Store tempo (BPM) and time signature
- Advance position each audio block, with loop wrapping when enabled
- Track block start/end beat positions and loop wrap status for downstream consumers (EventScheduler, ClockDispatch)
- Implement `juce::AudioPlayHead` so hosted plugins receive tempo and position info via `getPosition()`

## Types

```cpp
enum class TransportState { stopped, playing, paused };
```

Time signature uses `juce::AudioPlayHead::TimeSignature` directly (numerator, denominator, both int, default 4/4).

## Interface

### Construction

```cpp
Transport();  // stopped, position 0, 120 BPM, 4/4, sampleRate 0, blockSize 0
```

### Audio-thread method

```cpp
void advance(int numSamples);
```

Called once per `processBlock`. Advances `positionInSamples_` by `numSamples` when state is `playing`. No-op when stopped or paused. No-op when `numSamples <= 0`.

When looping is enabled and the advance crosses the loop end point, position wraps to the loop start and `didLoopWrap_` is set to true. `didLoopWrap_` is reset to false at the start of each `advance()`.

Stores pre-advance and post-advance beat positions for block range queries (`getBlockStartBeats()`, `getBlockEndBeats()`).

Loop wrapping uses cached integer sample boundaries — no floating-point conversion on the audio thread (see Loop Behavior).

### State control

Called on the audio thread via Engine's `handleCommand`, which processes commands at the top of `processBlock` before `advance()`.

```cpp
void play();    // → playing (from stopped or paused). No-op if already playing.
void stop();    // → stopped, resets positionInSamples_ to 0. No-op if already stopped.
void pause();   // → paused, position preserved. No-op if already paused or stopped.

void setTempo(double bpm);
void setTimeSignature(int numerator, int denominator);
void setPositionInSamples(int64_t samples);
void setPositionInBeats(double beats);  // converts to samples via tempo + sampleRate

void setLoopPoints(double startBeats, double endBeats);  // in PPQ (quarter notes)
void setLooping(bool enabled);

void prepare(double sampleRate, int blockSize);  // called during Engine construction / prepare
```

`setTempo()`, `prepare()`, and `setLoopPoints()` all recompute the cached loop sample boundaries (see Loop Behavior).

### Queries

All const. No synchronization needed — called on the audio thread only (Engine mediates cross-thread access; see Thread Safety).

```cpp
TransportState getState() const;
bool isPlaying() const;           // state == playing

double getTempo() const;          // BPM
juce::AudioPlayHead::TimeSignature getTimeSignature() const;
double getSampleRate() const;
int getBlockSize() const;

int64_t getPositionInSamples() const;
double getPositionInSeconds() const;   // positionInSamples / sampleRate
double getPositionInBeats() const;     // PPQ (quarter-note position)
int64_t getBarCount() const;           // 0-based, how many complete bars elapsed
double getPpqOfLastBarStart() const;   // PPQ position where current bar began

bool isLooping() const;
double getLoopStartBeats() const;      // PPQ
double getLoopEndBeats() const;        // PPQ

// Block range (read by Engine for EventScheduler and ClockDispatch)
bool didLoopWrap() const;
double getBlockStartBeats() const;     // beat position before advance
double getBlockEndBeats() const;       // beat position after advance (after wrap)
```

### juce::AudioPlayHead

```cpp
class Transport : public juce::AudioPlayHead {
    juce::Optional<PositionInfo> getPosition() const override;
};
```

`getPosition()` populates:

| PositionInfo field            | Value                                     |
|-------------------------------|-------------------------------------------|
| `timeInSamples`               | `positionInSamples_`                      |
| `timeInSeconds`               | `positionInSamples_ / sampleRate_`        |
| `ppqPosition`                 | `getPositionInBeats()`                    |
| `ppqPositionOfLastBarStart`   | `getPpqOfLastBarStart()`                  |
| `barCount`                    | `getBarCount()`                           |
| `bpm`                         | `tempo_`                                  |
| `timeSignature`               | `timeSignature_`                          |
| `isPlaying`                   | `state_ == playing`                       |
| `isRecording`                 | `false`                                   |
| `isLooping`                   | `looping_`                                |
| `loopPoints.ppqStart`         | `loopStartBeats_` (if looping)            |
| `loopPoints.ppqEnd`           | `loopEndBeats_` (if looping)              |

## Position Model

Sample position is the ground truth. All musical positions are derived.

```
positionInSeconds  = positionInSamples / sampleRate

positionInBeats    = positionInSeconds * (tempo / 60.0)         // PPQ (quarter notes)

quarterNotesPerBar = numerator * (4.0 / denominator)
                     // 4/4 → 4.0, 3/4 → 3.0, 6/8 → 3.0, 7/8 → 3.5

barCount           = floor(positionInBeats / quarterNotesPerBar)

ppqOfLastBarStart  = barCount * quarterNotesPerBar
```

`setPositionInBeats(beats)` converts back: `positionInSamples = round(beats * 60.0 / tempo * sampleRate)`.

### Beats-to-samples conversion

```
beatsToSamples(beats) = round(beats * 60.0 / tempo * sampleRate)
```

Called only from control-thread mutations (`setPositionInBeats`, `setLoopPoints`) and when inputs change (`setTempo`, `prepare`). Never called on the audio thread's hot path.

## Loop Behavior

Loop boundaries are stored in both beats (for queries and `AudioPlayHead`) and cached as integer samples (for `advance()`). The cached sample values are recomputed whenever the inputs change — `setLoopPoints()`, `setTempo()`, or `setSampleRate()` — so `advance()` does pure integer arithmetic with no per-block FP conversion.

### Minimum loop length

The minimum loop length is `blockSize_` samples — one audio block. A loop at least one block long wraps at most once per `advance()`, which is the normal case. Anything shorter fires the modulo every block, producing a buzz rather than a loop.

The minimum is enforced in the sample domain after caching, so it accounts for the current tempo, sample rate, and block size. `prepare(sampleRate, blockSize)` sets `blockSize_`; before `prepare()` is called, `blockSize_` is 0 and no minimum is enforced (loop points can be set freely during setup).

If `setTempo()` or `prepare()` causes a previously valid loop to fall below the minimum, looping is automatically disabled (`looping_ = false`) and a warning is logged. The beat-domain loop points are preserved so that a subsequent tempo/sample-rate/block-size change can re-enable the loop if the length becomes valid again.

### Members

```cpp
double  loopStartBeats_ = 0.0;       // beat-domain source of truth (for queries)
double  loopEndBeats_   = 0.0;
int64_t loopStartSamples_ = 0;       // cached: beatsToSamples(loopStartBeats_)
int64_t loopEndSamples_   = 0;       // cached: beatsToSamples(loopEndBeats_)
```

### Cache recomputation

```cpp
void recomputeLoopSamples() {
    loopStartSamples_ = beatsToSamples(loopStartBeats_);
    loopEndSamples_   = beatsToSamples(loopEndBeats_);
    if (looping_ && blockSize_ > 0
        && loopEndSamples_ - loopStartSamples_ < blockSize_) {
        looping_ = false;
        SQ_WARN("Transport: loop too short (%lld samples, block size %d), disabling",
                (long long)(loopEndSamples_ - loopStartSamples_), blockSize_);
    }
}
```

Called from `setLoopPoints()`, `setTempo()`, and `prepare()`.

### advance()

```cpp
advance(int numSamples):
    didLoopWrap_ = false;
    if (state_ != playing || numSamples <= 0) return;

    blockStartBeats_ = getPositionInBeats();
    positionInSamples_ += numSamples;

    if (looping_ && loopEndSamples_ > loopStartSamples_
        && positionInSamples_ >= loopEndSamples_) {
        int64_t loopLen = loopEndSamples_ - loopStartSamples_;
        positionInSamples_ = loopStartSamples_
            + ((positionInSamples_ - loopStartSamples_) % loopLen);
        didLoopWrap_ = true;
    }

    blockEndBeats_ = getPositionInBeats();
```

No `beatsToSamples()` call. Loop comparison and modular arithmetic are pure integer ops — deterministic, no jitter, no per-block FP rounding.

### Why cached samples

The spec says "sample position is the ground truth." If loop boundaries lived only in beats and were re-derived via `beatsToSamples()` every block, the actual loop fire point would be a `round()` call on the audio thread — not a stable integer. Caching makes the loop boundaries true integers, consistent with the position model. The only cost is three trivial recomputations on the control thread when tempo, sample rate, or loop points change.

## Block Range Tracking

`advance()` stores:
- `blockStartBeats_`: beat position before advance
- `blockEndBeats_`: beat position after advance (after loop wrap if any)
- `didLoopWrap_`: true if position wrapped this block

These are raw ingredients. Transport does not split the block into sub-ranges or feed consumers directly. When a loop wrap occurs, `blockEndBeats_ < blockStartBeats_` (position jumped backward) — this signals that the block spans a loop boundary, but **Engine is responsible for reconstructing the two sub-ranges** (pre-wrap and post-wrap) and feeding them independently to EventScheduler and ClockDispatch:

- **EventScheduler**: Engine calls `retrieve()` twice — once for `[blockStartBeats_, loopEndBeats_)` and once for `[loopStartBeats_, blockEndBeats_)`.
- **ClockDispatch**: Engine pushes two `BeatRangeUpdate`s to the SPSC queue — one per sub-range.

Transport provides `blockStartBeats_`, `blockEndBeats_`, `didLoopWrap()`, `getLoopStartBeats()`, and `getLoopEndBeats()`. Engine does the splitting.

## Plugin Wiring

Engine calls `pluginProcessor->getJuceProcessor()->setPlayHead(&transport_)` when adding a source with a plugin generator. This gives hosted VST/AU plugins access to tempo, position, and loop info. Transport already implements `AudioPlayHead` — no additional work beyond the `setPlayHead` call in Engine.

## Invariants

- `advance()` increases `positionInSamples_` by exactly `numSamples` when playing (before loop wrap)
- When looping, position after `advance()` is always within [loopStartSamples, loopEndSamples)
- `advance()` does not change position when stopped or paused
- Beat and bar positions are always derived from `positionInSamples_`, never stored or set independently
- Loop boundaries in samples are always consistent with the beat-domain values, tempo, and sample rate — recomputed on every change to those inputs
- `advance()` performs no floating-point-to-integer conversion for loop logic
- `stop()` always resets `positionInSamples_` to 0
- `pause()` never changes `positionInSamples_`
- Redundant transitions are no-ops: `play()` when playing, `stop()` when stopped, `pause()` when paused or stopped
- Tempo is clamped to [1.0, 999.0]
- Time signature numerator clamped to [1, 32]; denominator must be a power of 2 in {1, 2, 4, 8, 16, 32} — invalid values are ignored (no change)
- Loop end must be greater than loop start; `setLoopPoints()` enforces this (in both beat and sample domains)
- Loop length in samples must be >= `blockSize_`. Enforced at `setLoopPoints()`, and re-checked when tempo, sample rate, or block size changes
- `setLooping(true)` with no valid loop points (both 0) has no effect — looping stays disabled
- `didLoopWrap_` is reset to false at the start of each `advance()`
- Before `prepare()` is called, derived positions that depend on sample rate return 0

## Error Conditions

- `setTempo(bpm)` with out-of-range value: clamped silently
- `setTimeSignature(n, d)` with invalid denominator: no change, no error
- `setPositionInSamples(n)` with negative value: clamped to 0
- `advance(n)` with n <= 0: no-op
- `setLoopPoints(start, end)` with end <= start: no change, no error
- `setLoopPoints(start, end)` where cached sample length < `blockSize_`: no change, no error
- `setTempo()` or `prepare()` shrinks an active loop below minimum: looping auto-disabled, beat-domain points preserved, warning logged
- `setLooping(true)` when loop points are both 0: silently stays disabled
- `getPositionInSeconds()` / `getPositionInBeats()` before `prepare()`: return 0.0

## C ABI

Existing stubs cover state control. One new query function is needed.

```c
// State control (control thread → CommandQueue → audio thread)
void   sq_transport_play(SqEngine engine);
void   sq_transport_stop(SqEngine engine);
void   sq_transport_pause(SqEngine engine);
void   sq_transport_set_tempo(SqEngine engine, double bpm);
void   sq_transport_set_time_signature(SqEngine engine, int numerator, int denominator);
void   sq_transport_seek_samples(SqEngine engine, int64_t samples);
void   sq_transport_seek_beats(SqEngine engine, double beats);
void   sq_transport_set_loop_points(SqEngine engine, double start_beats, double end_beats);
void   sq_transport_set_looping(SqEngine engine, bool enabled);

// Queries (control thread)
double sq_transport_position(SqEngine engine);          // beats
double sq_transport_tempo(SqEngine engine);             // BPM
bool   sq_transport_is_playing(SqEngine engine);
bool   sq_transport_is_looping(SqEngine engine);        // NEW
```

### Cross-thread query mechanism

Transport lives on the audio thread. FFI query functions are called from the control thread. Engine bridges this:

- **Tempo, time signature, looping, loop points**: Engine keeps shadow copies on the control thread, updated under `controlMutex_` when the setter is called (before sending the command). Queries return the shadow value — immediately consistent with the most recent set.
- **Position**: Continuously changes on the audio thread. Engine maintains `std::atomic<int64_t> publishedPositionSamples_`, updated by the audio thread after `advance()`. The control thread reads the atomic and derives beats.
- **Playing state**: Changes on the audio thread in response to commands. Engine maintains `std::atomic<int> publishedState_`, updated by the audio thread in `handleCommand`.

This keeps Transport simple (no atomics, no synchronization) and puts the cross-thread concern in Engine where it belongs.

## Python API

Already exists in `python/squeeze/transport.py`. The `looping` getter needs updating to use `sq_transport_is_looping` instead of the current `return False` placeholder.

## Does NOT Handle

- Tempo automation / tempo maps (future)
- MIDI clock output (separate component)
- Sub-block splitting on loop wrap — Engine's responsibility, not Transport's
- Beat/bar crossing detection or callbacks (ClockDispatch handles this)
- Recording state
- Event emission — Transport is pure state

## Dependencies

- `juce::AudioPlayHead` (from `juce_audio_basics`)

## Thread Safety

Transport has **no internal synchronization**. All methods are called from the audio thread. Thread safety across threads is the caller's (Engine's) responsibility:

- **Mutations** (`play`, `stop`, `setTempo`, etc.) arrive on the audio thread via Engine's `handleCommand`, which processes CommandQueue entries at the top of `processBlock` before `advance()`.
- **Control-thread queries** are handled by Engine's shadow state and atomics (see C ABI section above). Transport itself is never read directly from other threads.

## Example Usage

```cpp
Transport transport;
transport.prepare(44100.0, 512);
transport.setTempo(120.0);
transport.setTimeSignature(4, 4);

// Before playback
assert(transport.getState() == TransportState::stopped);
assert(transport.getPositionInSamples() == 0);

// Start and advance one block
transport.play();
transport.advance(512);

assert(transport.isPlaying());
assert(transport.getPositionInSamples() == 512);
// positionInBeats = (512 / 44100) * (120 / 60) = 0.02322...

// Pause preserves position
transport.pause();
transport.advance(512);
assert(transport.getPositionInSamples() == 512);  // unchanged

// Resume
transport.play();
transport.advance(512);
assert(transport.getPositionInSamples() == 1024);

// Stop resets to 0
transport.stop();
assert(transport.getPositionInSamples() == 0);

// Seek by beats
transport.setPositionInBeats(4.0);   // beat 4 = start of bar 2 in 4/4
// positionInSamples = round(4.0 * 60.0 / 120.0 * 44100) = 88200
assert(transport.getPositionInSamples() == 88200);
assert(transport.getBarCount() == 1);  // 0-based: bar 0 done, now in bar 1

// Looping: loop beats 0–16 (4 bars in 4/4)
transport.setLoopPoints(0.0, 16.0);
transport.setLooping(true);
transport.setPositionInBeats(15.9);
transport.play();
transport.advance(44100);   // 1 second = 2 beats at 120 BPM
// 15.9 + 2.0 = 17.9, wraps: 0.0 + (17.9 - 0.0) % 16.0 = 1.9
assert(transport.getPositionInBeats() == Approx(1.9));
assert(transport.didLoopWrap());

// Block range
assert(transport.getBlockStartBeats() == Approx(15.9));
assert(transport.getBlockEndBeats() == Approx(1.9));  // < start → loop wrapped

// AudioPlayHead for plugins
auto pos = transport.getPosition();
assert(pos.has_value());
assert(*pos->getBpm() == 120.0);
assert(pos->getIsLooping());
assert(pos->getLoopPoints()->ppqStart == 0.0);
assert(pos->getLoopPoints()->ppqEnd == 16.0);

// Tempo change recomputes cached loop sample boundaries
transport.setTempo(60.0);
// loopEndSamples_ is now beatsToSamples(16.0) at 60 BPM = 16 * 60/60 * 44100 = 705600
// Loop behavior in advance() still uses pure integer comparison
```
