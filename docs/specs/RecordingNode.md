# RecordingNode Specification

## Responsibilities
- Provide a test-only node that records everything the audio thread delivers to it: MIDI events with sample offsets, parameter changes with call order, and per-sample audio content
- Enable sample-accurate verification of EventScheduler dispatch, sub-block parameter splitting, and MIDI routing
- Pass audio and MIDI through transparently so downstream nodes (including OutputNode) are unaffected

## Overview

RecordingNode is a test utility, not a production component. It sits in the graph like any other node — typically connected directly upstream of OutputNode — and records exactly what the engine delivers during `process()` and `setParameter()` calls. Tests inspect the recordings after `processBlock` to verify sample-accurate behavior.

RecordingNode is the primary tool for testing EventScheduler integration. Because EventScheduler resolves beat-timestamped events to sample offsets, and Engine dispatches those events as MIDI messages at specific sample positions or as sub-block-split parameter changes, a node that records *when* things arrive is the only way to verify correctness without inspecting audio waveforms.

### Why not mock the OutputNode?

OutputNode is a terminal sink with a no-op `process()`. Engine reads its `inputAudio` buffer directly after the processing loop — it never calls OutputNode's `process()`. Replacing it with a mock would require changes to Engine internals. RecordingNode avoids this: it's a regular graph node that records everything through the standard `process()` / `setParameter()` contract.

## Interface

```cpp
namespace squeeze {

class RecordingNode : public Node {
public:
    // --- Recorded data (read by tests after processBlock) ---

    struct MidiEvent {
        int sampleOffset;       // position within the block
        int blockIndex;         // which process() call (0-based, across all calls)
        int subBlockIndex;      // which sub-block within a split block
        juce::MidiMessage message;
    };

    struct ParamChange {
        std::string name;
        float value;
        int callIndex;          // sequential call order (0-based)
        int blockIndex;         // which process() call this change preceded
    };

    struct ProcessCall {
        int blockIndex;
        int numSamples;         // may be < blockSize if sub-block split
    };

    std::vector<MidiEvent> midiEvents;
    std::vector<ParamChange> paramChanges;
    std::vector<ProcessCall> processCalls;

    // Per-sample audio capture (optional, off by default)
    bool captureAudio = false;
    std::vector<std::vector<float>> capturedAudio; // [channel][sample], appended each process()

    int blockIndex_ = 0;

    // --- Node interface ---

    void prepare(double sampleRate, int blockSize) override;
    void release() override;
    void process(ProcessContext& context) override;

    std::vector<PortDescriptor> getInputPorts() const override;
    std::vector<PortDescriptor> getOutputPorts() const override;

    // Parameters — records all calls, stores current values
    std::vector<ParameterDescriptor> getParameterDescriptors() const override;
    float getParameter(const std::string& name) const override;
    void setParameter(const std::string& name, float value) override;

    // --- Test helpers ---

    void clearRecordings();

    // Query helpers for common test assertions
    int countMidiEventsOfType(int statusByte) const;
    int countNoteOns() const;
    int countNoteOffs() const;
    std::vector<MidiEvent> midiEventsAtOffset(int sampleOffset) const;
    std::vector<ParamChange> paramChangesByName(const std::string& name) const;
};

} // namespace squeeze
```

## Ports

| Port | Direction | Signal | Channels |
|------|-----------|--------|----------|
| `"in"` | input | audio | 2 |
| `"out"` | output | audio | 2 |
| `"midi_in"` | input | midi | — |
| `"midi_out"` | output | midi | — |

RecordingNode has both audio and MIDI ports so it can be placed anywhere in a chain and record both signal types.

## Parameters

RecordingNode declares two dummy automatable parameters for testing parameter scheduling:

| Name | Default | Range | Label | Automatable |
|------|---------|-------|-------|-------------|
| `"alpha"` | 0.0 | 0.0–1.0 | "" | yes |
| `"beta"` | 0.0 | 0.0–1.0 | "" | yes |

These parameters have no effect on audio. They exist solely so that `sq_schedule_param_change` has a valid target and `setParameter` calls can be recorded and verified. Tests can assert that `setParameter("alpha", 0.7)` was called at a specific point in the sub-block sequence.

Adding more test parameters in the future (e.g. stepped, boolean) is trivial but not needed yet.

## Behavior

### `prepare(sampleRate, blockSize)`

Stores sample rate and block size. Calls `clearRecordings()`.

### `release()`

No-op.

### `process(context)`

