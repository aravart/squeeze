# MidiSplitterNode Specification

## Responsibilities
- Receive MIDI on a single input and distribute to multiple named outputs based on filter rules
- Filter by MIDI channel and/or note range per output
- Support dynamic output ports (add/remove outputs at runtime on the control thread)
- Operate as a regular graph node — usable inside GroupNodes or at the top level

## Overview

MidiSplitterNode is a graph-level MIDI routing node. It receives MIDI on one input and distributes messages to named outputs, each with its own filter. It is the primary mechanism for MIDI fan-out inside GroupNodes (e.g., routing one MIDI input to multiple samplers in a drum kit) but is equally useful at the top level.

MidiSplitterNode is distinct from MidiRouter (which manages external MIDI device → node routing). MidiSplitterNode operates within the audio graph, routing MIDI between nodes.

```
                          ┌──────────────────┐
                          │ MidiSplitterNode  │
  midi_in ──────────────→ │                  │
                          │  ch=0  n=36-47  ─┼──→ "kick"
                          │  ch=0  n=48-59  ─┼──→ "snare"
                          │  ch=0  n=60-71  ─┼──→ "hat"
                          └──────────────────┘
```

## Interface

```cpp
namespace squeeze {

struct MidiFilter {
    int channel;    // 0 = all channels, 1-16 = specific channel
    int noteLow;    // 0-127 inclusive range start
    int noteHigh;   // 0-127 inclusive range end
};

class MidiSplitterNode : public Node {
public:
    explicit MidiSplitterNode(const std::string& name);
    ~MidiSplitterNode() override;

    // --- Output management (control thread) ---
    bool addOutput(const std::string& portName, const MidiFilter& filter,
                   std::string& error);
    bool removeOutput(const std::string& portName);
    bool setFilter(const std::string& portName, const MidiFilter& filter);
    MidiFilter getFilter(const std::string& portName) const;
    std::vector<std::string> getOutputNames() const;

    // --- Node interface ---
    void prepare(double sampleRate, int blockSize) override;
    void process(ProcessContext& context) override;
    void release() override;
    std::vector<PortDescriptor> getInputPorts() const override;
    std::vector<PortDescriptor> getOutputPorts() const override;

    // No parameters — configuration is via addOutput/setFilter
    // (ParameterDescriptor defaults are sufficient)

    // --- Per-port output buffer access (for Engine routing) ---
    juce::MidiBuffer& getOutputBuffer(const std::string& portName);
};

} // namespace squeeze
```

## MidiFilter

| Field | Range | Default | Description |
|-------|-------|---------|-------------|
| `channel` | 0-16 | 0 | 0 = all channels. 1-16 = specific MIDI channel. System messages (channel 0) always pass regardless of filter. |
| `noteLow` | 0-127 | 0 | Lower bound of note range (inclusive) |
| `noteHigh` | 0-127 | 127 | Upper bound of note range (inclusive) |

Default filter `{0, 0, 127}` passes everything.

### Filter logic

Note range filtering applies only to note-on, note-off, and polyphonic aftertouch messages. All other message types (CC, pitch bend, channel pressure, program change) pass through based on the channel filter alone.

A message can match multiple outputs if their filters overlap. The message is copied to each matching output.

Messages matching no output are silently dropped.

```cpp
bool matches(const MidiFilter& f, const juce::MidiMessage& msg) {
    // Channel filter (system messages always pass)
    if (f.channel != 0 && msg.getChannel() != 0
        && msg.getChannel() != f.channel)
        return false;

    // Note range filter (note-on, note-off, poly aftertouch only)
    if (msg.isNoteOnOrOff() || msg.isAftertouch()) {
        int note = msg.getNoteNumber();
        if (note < f.noteLow || note > f.noteHigh)
            return false;
    }

    return true;
}
```

## Ports

### Input
- Always one: `{"midi_in", PortDirection::input, SignalType::midi, 1}`

### Outputs
- Dynamic. One MIDI output port per `addOutput()` call. Port name matches the output name.
- No outputs exist until `addOutput()` is called.
- `getOutputPorts()` returns the current set of output ports.

## Output Management

### `addOutput(portName, filter)`
- Creates a new MIDI output port with the given name and filter
- Allocates an internal `MidiBuffer` for this output
- `portName` must be unique among existing outputs
- Returns false if name is duplicate or empty

### `removeOutput(portName)`
- Removes the output port and its internal buffer
- Downstream connections are auto-disconnected by the Engine (cascade)
- Returns false if not found

### `setFilter(portName, filter)`
- Changes the filter on an existing output without removing/recreating it
- Does **not** require re-prepare (filter changes are lightweight)
- Returns false if output not found

### `getFilter(portName)`
- Returns the current filter for an output
- Returns `{0, 0, 127}` (pass-all) if not found

### Structural changes
Adding or removing outputs is a structural change — same invariant as GroupNode: `prepare()` must be called before the next `process()`. In practice, the Engine handles this via snapshot rebuild. Changing a filter via `setFilter()` is **not** a structural change and does not require re-prepare.

## Processing

### `process(context)`

RT-safe. For each message in `context.inputMidi`:

1. Check the message against each output's filter
2. Copy matching messages to the corresponding output's internal `MidiBuffer`
3. Clear all output buffers at the start of each `process()` call

The standard `context.outputMidi` is **not used** — MidiSplitterNode writes to per-port internal buffers instead. The Engine reads from these buffers via `getOutputBuffer(portName)` when routing MIDI connections from specific output ports.

