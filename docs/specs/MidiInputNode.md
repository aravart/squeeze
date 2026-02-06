# MidiInputNode Specification

## Responsibilities
- Capture MIDI from a hardware input device and make it available in the graph
- Bridge MIDI thread (OS high-priority callback) to audio thread via lock-free queue
- Provide a factory method to enumerate and open MIDI devices by name

## Interface

```cpp
struct MidiEvent {
    uint8_t data[3];
    int size;           // 1, 2, or 3
};

class MidiInputNode : public Node, public juce::MidiInputCallback {
public:
    explicit MidiInputNode(const std::string& deviceName,
                           const juce::String& deviceIdentifier);
    ~MidiInputNode() override;

    static std::unique_ptr<MidiInputNode> create(const std::string& deviceName,
                                                  std::string& errorMessage);

    void prepare(double sampleRate, int blockSize) override;
    void process(ProcessContext& context) override;
    void release() override;
    std::vector<PortDescriptor> getInputPorts() const override;
    std::vector<PortDescriptor> getOutputPorts() const override;

    const std::string& getDeviceName() const;

    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;
};
```

## Invariants
- `getInputPorts()` always returns empty (no inputs)
- `getOutputPorts()` always returns exactly one MIDI output port named `"midi_out"`
- `process()` never allocates, never blocks (RT-safe)
- Events appear in `outputMidi` in the order they were pushed to the queue
- `handleIncomingMidiMessage()` is safe to call from any thread (lock-free push)
- SysEx messages (size > 3 bytes) are silently dropped

## Error Conditions
- `create()` returns nullptr and sets errorMessage when the device name doesn't match any available device
- Queue overflow: if the SPSC queue is full, `handleIncomingMidiMessage()` silently drops the event (no crash, no block)

## Does NOT Handle
- SysEx messages (fixed 3-byte queue entries)
- Device hot-plug detection (device must be connected at construction time)
- Multiple MIDI devices in a single node (create one node per device)
- MIDI output to hardware
- Timestamp correction or jitter compensation

## Dependencies
- `Node` (base class)
- `SPSCQueue` (lock-free queue)
- `juce::MidiInput` / `juce::MidiInputCallback` (JUCE MIDI device API)
- `Logger` (debug logging)

## Thread Safety
- **MIDI thread** (OS callback): calls `handleIncomingMidiMessage()` which pushes to SPSC queue (producer)
- **Audio thread**: calls `process()` which pops from SPSC queue (consumer)
- **Control thread**: constructor, destructor, `prepare()`, `release()`, `create()`
- SPSC queue provides safe transfer from MIDI thread to audio thread without locks

## Example Usage

```cpp
std::string error;
auto midiNode = MidiInputNode::create("USB MIDI Controller", error);
if (!midiNode) {
    std::cerr << "Failed: " << error << std::endl;
    return;
}

int id = graph.addNode(midiNode.get());
// Connect MIDI output to a synth plugin's MIDI input
graph.connect({id, PortDirection::output, "midi_out"},
              {synthId, PortDirection::input, "midi_in"});
```