1. Record a `ProcessCall{blockIndex_, context.numSamples}` into `processCalls`.
2. Iterate `context.inputMidi`: for each MIDI message, record `MidiEvent{samplePosition, blockIndex_, subBlockIndex, message}` into `midiEvents`.
3. If `captureAudio` is true: for each channel, append samples from `context.inputAudio` to `capturedAudio[channel]`.
4. Copy `inputAudio` to `outputAudio` (channel-by-channel, `context.numSamples` samples).
5. Copy `inputMidi` to `outputMidi` (add all messages).
6. Increment `blockIndex_`.

All operations are O(n) where n = numSamples or number of MIDI events. No allocation on the audio thread in the normal case — vectors are pre-reserved in `prepare()` or by the test before calling `processBlock`. In test-only usage (headless, no real-time constraint), the allocation from vector growth is acceptable.

### `setParameter(name, value)`

1. Record `ParamChange{name, value, callIndex, blockIndex_}` into `paramChanges` where `callIndex` is the running count of all `setParameter` calls.
2. Store `value` in an internal map for `getParameter` retrieval.

`setParameter` may be called from the control thread (immediate changes) or between sub-blocks on the audio thread (scheduled parameter changes via EventScheduler). The `blockIndex_` recorded tells the test which process() call the change preceded.

### `clearRecordings()`

Clears `midiEvents`, `paramChanges`, `processCalls`, `capturedAudio`. Resets `blockIndex_` to 0 and the `setParameter` call counter to 0. Does not reset stored parameter values.

## Invariants

- Audio passthrough is bit-exact: `outputAudio` == `inputAudio` for all channels and samples
- MIDI passthrough is complete: every message in `inputMidi` appears in `outputMidi` at the same sample offset
- Every `process()` call increments `blockIndex_` exactly once
- `MidiEvent.sampleOffset` faithfully reflects `juce::MidiMessageMetadata::samplePosition` — RecordingNode does not modify, reorder, or fabricate MIDI events
- `ParamChange.callIndex` is strictly increasing across all `setParameter` calls
- `ParamChange.blockIndex` matches the `blockIndex_` at the time `setParameter` was called, enabling tests to determine whether a parameter change happened before, between, or after specific `process()` calls

## Error Conditions

- None. RecordingNode is a passive recorder. Invalid parameter names are recorded as-is (no error). Unknown names return 0.0 from `getParameter`.

## Does NOT Handle

- Generating audio or MIDI — it is purely a passthrough and recorder
- Interpreting events — it records raw data; test assertions give it meaning
- Production use — not RT-safe due to vector allocations; test-only
- Audio analysis (FFT, level metering, etc.) — tests inspect raw samples directly

## Dependencies

- `Node` (base class)
- `juce::AudioBuffer`, `juce::MidiBuffer`, `juce::MidiMessage` (for audio/MIDI types)

No dependency on Engine, EventScheduler, Transport, or Graph.

## Thread Safety

RecordingNode is designed for single-threaded test use via `Engine::prepareForTesting()` + `Engine::processBlock()`, where both control and audio paths run on the test thread.

| Method | Thread | Notes |
|--------|--------|-------|
| `prepare()` / `release()` | Control (test) | Called once before processing |
| `process()` | Audio (test) | Called by Engine::processBlock |
| `setParameter()` | Control or Audio (test) | Called by Engine on control thread, or between sub-blocks on audio thread |
| `getParameter()` | Any (test) | Read-only, inspected after processBlock |
| `clearRecordings()` | Control (test) | Called between test cases |
| Recording vectors | Test thread | Inspected after processBlock returns |

Not safe for concurrent access. Not intended for real-time audio callbacks with actual audio devices.

## C ABI

```c
// Add a RecordingNode to the engine (test-only)
int sq_add_recording_node(SqEngine engine, const char* name);

// Query recordings after processBlock
int sq_recording_get_midi_event_count(SqEngine engine, int node_id);
int sq_recording_get_midi_event(SqEngine engine, int node_id, int index,
                                 int* sample_offset, int* status,
                                 int* data1, int* data2);

int sq_recording_get_param_change_count(SqEngine engine, int node_id);
int sq_recording_get_param_change(SqEngine engine, int node_id, int index,
                                   char* name_buf, int name_buf_size,
                                   float* value, int* call_index,
                                   int* block_index);

int sq_recording_get_process_call_count(SqEngine engine, int node_id);
int sq_recording_get_process_call(SqEngine engine, int node_id, int index,
                                   int* block_index, int* num_samples);

void sq_recording_clear(SqEngine engine, int node_id);
```

All `sq_recording_*` functions return `-1` if `node_id` does not refer to a RecordingNode.

## Python API

### Low-level (`_low_level.Squeeze`)

