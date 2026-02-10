# Transport Specification

## Responsibilities

- Track playback state (stopped, playing, paused)
- Maintain sample-accurate position (ground truth in samples)
- Derive musical position (PPQ / quarter-note beats, bars) from sample position and tempo
- Store tempo (BPM) and time signature
- Implement `juce::AudioPlayHead` so hosted plugins receive tempo and position info
- Optionally loop playback between two beat positions

## Types

```cpp
enum class TransportState { stopped, playing, paused };
```

Time signature uses `juce::AudioPlayHead::TimeSignature` directly (numerator, denominator, both int, default 4/4).

## Interface

### Construction

```cpp
Transport();  // stopped, position 0, 120 BPM, 4/4
```

### Audio-thread method

```cpp
void advance(int numSamples);
```

Called once per `processBlock`. Advances `positionInSamples_` by `numSamples` when state is `playing`. No-op when stopped or paused. No-op when `numSamples <= 0`.

When looping is enabled and the advance crosses the loop end point, position wraps to the loop start:

```
positionInSamples_ += numSamples;
if (looping_) {
    int64_t loopStart = beatsToSamples(loopStartBeats_);
    int64_t loopEnd   = beatsToSamples(loopEndBeats_);
    if (positionInSamples_ >= loopEnd) {
        int64_t loopLen = loopEnd - loopStart;
        positionInSamples_ = loopStart
            + ((positionInSamples_ - loopStart) % loopLen);
    }
}
```

The sample positions are computed inline from the beat values, tempo, and sample rate. A couple of FP ops once per block is negligible.

### State control

These are called on the audio thread, typically via Scheduler commands executed at the top of `processBlock` (before `advance`).

```cpp
void play();    // → playing (from stopped or paused)
void stop();    // → stopped, resets positionInSamples_ to 0
void pause();   // → paused, position preserved

void setTempo(double bpm);
void setTimeSignature(int numerator, int denominator);
void setPositionInSamples(int64_t samples);
void setPositionInBeats(double beats);  // converts to samples via tempo + sampleRate

void setLoopPoints(double startBeats, double endBeats);  // in PPQ (quarter notes)
void setLooping(bool enabled);

void setSampleRate(double sr);  // called from Engine::audioDeviceAboutToStart / prepareForTesting
```

### Queries

All const, all lock-free (plain member reads — no atomics needed since everything runs on one thread).

```cpp
TransportState getState() const;
bool isPlaying() const;           // state == playing

double getTempo() const;          // BPM
juce::AudioPlayHead::TimeSignature getTimeSignature() const;
double getSampleRate() const;

int64_t getPositionInSamples() const;
double getPositionInSeconds() const;   // positionInSamples / sampleRate
double getPositionInBeats() const;     // PPQ (quarter-note position)
int64_t getBarCount() const;           // 0-based, how many complete bars elapsed
double getPpqOfLastBarStart() const;   // PPQ position where current bar began

bool isLooping() const;
double getLoopStartBeats() const;      // PPQ
double getLoopEndBeats() const;        // PPQ
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

Used by `setPositionInBeats()` and the loop wrap logic in `advance()`:

```
beatsToSamples(beats) = round(beats * 60.0 / tempo * sampleRate)
```

## Invariants

- `advance()` increases `positionInSamples_` by exactly `numSamples` when playing (before loop wrap)
- When looping, position after `advance()` is always within [loopStart, loopEnd)
- `advance()` does not change position when stopped or paused
- Beat and bar positions are always derived from `positionInSamples_`, never stored or set independently
- `stop()` always resets `positionInSamples_` to 0
- `pause()` never changes `positionInSamples_`
- Tempo is clamped to [1.0, 999.0]
- Time signature numerator clamped to [1, 32]; denominator must be a power of 2 in {1, 2, 4, 8, 16, 32} — invalid values are ignored (no change)
- Loop end must be greater than loop start; `setLoopPoints()` enforces this
- `setLooping(true)` with no loop points set (both 0) has no effect — looping stays disabled
- Before `setSampleRate()` is called, derived positions that depend on sample rate return 0

## Error Conditions

- `setTempo(bpm)` with out-of-range value: clamped silently
- `setTimeSignature(n, d)` with invalid denominator: no change, no error
- `setPositionInSamples(n)` with negative value: clamped to 0
- `advance(n)` with n <= 0: no-op
- `setLoopPoints(start, end)` with end <= start: no change, no error
- `setLooping(true)` when loop points are both 0: silently stays disabled
- `getPosition()` before `setSampleRate()`: returns PositionInfo with `timeInSamples` set, musical fields empty

## Loop Wrap Detection

```cpp
bool didLoopWrap() const;
```

Returns true if the most recent `advance()` call caused the position to wrap around the loop boundary. Reset to false at the start of each `advance()`. This flag is read by Engine to pass the `looped` parameter to `EventQueue::retrieve()`.

Also provides the pre-wrap position for computing the block's beat range:

```cpp
double getBlockStartBeats() const;
double getBlockEndBeats() const;
```

`getBlockStartBeats()` returns the position *before* the most recent `advance()` (in beats). `getBlockEndBeats()` returns the position *after* advance (in beats). These are stored by `advance()` and read by Engine to pass to `EventQueue::retrieve()`. When a loop wrap occurred, `blockEndBeats < blockStartBeats` (the position jumped backward), which is how EventQueue detects the discontinuity.

## PluginNode Wiring

Engine calls `pluginNode->getProcessor()->setPlayHead(&transport_)` when adding a PluginNode. This gives hosted VST/AU plugins access to tempo, position, and loop info via `juce::AudioPlayHead::getPosition()`. Transport already implements the `AudioPlayHead` interface — no additional work needed beyond the `setPlayHead` call in Engine.

## Does NOT Handle

- Tempo automation / tempo maps (future)
- MIDI clock output (separate component)
- Beat/bar crossing detection or callbacks (future, higher-level concern)
- Recording state
- Event emission of any kind — this is pure state

## Dependencies

- `juce::AudioPlayHead` (from `juce_audio_basics`)

## Thread Safety

Transport has **no internal synchronization**. All methods are called from the audio thread (or sequentially during setup before audio starts). Thread safety across threads is the caller's responsibility:

- **Mutations** (`play`, `stop`, `setTempo`, etc.) reach Transport via Scheduler commands, executed at the top of `processBlock` before `advance()`.
- **Reading from Lua/UI** is handled by Engine publishing a snapshot of transport state (e.g., via SeqLock or atomics). Transport itself is not read directly from other threads.

## Example Usage

```cpp
Transport transport;
transport.setSampleRate(44100.0);
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

// Looping: loop bars 1-4 (beats 0-16 in 4/4 at any tempo)
transport.setLoopPoints(0.0, 16.0);   // PPQ: 0 to 16 quarter notes = 4 bars
transport.setLooping(true);
transport.setPositionInBeats(15.9);    // near end of bar 4
transport.play();
transport.advance(44100);              // advance 1 second = 2 beats at 120 BPM
// position would have been 15.9 + 2.0 = 17.9, but wraps:
// 17.9 >= 16.0, so position = 0.0 + (17.9 - 16.0) = 1.9 beats
assert(transport.getPositionInBeats() == Approx(1.9));

// AudioPlayHead for plugins
auto pos = transport.getPosition();
assert(pos.has_value());
assert(*pos->getBpm() == 120.0);
assert(pos->getIsLooping());
assert(pos->getLoopPoints()->ppqStart == 0.0);
assert(pos->getLoopPoints()->ppqEnd == 16.0);
```
