# RecordingProcessor Specification

## Responsibilities

- Provide a test-only processor that records everything the audio thread delivers: MIDI events with sample offsets, parameter changes with call order, and per-sample audio content
- Enable sample-accurate verification of EventScheduler dispatch, sub-block parameter splitting, and MIDI routing
- Pass audio through transparently (in-place — buffer is unchanged)

## Overview

RecordingProcessor is a test utility, not a production component. It sits in a Source chain or Bus chain and records exactly what the engine delivers during `process()` and `setParameter()` calls. Tests inspect the recordings after `processBlock` to verify sample-accurate behavior.

RecordingProcessor is the primary tool for testing EventScheduler integration. Because EventScheduler resolves beat-timestamped events to sample offsets, and Engine dispatches those events as MIDI messages at specific sample positions or as sub-block-split parameter changes, a processor that records *when* things arrive is the only way to verify correctness without inspecting audio waveforms.

Unlike the old RecordingNode, RecordingProcessor is an in-place processor with no ports. It simply passes audio through and records what it sees.

## Interface

```cpp
namespace squeeze {

class RecordingProcessor : public Processor {
public:
    RecordingProcessor();

    // --- Recorded data (read by tests after processBlock) ---

    struct MidiEvent {
        int sampleOffset;
        int blockIndex;
        juce::MidiMessage message;
    };

    struct ParamChange {
        std::string name;
        float value;
        int callIndex;
        int blockIndex;
    };

    struct ProcessCall {
        int blockIndex;
        int numSamples;
    };

    std::vector<MidiEvent> midiEvents;
    std::vector<ParamChange> paramChanges;
    std::vector<ProcessCall> processCalls;

    // Per-sample audio capture (optional, off by default)
    bool captureAudio = false;
    std::vector<std::vector<float>> capturedAudio; // [channel][sample]

    int blockIndex = 0;

    // --- Processor interface ---

    void prepare(double sampleRate, int blockSize) override;
    void release() override;
    void process(juce::AudioBuffer<float>& buffer) override;
    void process(juce::AudioBuffer<float>& buffer,
                 const juce::MidiBuffer& midi) override;

    // Parameters — records all calls, stores current values
    int getParameterCount() const override;
    std::vector<ParamDescriptor> getParameterDescriptors() const override;
    float getParameter(const std::string& name) const override;
    void setParameter(const std::string& name, float value) override;

    // --- Test helpers ---

    void clearRecordings();

    int countMidiEventsOfType(int statusByte) const;
    int countNoteOns() const;
    int countNoteOffs() const;
    std::vector<MidiEvent> midiEventsAtOffset(int sampleOffset) const;
    std::vector<ParamChange> paramChangesByName(const std::string& name) const;
};

} // namespace squeeze
```

## Parameters

RecordingProcessor declares two dummy automatable parameters for testing parameter scheduling:

| Name | Default | Range | Label | Automatable |
|------|---------|-------|-------|-------------|
| `"alpha"` | 0.0 | 0.0–1.0 | "" | yes |
| `"beta"` | 0.0 | 0.0–1.0 | "" | yes |

These parameters have no effect on audio. They exist solely so that `sq_schedule_param_change` has a valid target and `setParameter` calls can be recorded and verified.

## Behavior

### `prepare(sampleRate, blockSize)`

Stores sample rate and block size. Calls `clearRecordings()`.

### `release()`

No-op.

### `process(buffer)`

Audio-only variant:
1. Record a `ProcessCall{blockIndex, buffer.getNumSamples()}` into `processCalls`.
2. If `captureAudio` is true: for each channel, append samples from buffer to `capturedAudio[channel]`.
3. **Audio passes through unchanged** (in-place, no modification).
4. Increment `blockIndex`.

### `process(buffer, midi)`

MIDI variant:
1. Record a `ProcessCall{blockIndex, buffer.getNumSamples()}` into `processCalls`.
2. Iterate `midi`: for each MIDI message, record `MidiEvent{samplePosition, blockIndex, message}` into `midiEvents`.
3. If `captureAudio` is true: for each channel, append samples from buffer to `capturedAudio[channel]`.
4. **Audio passes through unchanged** (in-place, no modification).
5. Increment `blockIndex`.

### `setParameter(name, value)`

1. Record `ParamChange{name, value, callIndex, blockIndex}` into `paramChanges`.
2. Store `value` in an internal map for `getParameter` retrieval.

### `clearRecordings()`

