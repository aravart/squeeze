# Python High-Level API Specification

## Motivation

The current Python package (`squeeze.Squeeze`) is a 1:1 ctypes wrapper over the C ABI. Every method maps directly to a `sq_*` function. This is correct and complete, but it exposes C idioms to Python users:

- Nodes are bare `int` IDs threaded through every call
- Ports and parameters are untyped dicts
- Connecting two nodes requires manual port lookup (6+ lines)
- Transport, MIDI, and scheduling are flat methods on a single class (~40 methods)
- Event scheduling uses raw IDs and positional arguments

The high-level API wraps the low-level layer with Python objects. The low-level `Squeeze` class remains available and unchanged — it is the foundation, not a replacement target.

## Responsibilities

- Provide a Pythonic interface to the Squeeze audio engine
- Wrap node IDs in `Node` objects with methods, properties, and operator overloads
- Group transport, MIDI, and scheduling operations into sub-objects
- Provide typed dataclasses for ports and parameters instead of raw dicts
- Provide enums for port direction and signal type
- Auto-match ports for the common case (connect first compatible audio/MIDI out to in)
- Delegate all FFI calls to the existing low-level `Squeeze` class

## Interface

### Module Layout

```
python/squeeze/
├── __init__.py          # re-exports Engine, Node, SqueezeError, set_log_level, etc.
├── _ffi.py              # unchanged — raw ctypes bindings
├── _low_level.py        # current Squeeze class, moved here, renamed internally
├── engine.py            # Engine (high-level entry point)
├── node.py              # Node, Port, Param, ParamMap
├── transport.py         # Transport sub-object
├── midi.py              # Midi, MidiDevice sub-objects
└── types.py             # Direction, SignalType enums; Port, ParamDescriptor dataclasses
```

### Enums (`types.py`)

```python
from enum import Enum

class Direction(Enum):
    INPUT = "input"
    OUTPUT = "output"

class SignalType(Enum):
    AUDIO = "audio"
    MIDI = "midi"
```

### Port (`types.py`)

```python
from dataclasses import dataclass

@dataclass(frozen=True)
class Port:
    """Describes a connection endpoint on a node."""
    name: str
    direction: Direction
    signal_type: SignalType
    channels: int

    @property
    def is_audio(self) -> bool:
        return self.signal_type == SignalType.AUDIO

    @property
    def is_midi(self) -> bool:
        return self.signal_type == SignalType.MIDI

    @property
    def is_input(self) -> bool:
        return self.direction == Direction.INPUT

    @property
    def is_output(self) -> bool:
        return self.direction == Direction.OUTPUT
```

### ParamDescriptor (`types.py`)

```python
@dataclass(frozen=True)
class ParamDescriptor:
    """Metadata for a node parameter."""
    name: str
    default_value: float
    num_steps: int          # 0 = continuous, >0 = stepped
    automatable: bool
    boolean: bool
    label: str              # unit: "dB", "Hz", "%", ""
    group: str              # "" = ungrouped
```

### Connection (`types.py`)

```python
@dataclass(frozen=True)
class Connection:
    """An active connection between two ports."""
    id: int
    src_node: int
    src_port: str
    dst_node: int
    dst_port: str
```

---

### Node (`node.py`)

A `Node` wraps a node ID and a reference to the low-level `Squeeze` instance. It is created by `Engine` — users never construct `Node` directly.

