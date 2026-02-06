# Port Specification

## Overview

A Port is a connection point on a Node. It describes what kind of signal a node accepts or produces. Ports are value types — lightweight, copyable, comparable. They carry no audio data; they are metadata that Graph uses to validate connections and Engine uses to assign buffers.

## Responsibilities

- Describe a connection point's direction, signal type, channel count, and name
- Support equality comparison (for connection validation)
- Provide a unique identity within a node (direction + name)

## Types

```cpp
enum class PortDirection { input, output };
enum class SignalType { audio, midi };

struct PortDescriptor {
    std::string name;
    PortDirection direction;
    SignalType signalType;
    int channels;  // audio: 1=mono, 2=stereo, N; midi: always 1
};
```

## PortAddress

A globally unique reference to a specific port on a specific node, used by Graph for connections.

```cpp
struct PortAddress {
    int nodeId;
    PortDirection direction;
    std::string portName;
};
```

## Invariants

- `name` is non-empty
- `channels` >= 1
- `channels` == 1 for midi ports
- A node may not have two ports with the same direction and name
- PortDescriptor is immutable after construction — nodes declare ports at construction time and never change them

## Connection Compatibility

Two ports can be connected if and only if:

1. Source port direction is `output`, destination port direction is `input`
2. Both ports have the same `SignalType`
3. For audio: source `channels` == destination `channels` (no implicit mixing/splitting for this milestone)

Channel mixing (mono-to-stereo, etc.) is deferred to a later milestone.

## Error Conditions

- Construction with empty name: invalid
- Construction with channels < 1: invalid
- Construction of midi port with channels != 1: invalid

## Does NOT Handle

- Actual audio/MIDI data (Engine provides buffers)
- Connection logic (Graph)
- Buffer assignment (Engine)
- Channel mixing or splitting

## Dependencies

- None (standard library only)

## Thread Safety

- PortDescriptor is immutable after construction, safe to read from any thread
- PortAddress is a value type, safe to copy and read from any thread

## Example Usage

```cpp
// A stereo audio effect
auto inputs = {
    PortDescriptor{ "in", PortDirection::input, SignalType::audio, 2 }
};
auto outputs = {
    PortDescriptor{ "out", PortDirection::output, SignalType::audio, 2 }
};

// A synth plugin (MIDI in, stereo audio out)
auto inputs = {
    PortDescriptor{ "midi", PortDirection::input, SignalType::midi, 1 }
};
auto outputs = {
    PortDescriptor{ "out", PortDirection::output, SignalType::audio, 2 }
};

// Checking connection compatibility
bool canConnect(const PortDescriptor& src, const PortDescriptor& dst) {
    return src.direction == PortDirection::output
        && dst.direction == PortDirection::input
        && src.signalType == dst.signalType
        && src.channels == dst.channels;
}
```
