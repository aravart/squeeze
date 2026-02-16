# Port Specification

## Responsibilities
- Describe a connection point's direction, signal type, channel count, and name
- Support equality comparison (for connection validation and fan-in detection)
- Provide a unique identity within a node (direction + name)
- Validate port construction (non-empty name, valid channel count)
- Determine connection compatibility between two ports

## Interface

```cpp
namespace squeeze {

enum class PortDirection { input, output };
enum class SignalType { audio, midi };

struct PortDescriptor {
    std::string name;
    PortDirection direction;
    SignalType signalType;
    int channels;  // audio: 1=mono, 2=stereo, N; midi: always 1

    bool operator==(const PortDescriptor& o) const;
    bool operator!=(const PortDescriptor& o) const;
};

struct PortAddress {
    int nodeId;
    PortDirection direction;
    std::string portName;

    bool operator==(const PortAddress& o) const;
    bool operator!=(const PortAddress& o) const;
};

// Free functions
bool isValid(const PortDescriptor& port);
bool canConnect(const PortDescriptor& src, const PortDescriptor& dst);

} // namespace squeeze
```

## Signal Types

| Type | Description | Channels |
|------|-------------|----------|
| `audio` | PCM audio samples; Engine provides float buffers | 1 (mono), 2 (stereo), or N |
| `midi` | JUCE `MidiBuffer` events | Always 1 |

Parameters are not routed through ports. They use a separate index-based virtual system on Node.

## PortAddress

A globally unique reference to a specific port on a specific node. Used by Graph for connection endpoints and fan-in detection.

The compound key is `(nodeId, direction, portName)`. Two `PortAddress` values are equal iff all three fields match.

## Validation: `isValid()`

A `PortDescriptor` is valid iff:
- `name` is non-empty
- `channels >= 1`
- If `signalType == midi`, then `channels == 1`

## Connection Compatibility: `canConnect()`

Two ports can be connected iff:
1. `src.direction == output` and `dst.direction == input`
2. `src.signalType == dst.signalType`

Channel count mismatch is **allowed for audio** — the Engine copies `min(src, dst)` channels. MIDI channels are always 1 on both sides, so the check is trivially satisfied.

## Fan-in and Fan-out

| Signal Type | Fan-in (multiple sources to one input) | Fan-out (one output to multiple inputs) |
|-------------|----------------------------------------|-----------------------------------------|
| Audio | **Allowed** — Engine sums all sources into the input buffer before calling `process()` | Allowed |
| MIDI | Allowed — MIDI buffers are merged | Allowed |

This is a v2 change from v1, which forbade audio fan-in. Allowing it means any audio input port naturally acts as a summing bus — no special MixerNode needed for basic mixing. The summing is handled by the Engine at snapshot execution time, not by Port or Graph.

## Invariants
- `PortDescriptor` is a value type — immutable once constructed, safe to copy and compare
- `PortDescriptor` equality compares all four fields (name, direction, signalType, channels)
- `PortAddress` equality compares all three fields (nodeId, direction, portName)
- Ports carry no audio data — they are metadata only. Engine assigns buffers separately.
- A node's port list is typically fixed at construction, but **may change at runtime** (control thread only). GroupNode uses this to dynamically export/unexport internal ports. When a port is removed, Graph auto-disconnects any connections referencing it.

## Error Conditions
- Construction with empty name: `isValid()` returns false
- Construction with `channels < 1`: `isValid()` returns false
- Construction of midi port with `channels != 1`: `isValid()` returns false

## Does NOT Handle
- Audio/MIDI data (Engine provides buffers)
- Connection storage or cycle detection (Graph)
- Buffer assignment or summing (Engine)
- Port registration or indexing (Node owns its port list)

## Dependencies
- None (`<string>` only)

## Thread Safety
- `PortDescriptor` and `PortAddress` are value types, safe to copy and read from any thread
- Port types themselves have no mutexes or atomics
- A node's port list may be mutated on the control thread only (e.g. GroupNode export/unexport). The audio thread reads ports only via the snapshot, which is rebuilt after any port change.

## C ABI

Port has no direct C ABI surface. Port types are exposed indirectly through:
- `sq_get_ports(engine, node_id)` — returns `SqPortList` (defined with Node)
- `sq_connect(engine, src_node, src_port, dst_node, dst_port)` — references ports by name (defined with Graph)

## Example Usage

```cpp
// Fixed ports — most nodes declare ports at construction and never change them
std::vector<PortDescriptor> SamplerNode::getInputPorts() const {
    return {{"midi_in", PortDirection::input, SignalType::midi, 1}};
}
std::vector<PortDescriptor> SamplerNode::getOutputPorts() const {
    return {{"out", PortDirection::output, SignalType::audio, 2}};
}

// Dynamic ports — GroupNode exports/unexports internal ports at runtime
// Adding a new input to a mixer bus (control thread):
mixerGroup.exportPort(newGainNode, "in", "in_3");  // new port appears
graph.connect(synthId, "out", mixerGroup.getId(), "in_3");
engine.rebuildSnapshot();
// No teardown — mixer keeps playing, new input is live after swap

// Graph uses PortAddress to describe connections
PortAddress src{srcNodeId, PortDirection::output, "out"};
PortAddress dst{dstNodeId, PortDirection::input, "in"};

// Validation
assert(isValid(srcPort));
assert(canConnect(srcPort, dstPort));
```

### Port Naming Conventions

| Pattern | Usage |
|---------|-------|
| `"in"` / `"out"` | Primary audio I/O |
| `"midi_in"` / `"midi_out"` | MIDI I/O |
| Descriptive names for extras | e.g. `"sidechain"` for secondary audio input |
| Numbered inputs for groups | e.g. `"in_1"`, `"in_2"` for mixer/bus channels |