```python
class Node:
    def __init__(self, engine: "Engine", node_id: int):
        self._engine = engine       # high-level Engine
        self._sq = engine._sq       # low-level Squeeze
        self._id = node_id

    @property
    def id(self) -> int:
        """The underlying integer node ID."""
        return self._id

    @property
    def name(self) -> str:
        """Node type name (e.g. 'GainNode', 'Dexed')."""
        return self._sq.node_name(self._id)

    # --- Ports ---

    @property
    def ports(self) -> list[Port]:
        """All ports on this node, as typed Port objects."""

    @property
    def audio_inputs(self) -> list[Port]:
        """Audio input ports."""

    @property
    def audio_outputs(self) -> list[Port]:
        """Audio output ports."""

    @property
    def midi_inputs(self) -> list[Port]:
        """MIDI input ports."""

    @property
    def midi_outputs(self) -> list[Port]:
        """MIDI output ports."""

    def port(self, name: str) -> "PortRef":
        """Return a PortRef for explicit connection.

        Usage: synth.port("sidechain_out") >> output.port("in")
        Raises KeyError if port name not found.
        """

    # --- Parameters ---

    @property
    def params(self) -> "ParamMap":
        """Dict-like access to parameters.

        synth.params["Gain"].value = 0.8
        synth.params["Gain"].text   # "0.8 dB"
        """

    @property
    def param_descriptors(self) -> list[ParamDescriptor]:
        """Parameter metadata for all parameters on this node."""

    # --- Connection operator ---

    def __rshift__(self, other: "Node") -> Connection:
        """Connect this node to another: synth >> output.

        Auto-matches the first audio output of self to the first audio
        input of other. If no audio ports exist, tries MIDI.

        Raises SqueezeError if:
          - self has no output ports
          - other has no input ports
          - no compatible port pair found
          - the connection would create a cycle
        """

    def __rrshift__(self, other: "Node") -> Connection:
        """Support reverse: allows Node on right side of >>."""

    # --- Plugin editor ---

    def open_editor(self) -> None:
        """Open the native plugin editor window.
        Raises SqueezeError if not a plugin, no editor, or already open."""

    def close_editor(self) -> None:
        """Close the plugin editor window.
        Raises SqueezeError if no editor is open."""

    @property
    def editor_open(self) -> bool:
        """True if the editor window is currently open for this node."""

    # --- Event scheduling (delegates to Engine) ---

    def note_on(self, beat: float, channel: int, note: int,
                velocity: float) -> bool:
        """Schedule a note-on event at the given beat time."""

    def note_off(self, beat: float, channel: int, note: int) -> bool:
        """Schedule a note-off event at the given beat time."""

    def cc(self, beat: float, channel: int, cc_num: int,
           cc_val: int) -> bool:
        """Schedule a CC event at the given beat time."""

    def automate(self, beat: float, param_name: str, value: float) -> bool:
        """Schedule a parameter change at the given beat time."""

    # --- Lifecycle ---

    def remove(self) -> bool:
        """Remove this node from the engine. Returns False if not found."""

    def __repr__(self) -> str:
        return f"Node({self._id}, {self.name!r})"

    def __eq__(self, other) -> bool:
        if isinstance(other, Node):
            return self._id == other._id
        return NotImplemented

    def __hash__(self) -> int:
        return hash(self._id)
```

### PortRef (`node.py`)

A `PortRef` binds a specific port name to a node, for explicit connections.

```python
class PortRef:
    """A reference to a specific port on a specific node.

    Used for explicit connections:
        synth.port("sidechain_out") >> output.port("in")
    """

    def __init__(self, node: Node, port_name: str):
        self._node = node
        self._port_name = port_name

    @property
    def node(self) -> Node:
        return self._node

    @property
    def port_name(self) -> str:
        return self._port_name

    def __rshift__(self, other: "PortRef") -> Connection:
        """Connect this port to another port.

        Raises SqueezeError if:
          - port names don't exist on their respective nodes
          - signal types are incompatible
          - the connection would create a cycle
        """
```

### ParamMap and Param (`node.py`)