```python
def add_recording_node(self, name: str) -> int: ...
def recording_get_midi_event_count(self, node_id: int) -> int: ...
def recording_get_midi_event(self, node_id: int, index: int) -> tuple[int, int, int, int]: ...
    # returns (sample_offset, status, data1, data2)
def recording_get_param_change_count(self, node_id: int) -> int: ...
def recording_get_param_change(self, node_id: int, index: int) -> tuple[str, float, int, int]: ...
    # returns (name, value, call_index, block_index)
def recording_get_process_call_count(self, node_id: int) -> int: ...
def recording_get_process_call(self, node_id: int, index: int) -> tuple[int, int]: ...
    # returns (block_index, num_samples)
def recording_clear(self, node_id: int) -> None: ...
```

### High-level

```python
class RecordingNode(Node):
    """Test-only node that records MIDI events, parameter changes, and process calls."""

    @property
    def midi_events(self) -> list[RecordedMidiEvent]: ...

    @property
    def param_changes(self) -> list[RecordedParamChange]: ...

    @property
    def process_calls(self) -> list[RecordedProcessCall]: ...

    def clear(self) -> None: ...

# Dataclasses
@dataclass
class RecordedMidiEvent:
    sample_offset: int
    status: int
    data1: int
    data2: int

@dataclass
class RecordedParamChange:
    name: str
    value: float
    call_index: int
    block_index: int

@dataclass
class RecordedProcessCall:
    block_index: int
    num_samples: int
```

## Example Usage

### C++ test: verify MIDI event sample offset

```cpp
Engine engine;
engine.prepareForTesting(44100.0, 512);

auto recorder = std::make_unique<RecordingNode>();
auto* rec = recorder.get();
int recId = engine.addNode("rec", std::move(recorder));

engine.connect(recId, "out", engine.getOutputNodeId(), "in", error);
engine.render(512); // install snapshot

// Schedule a note-on at beat 1.0 (at 120 BPM, 44100 Hz)
// Beat 1.0 at 120 BPM = 0.5 seconds = 22050 samples from origin
engine.scheduleNoteOn(recId, /*beat=*/1.0, /*ch=*/1, /*note=*/60, /*vel=*/0.8f);

// Start transport at beat 0.0, process one 512-sample block
engine.transportPlay();
engine.render(512);

float* out[2] = { ... };
engine.processBlock(out, 2, 512);

// Beat 1.0 is at sample 22050, which is far beyond this 512-sample block,
// so no events yet
REQUIRE(rec->midiEvents.empty());

// Fast-forward to the block containing beat 1.0 and process again
// ... (advance transport, process blocks until beat 1.0 is in range)

// When the right block arrives:
REQUIRE(rec->midiEvents.size() == 1);
REQUIRE(rec->midiEvents[0].message.isNoteOn());
REQUIRE(rec->midiEvents[0].message.getNoteNumber() == 60);
CHECK(rec->midiEvents[0].sampleOffset == expectedOffset);
```

### C++ test: verify sub-block parameter splitting

```cpp
Engine engine;
engine.prepareForTesting(44100.0, 512);

auto recorder = std::make_unique<RecordingNode>();
auto* rec = recorder.get();
int recId = engine.addNode("rec", std::move(recorder));
engine.connect(recId, "out", engine.getOutputNodeId(), "in", error);
engine.render(512);

// Schedule a parameter change mid-block
// At 120 BPM, 44100 Hz: beat 0.25 = sample 5512.5 ≈ sample 5513
engine.scheduleParamChange(recId, /*beat=*/0.25, "alpha", 0.75f);

engine.transportPlay();
engine.render(512);

float* out[2] = { ... };
engine.processBlock(out, 2, 512);

// If Engine does sub-block splitting, process() should have been called twice:
// once for samples [0, splitPoint), then setParameter, then [splitPoint, 512)
REQUIRE(rec->processCalls.size() == 2);
CHECK(rec->processCalls[0].numSamples + rec->processCalls[1].numSamples == 512);

REQUIRE(rec->paramChanges.size() == 1);
CHECK(rec->paramChanges[0].name == "alpha");
CHECK(rec->paramChanges[0].value == Catch::Approx(0.75f));
// paramChange happened between process call 0 and process call 1
CHECK(rec->paramChanges[0].blockIndex == 1);
```

### Python test

```python
def test_midi_sample_offset(engine):
    rec_id = engine.add_recording_node("rec")
    engine.connect(rec_id, "out", engine.output_node_id, "in")
    engine.render(512)

    engine.schedule_note_on(rec_id, beat_time=0.0, channel=1, note=60, velocity=0.8)
    engine.transport_play()
    engine.render(512)
    engine.process_block(512)

    rec = engine.get_recording_node(rec_id)
    assert len(rec.midi_events) == 1
    assert rec.midi_events[0].sample_offset == 0
    assert rec.midi_events[0].data1 == 60  # note number
```