Clears `midiEvents`, `paramChanges`, `processCalls`, `capturedAudio`. Resets `blockIndex` to 0 and the `setParameter` call counter to 0. Does not reset stored parameter values.

## Invariants

- Audio passthrough is bit-exact: buffer is not modified
- Every `process()` call increments `blockIndex` exactly once
- `MidiEvent.sampleOffset` faithfully reflects `juce::MidiMessageMetadata::samplePosition`
- `ParamChange.callIndex` is strictly increasing across all `setParameter` calls
- `ParamChange.blockIndex` matches the `blockIndex` at the time `setParameter` was called

## Error Conditions

- None. RecordingProcessor is a passive recorder. Invalid parameter names are recorded as-is. Unknown names return 0.0 from `getParameter`.

## Does NOT Handle

- Generating audio or MIDI — purely a passthrough and recorder
- Production use — not RT-safe due to vector allocations; test-only
- Audio analysis — tests inspect raw samples directly

## Dependencies

- `Processor` (base class)
- `juce::AudioBuffer`, `juce::MidiBuffer`, `juce::MidiMessage`

## Thread Safety

RecordingProcessor is designed for single-threaded test use via `Engine::render()`, where both control and audio paths run on the test thread.

| Method | Thread | Notes |
|--------|--------|-------|
| `prepare()` / `release()` | Control (test) | Called once before processing |
| `process()` | Audio (test) | Called by Engine::processBlock |
| `setParameter()` | Control or Audio (test) | Between sub-blocks on audio thread |
| `getParameter()` | Any (test) | Read-only |
| `clearRecordings()` | Control (test) | Called between test cases |

Not safe for concurrent access. Not intended for real-time audio callbacks.

## C ABI

```c
// Add a RecordingProcessor to a source chain (test-only)
SqProc sq_add_recording_processor(SqEngine engine, SqSource src);

// Query recordings after processBlock
int sq_recording_get_midi_event_count(SqEngine engine, SqProc proc);
int sq_recording_get_midi_event(SqEngine engine, SqProc proc, int index,
                                 int* sample_offset, int* status,
                                 int* data1, int* data2);

int sq_recording_get_param_change_count(SqEngine engine, SqProc proc);
int sq_recording_get_param_change(SqEngine engine, SqProc proc, int index,
                                   char* name_buf, int name_buf_size,
                                   float* value, int* call_index,
                                   int* block_index);

int sq_recording_get_process_call_count(SqEngine engine, SqProc proc);
int sq_recording_get_process_call(SqEngine engine, SqProc proc, int index,
                                   int* block_index, int* num_samples);

void sq_recording_clear(SqEngine engine, SqProc proc);
```

All `sq_recording_*` functions return `-1` if `proc` does not refer to a RecordingProcessor.

## Example Usage

### C++ test: verify MIDI event sample offset

```cpp
Engine engine(44100.0, 512);

auto gen = std::make_unique<TestSynthProcessor>();
auto* src = engine.addSource("synth", std::move(gen));

auto rec = std::make_unique<RecordingProcessor>();
auto* recorder = rec.get();
src->getChain().append(rec.release());

engine.render(512);  // install snapshot

// Schedule a note-on
engine.scheduleNoteOn(src, 0.0, 1, 60, 0.8f);
engine.transportPlay();
engine.render(512);

REQUIRE(recorder->midiEvents.size() == 1);
REQUIRE(recorder->midiEvents[0].message.isNoteOn());
REQUIRE(recorder->midiEvents[0].message.getNoteNumber() == 60);
```

### C++ test: verify sub-block parameter splitting

```cpp
Engine engine(44100.0, 512);

auto gen = std::make_unique<TestSynthProcessor>();
auto* src = engine.addSource("synth", std::move(gen));

auto rec = std::make_unique<RecordingProcessor>();
auto* recorder = rec.get();
src->getChain().append(rec.release());

engine.render(512);

// Schedule a parameter change mid-block
engine.scheduleParamChange(recorder->getHandle(), 0.25, "alpha", 0.75f);
engine.transportPlay();
engine.render(512);

// process() should have been called twice (sub-block split)
REQUIRE(recorder->processCalls.size() == 2);
CHECK(recorder->processCalls[0].numSamples + recorder->processCalls[1].numSamples == 512);

REQUIRE(recorder->paramChanges.size() == 1);
CHECK(recorder->paramChanges[0].name == "alpha");
CHECK(recorder->paramChanges[0].value == Catch::Approx(0.75f));
```