```python
class ParamMap:
    """Dict-like access to a node's parameters.

    map = node.params
    map["Gain"].value           # get
    map["Gain"].value = 0.5     # set
    map["Gain"].text            # display text
    map["Gain"].descriptor      # ParamDescriptor

    for name, param in map.items():
        print(f"{name}: {param.value} ({param.text})")

    "Gain" in map               # True/False
    len(map)                    # number of parameters
    """

    def __init__(self, node: Node):
        self._node = node

    def __getitem__(self, name: str) -> "Param":
        """Return a Param proxy for the named parameter.
        Raises KeyError if the parameter does not exist on this node.
        """

    def __contains__(self, name: str) -> bool:
        """Check if a parameter exists by name."""

    def __len__(self) -> int:
        """Number of parameters on this node."""

    def __iter__(self):
        """Iterate over parameter names."""

    def items(self):
        """Iterate over (name, Param) pairs."""

    def keys(self):
        """Iterate over parameter names."""

    def values(self):
        """Iterate over Param proxies."""


class Param:
    """Live proxy to a single parameter on a node.

    Reads and writes go directly to the engine — no caching.
    """

    def __init__(self, node: Node, name: str, descriptor: ParamDescriptor):
        self._node = node
        self._name = name
        self._descriptor = descriptor

    @property
    def name(self) -> str:
        return self._name

    @property
    def value(self) -> float:
        """Current normalized value (0.0-1.0). Reads from engine."""
        return self._node._sq.get_param(self._node.id, self._name)

    @value.setter
    def value(self, v: float) -> None:
        """Set normalized value (0.0-1.0). Writes to engine."""
        self._node._sq.set_param(self._node.id, self._name, v)

    @property
    def text(self) -> str:
        """Human-readable display text (e.g. '-6.0 dB'). Reads from engine."""
        return self._node._sq.param_text(self._node.id, self._name)

    @property
    def descriptor(self) -> ParamDescriptor:
        """Parameter metadata (default, steps, label, group, etc.)."""
        return self._descriptor

    @property
    def default(self) -> float:
        """Default value."""
        return self._descriptor.default_value

    def __repr__(self) -> str:
        return f"Param({self._name!r}, value={self.value:.3f}, text={self.text!r})"
```

---

### Transport (`transport.py`)

```python
class Transport:
    """Sub-object for transport control. Accessed via engine.transport."""

    def __init__(self, sq: "Squeeze"):
        self._sq = sq

    def play(self) -> None:
        """Start playback."""
        self._sq.transport_play()

    def stop(self) -> None:
        """Stop playback and reset position."""
        self._sq.transport_stop()

    def pause(self) -> None:
        """Pause playback (position preserved)."""
        self._sq.transport_pause()

    @property
    def tempo(self) -> float:
        """Current tempo in BPM."""
        return self._sq.transport_tempo

    @tempo.setter
    def tempo(self, bpm: float) -> None:
        """Set tempo in BPM."""
        self._sq.transport_set_tempo(bpm)

    @property
    def position(self) -> float:
        """Current playback position in beats."""
        return self._sq.transport_position

    @property
    def playing(self) -> bool:
        """True if transport is currently playing."""
        return self._sq.transport_is_playing

    def seek(self, *, beats: float = None, samples: int = None) -> None:
        """Seek to a position. Specify exactly one of beats or samples.

        Raises ValueError if neither or both are specified.
        """

    def set_time_signature(self, numerator: int, denominator: int) -> None:
        """Set the time signature (e.g. 4, 4 for 4/4)."""
        self._sq.transport_set_time_signature(numerator, denominator)

    def set_loop(self, start: float, end: float) -> None:
        """Set loop points in beats."""
        self._sq.transport_set_loop_points(start, end)

    @property
    def looping(self) -> bool:
        """Whether looping is enabled."""
        # Note: requires adding a getter to low-level API if not present.
        # If not available, this property is write-only via the setter.
        raise NotImplementedError("read-only looping query not yet in C ABI")

    @looping.setter
    def looping(self, enabled: bool) -> None:
        """Enable or disable looping."""
        self._sq.transport_set_looping(enabled)
```

---

### Midi and MidiDevice (`midi.py`)