### `prepare(sampleRate, blockSize)`

Pre-allocate internal `MidiBuffer` per output. Clear all buffers.

### `release()`

Free internal buffers.

## Invariants
- Always has exactly one MIDI input port `"midi_in"`
- Output ports are dynamic — zero or more, controlled by `addOutput()` / `removeOutput()`
- Adding/removing outputs is a structural change requiring re-prepare
- Changing a filter via `setFilter()` is not a structural change
- `process()` is RT-safe: no allocation, no blocking
- Filter matching uses the same semantics as MidiRouter (channel + note range, system messages bypass channel filter)
- A message can match multiple outputs (overlapping filters)

## Error Conditions
- `addOutput()` with empty name: returns false
- `addOutput()` with duplicate name: returns false
- `addOutput()` with `noteLow > noteHigh`: returns false
- `addOutput()` with `channel` outside 0-16: returns false
- `removeOutput()` with unknown name: returns false
- `setFilter()` with unknown name: returns false
- `getFilter()` with unknown name: returns default `{0, 0, 127}`

## Does NOT Handle
- External MIDI device management (MidiRouter)
- MIDI generation or transformation (it only routes/filters)
- CC filtering or velocity filtering (may be added later)
- Audio processing (MIDI-only node)

## Dependencies
- Node (implements Node interface)
- Port (PortDescriptor)
- JUCE (juce::MidiBuffer, juce::MidiMessage)

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| `addOutput()` / `removeOutput()` | Control | Structural change; requires Engine's controlMutex_ |
| `setFilter()` | Control | Lightweight; requires controlMutex_ but not re-prepare |
| `getFilter()` / `getOutputNames()` | Control | Query |
| `process()` | Audio | RT-safe |
| `getOutputBuffer()` | Audio | Called by Engine during snapshot processing |
| `prepare()` / `release()` | Control | |

## C ABI

```c
// Creation (top-level or in a group)
int sq_add_midi_splitter(SqEngine engine, const char* name, char** error);
int sq_group_add_midi_splitter(SqEngine engine, int group_id,
                                const char* name, char** error);

// Output management
bool sq_midi_splitter_add_output(SqEngine engine, int node_id,
                                  const char* port_name,
                                  int channel, int note_low, int note_high,
                                  char** error);
bool sq_midi_splitter_remove_output(SqEngine engine, int node_id,
                                     const char* port_name);
bool sq_midi_splitter_set_filter(SqEngine engine, int node_id,
                                  const char* port_name,
                                  int channel, int note_low, int note_high);
```

## Python API

```python
split = engine.add_midi_splitter("drum_split")
engine.midi_splitter_add_output(split, "kick",  channel=0, note_low=36, note_high=36)
engine.midi_splitter_add_output(split, "snare", channel=0, note_low=38, note_high=38)
engine.midi_splitter_add_output(split, "hat",   channel=0, note_low=42, note_high=46)

# Connect outputs to nodes
engine.connect(split, "kick",  kick_sampler,  "midi_in")
engine.connect(split, "snare", snare_sampler, "midi_in")
engine.connect(split, "hat",   hat_sampler,   "midi_in")
```

## Example: Drum Kit inside a GroupNode

```python
kit = engine.add_group("drums")

# Internal nodes
split = engine.group_add_midi_splitter(kit, "split")
kick  = engine.group_add_sampler(kit, "kick", 4)
snare = engine.group_add_sampler(kit, "snare", 4)
hat   = engine.group_add_sampler(kit, "hihat", 4)

# Configure splitter outputs
engine.midi_splitter_add_output(split, "kick",  channel=0, note_low=36, note_high=36)
engine.midi_splitter_add_output(split, "snare", channel=0, note_low=38, note_high=38)
engine.midi_splitter_add_output(split, "hat",   channel=0, note_low=42, note_high=46)

# Internal connections
engine.group_connect(kit, split, "kick",  kick,  "midi_in")
engine.group_connect(kit, split, "snare", snare, "midi_in")
engine.group_connect(kit, split, "hat",   hat,   "midi_in")

# Export: one MIDI in, three audio outs
engine.group_export_input(kit, split, "midi_in", "midi_in")
engine.group_export_output(kit, kick,  "out", "kick_out")
engine.group_export_output(kit, snare, "out", "snare_out")
engine.group_export_output(kit, hat,   "out", "hat_out")

# From the parent graph — one MIDI connection
engine.connect(midi_router_node, "ch10", kit, "midi_in")
engine.connect(kit, "kick_out",  engine.output, "in")
engine.connect(kit, "snare_out", engine.output, "in")
engine.connect(kit, "hat_out",   engine.output, "in")
```

## Example: Keyboard Split (top-level)

```python
split = engine.add_midi_splitter("key_split")
engine.midi_splitter_add_output(split, "bass",  channel=0, note_low=0,  note_high=59)
engine.midi_splitter_add_output(split, "lead",  channel=0, note_low=60, note_high=127)

bass_synth = engine.add_plugin("Bass Synth")
lead_synth = engine.add_plugin("Lead Synth")

engine.connect(split, "bass", bass_synth, "midi_in")
engine.connect(split, "lead", lead_synth, "midi_in")
engine.connect(bass_synth, "out", engine.output, "in")
engine.connect(lead_synth, "out", engine.output, "in")
```