```python
class Midi:
    """Sub-object for MIDI device management. Accessed via engine.midi."""

    def __init__(self, engine: "Engine", sq: "Squeeze"):
        self._engine = engine
        self._sq = sq

    @property
    def devices(self) -> list["MidiDevice"]:
        """Available MIDI input devices (may change due to hot-plug)."""
        return [MidiDevice(self._engine, self._sq, name)
                for name in self._sq.midi_devices]

    @property
    def open_devices(self) -> list["MidiDevice"]:
        """Currently open MIDI devices."""
        return [MidiDevice(self._engine, self._sq, name)
                for name in self._sq.midi_open_devices]

    @property
    def routes(self) -> list[dict]:
        """Active MIDI routes."""
        return self._sq.midi_routes

    def unroute(self, route_id: int) -> bool:
        """Remove a MIDI route by ID."""
        return self._sq.midi_unroute(route_id)


class MidiDevice:
    """Represents a MIDI input device.

    Supports the >> operator to route to a Node:
        device >> synth
        device.route(synth, channel=1)
    """

    def __init__(self, engine: "Engine", sq: "Squeeze", name: str):
        self._engine = engine
        self._sq = sq
        self._name = name

    @property
    def name(self) -> str:
        return self._name

    def open(self) -> None:
        """Open this MIDI device. Raises SqueezeError on failure."""
        self._sq.midi_open(self._name)

    def close(self) -> None:
        """Close this MIDI device. No-op if not open."""
        self._sq.midi_close(self._name)

    def route(self, node: "Node", *, channel: int = 0,
              note: int = -1) -> int:
        """Route this device to a node. Returns route ID.

        channel: 0=all, 1-16=specific channel.
        note: -1=all, 0-127=specific note.
        Raises SqueezeError on failure.
        """
        return self._sq.midi_route(self._name, node.id, channel, note)

    def __rshift__(self, node: "Node") -> int:
        """Route this device to a node: device >> synth.

        Uses channel=0 (all), note=-1 (all).
        The device must be open. Returns route ID.
        """
        return self.route(node)

    def __repr__(self) -> str:
        return f"MidiDevice({self._name!r})"

    def __eq__(self, other) -> bool:
        if isinstance(other, MidiDevice):
            return self._name == other._name
        return NotImplemented

    def __hash__(self) -> int:
        return hash(self._name)
```

---

### Engine (`engine.py`)

The top-level entry point. Wraps the low-level `Squeeze` class and provides sub-objects.

```python
class Engine:
    """Squeeze audio engine — high-level Pythonic interface.

    Usage:
        with Engine() as engine:
            synth = engine.add_test_synth()
            synth >> engine.output
            engine.start()
    """

    def __init__(self):
        self._sq = Squeeze()  # low-level

    def close(self) -> None:
        """Destroy the engine. Safe to call multiple times."""
        self._sq.close()

    def __enter__(self) -> "Engine":
        return self

    def __exit__(self, *args) -> None:
        self.close()

    def __del__(self):
        self.close()

    @property
    def version(self) -> str:
        """Engine version string."""
        return self._sq.version

    # --- Sub-objects ---

    @property
    def transport(self) -> Transport:
        """Transport control (play, stop, tempo, seek, loop)."""

    @property
    def midi(self) -> Midi:
        """MIDI device management and routing."""

    # --- Node management ---

    def add_gain(self) -> Node:
        """Add a GainNode. Returns a Node object."""
        node_id = self._sq.add_gain()
        return Node(self, node_id)

    def add_test_synth(self) -> Node:
        """Add a test synth (sine/noise, 2ch audio out, MIDI in).
        Returns a Node object."""
        node_id = self._sq.add_test_synth()
        return Node(self, node_id)

    def add_plugin(self, name: str) -> Node:
        """Add a plugin by name. Returns a Node object.
        Raises SqueezeError if plugin not found or instantiation fails."""
        node_id = self._sq.add_plugin(name)
        return Node(self, node_id)

    @property
    def output(self) -> Node:
        """The built-in output node."""
        return Node(self, self._sq.output)

    @property
    def node_count(self) -> int:
        """Total number of nodes (including the output node)."""
        return self._sq.node_count()

    def node(self, node_id: int) -> Node:
        """Wrap an existing node ID in a Node object.
        Useful for interop with the low-level API.
        Does not validate the ID — operations on an invalid Node
        will raise SqueezeError or return empty results.
        """
        return Node(self, node_id)

    # --- Connection management ---

    def connect(self, src: Node, src_port: str,
                dst: Node, dst_port: str) -> Connection:
        """Connect two specific ports. Returns a Connection.
        Raises SqueezeError on failure."""
        conn_id = self._sq.connect(src.id, src_port, dst.id, dst_port)
        return Connection(id=conn_id, src_node=src.id, src_port=src_port,
                          dst_node=dst.id, dst_port=dst_port)

    def disconnect(self, conn: Connection | int) -> bool:
        """Disconnect by Connection object or connection ID.
        Returns False if not found."""
        conn_id = conn.id if isinstance(conn, Connection) else conn
        return self._sq.disconnect(conn_id)

    @property
    def connections(self) -> list[Connection]:
        """All active connections as typed Connection objects."""

    # --- Plugin management ---

    def load_plugin_cache(self, path: str) -> None:
        """Load plugin cache from XML file. Raises SqueezeError on failure."""
        self._sq.load_plugin_cache(path)

    @property
    def available_plugins(self) -> list[str]:
        """Available plugin names (sorted alphabetically)."""
        return self._sq.available_plugins

    @property
    def num_plugins(self) -> int:
        """Number of plugins in the loaded cache."""
        return self._sq.num_plugins

    # --- Audio device ---

    def start(self, sample_rate: float = 44100.0,
              block_size: int = 512) -> None:
        """Start the audio device. Raises SqueezeError on failure."""
        self._sq.start(sample_rate, block_size)

    def stop(self) -> None:
        """Stop the audio device. No-op if not running."""
        self._sq.stop()

    @property
    def running(self) -> bool:
        """True if the audio device is currently running."""
        return self._sq.is_running

    @property
    def sample_rate(self) -> float:
        """Actual device sample rate (0.0 if not running)."""
        return self._sq.sample_rate

    @property
    def block_size(self) -> int:
        """Actual device block size (0 if not running)."""
        return self._sq.block_size

    # --- Plugin editor ---

    @staticmethod
    def process_events(timeout_ms: int = 0) -> None:
        """Process pending JUCE GUI/message events.
        With timeout_ms=0 (default), non-blocking. With timeout_ms>0, blocking.
        Call from the main thread."""

    # --- Testing ---

    def prepare_for_testing(self, sample_rate: float = 44100.0,
                            block_size: int = 512) -> None:
        """Prepare engine for headless testing (no audio device)."""
        self._sq.prepare_for_testing(sample_rate, block_size)

    def render(self, num_samples: int = 512) -> None:
        """Render one block in test mode."""
        self._sq.render(num_samples)

    # --- Low-level access ---

    @property
    def sq(self) -> "Squeeze":
        """Access the underlying low-level Squeeze instance.
        For advanced use or interop with code that uses the low-level API.
        """
        return self._sq
```

---

## Auto-Connection Algorithm (`>>` operator)

`Node.__rshift__` implements automatic port matching. The algorithm:

1. Collect output ports of `self`, input ports of `other`.
2. Find the first matching pair by signal type preference: **audio first, then MIDI**.
3. Within each signal type, match by order of declaration (first output to first input).
4. If a match is found, call `engine.connect(self, src_port, other, dst_port)`.
5. If no match is found, raise `SqueezeError` with a descriptive message listing the available ports.

### Matching rules

| self outputs | other inputs | Result |
|---|---|---|
| 1 audio out | 1 audio in | Connect audio |
| 1 audio out, 1 MIDI out | 1 audio in, 1 MIDI in | Connect audio only |
| 1 MIDI out | 1 MIDI in | Connect MIDI |
| No outputs | Any | Raise SqueezeError |
| Any | No inputs | Raise SqueezeError |
| Audio out | MIDI in only | Raise SqueezeError |

`>>` always makes exactly one connection. For nodes with multiple output/input port pairs (e.g. audio + MIDI), use explicit port connections:

```python
synth.port("audio_out") >> mixer.port("in")
synth.port("midi_out") >> recorder.port("midi_in")
```

### Return value

`>>` returns a `Connection` object. This allows:

```python
conn = synth >> output
engine.disconnect(conn)
```

---

## `__init__.py` Public Exports

```python
# High-level API (primary)
from squeeze.engine import Engine
from squeeze.node import Node, PortRef, Param, ParamMap
from squeeze.transport import Transport
from squeeze.midi import Midi, MidiDevice
from squeeze.types import Direction, SignalType, Port, ParamDescriptor, Connection

# Low-level API (still available)
from squeeze._low_level import Squeeze

# Module-level utilities
from squeeze._low_level import SqueezeError, set_log_level, set_log_callback
```

---

## Invariants

- `Node` objects are lightweight proxies — they hold only an ID and a reference. Creating multiple `Node` objects for the same ID is safe. `Node.__eq__` compares by ID.
- `Param.value` always reads/writes through the engine — no stale caching. Every `.value` access is an FFI call.
- `ParamMap` validates parameter names against descriptors on access — `KeyError` for unknown names.
- `Port` is a frozen dataclass — immutable value type, safe to store and compare.
- `Connection` is a frozen dataclass — immutable record of a connection.
- `>>` never creates more than one connection per invocation.
- All error conditions raise `SqueezeError` (same exception as the low-level API).
- Sub-objects (`Transport`, `Midi`) hold a reference to the low-level `Squeeze` — they do not outlive the `Engine`.
- The low-level `Squeeze` class is unchanged and fully functional. The high-level API is additive.

## Error Conditions

- `Node.port(name)` with unknown port name: raises `KeyError`
- `ParamMap[name]` with unknown parameter name: raises `KeyError`
- `Node >> Node` with no compatible ports: raises `SqueezeError`
- `Node >> Node` that would create a cycle: raises `SqueezeError` (from engine)
- `MidiDevice.open()` with unavailable device: raises `SqueezeError`
- `MidiDevice >> Node` when device not open: raises `SqueezeError`
- `Transport.seek()` with neither `beats` nor `samples`: raises `ValueError`
- `Transport.seek()` with both `beats` and `samples`: raises `ValueError`
- `Engine.add_plugin(name)` with unknown plugin: raises `SqueezeError`
- `Engine.start()` with no audio device: raises `SqueezeError`
- All other FFI failures: raises `SqueezeError` with message from C layer

## Does NOT Handle

- **New C ABI surface** — this is purely a Python layer on top of the existing FFI
- **Async or threaded Python access** — all calls are synchronous, matching the C ABI's control-thread model
- **Audio buffer access from Python** — no direct access to audio data (same as current API)
- **Pattern sequencing or composition helpers** — the API exposes the scheduling primitives, not higher-level music abstractions
- **Automatic MIDI device discovery/reconnection** — users poll `midi.devices` as needed

## Dependencies

- `squeeze._low_level` (the existing `Squeeze` class, moved from `__init__.py`)
- `squeeze._ffi` (unchanged ctypes bindings)
- Python standard library only: `dataclasses`, `enum`, `typing`
- No third-party dependencies

## Thread Safety

The high-level API has the same thread safety model as the low-level API: all calls must happen from a single thread (the control thread). `Node`, `Param`, `Transport`, `Midi`, and `MidiDevice` objects are not thread-safe — they are proxies to the engine, which serializes under `controlMutex_` on the C++ side.

## Testing Strategy

- **Every high-level method** must have a pytest case in `python/tests/`.
- **Node proxy tests**: create nodes, verify `name`, `ports`, `params`, `remove()`.
- **ParamMap tests**: get/set values, `text`, `KeyError` for unknown params, iteration.
- **`>>` operator tests**: auto-connect audio, auto-connect MIDI, explicit `port()` connection, cycle rejection, no-match error.
- **Transport tests**: play/stop/pause, tempo get/set, seek, loop.
- **Midi tests**: device listing, open/close, `>>` routing, route removal.
- **Interop tests**: `engine.sq` access, `engine.node(id)` wrapping, mixing high-level and low-level calls.
- **Existing low-level tests** must continue to pass unchanged.

## Example Usage

### Before (current low-level API)

```python
from squeeze import Squeeze, SqueezeError, set_log_level

set_log_level(2)

with Squeeze() as sq:
    sq.load_plugin_cache("plugin-cache.xml")

    synth_id = sq.add_plugin("Dexed")
    ports = sq.get_ports(synth_id)
    synth_out = next(p["name"] for p in ports
                     if p["direction"] == "output" and p["signal_type"] == "audio")

    out_id = sq.output
    out_ports = sq.get_ports(out_id)
    out_in = next(p["name"] for p in out_ports
                  if p["direction"] == "input" and p["signal_type"] == "audio")

    sq.connect(synth_id, synth_out, out_id, out_in)

    val = sq.get_param(synth_id, "Cutoff")
    sq.set_param(synth_id, "Cutoff", 0.7)

    for dev in sq.midi_devices:
        sq.midi_open(dev)
        sq.midi_route(dev, synth_id)

    sq.transport_set_tempo(120.0)
    sq.schedule_note_on(synth_id, 0.0, 1, 60, 0.8)
    sq.schedule_note_off(synth_id, 0.5, 1, 60)

    sq.start()
```

### After (high-level API)

```python
from squeeze import Engine, set_log_level

set_log_level(2)

with Engine() as engine:
    engine.load_plugin_cache("plugin-cache.xml")

    synth = engine.add_plugin("Dexed")
    synth >> engine.output

    synth.params["Cutoff"].value = 0.7

    for dev in engine.midi.devices:
        dev.open()
        dev >> synth

    engine.transport.tempo = 120.0
    synth.note_on(beat=0.0, channel=1, note=60, velocity=0.8)
    synth.note_off(beat=0.5, channel=1, note=60)

    engine.start()
```

### Explicit port connections

```python
# When auto-matching isn't enough
synth.port("sidechain_out") >> compressor.port("sidechain_in")

# Or using the engine's connect method
engine.connect(synth, "out", mixer, "in_2")
```

### Inspecting a node

```python
synth = engine.add_plugin("Dexed")

print(synth.name)                  # "Dexed"
print(synth.audio_outputs)         # [Port(name='out', ...)]
print(synth.midi_inputs)           # [Port(name='midi_in', ...)]

for name, param in synth.params.items():
    print(f"{name}: {param.value:.3f} ({param.text})")

print("Cutoff" in synth.params)    # True
print(len(synth.params))           # number of parameters
```

### Offline rendering

```python
with Engine() as engine:
    synth = engine.add_test_synth()
    synth >> engine.output

    engine.prepare_for_testing(44100.0, 512)
    engine.transport.tempo = 120.0

    # Schedule a melody
    for i, note in enumerate([60, 64, 67, 72]):
        synth.note_on(beat=i * 0.5, channel=1, note=note, velocity=0.8)
        synth.note_off(beat=i * 0.5 + 0.4, channel=1, note=note)

    engine.transport.play()
    for _ in range(256):
        engine.render(512)
    engine.transport.stop()
```

### Interop with low-level API

```python
engine = Engine()

# Drop down to low-level when needed
raw_id = engine.sq.add_gain()
gain_node = engine.node(raw_id)   # wrap in high-level Node

# Mix freely
gain_node >> engine.output
```
