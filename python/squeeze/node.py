"""Node, PortRef, ParamMap, and Param for the Squeeze high-level API."""

from __future__ import annotations

from typing import TYPE_CHECKING

from squeeze.types import Connection, Direction, ParamDescriptor, Port, SignalType

if TYPE_CHECKING:
    from squeeze._low_level import Squeeze, SqueezeError
    from squeeze.engine import Engine


def _ports_from_dicts(raw: list[dict]) -> list[Port]:
    """Convert low-level port dicts to typed Port objects."""
    return [
        Port(
            name=p["name"],
            direction=Direction(p["direction"]),
            signal_type=SignalType(p["signal_type"]),
            channels=p["channels"],
        )
        for p in raw
    ]


def _descriptors_from_dicts(raw: list[dict]) -> list[ParamDescriptor]:
    """Convert low-level param descriptor dicts to typed ParamDescriptor objects."""
    return [
        ParamDescriptor(
            name=d["name"],
            default_value=d["default_value"],
            num_steps=d["num_steps"],
            automatable=d["automatable"],
            boolean=d["boolean"],
            label=d["label"],
            group=d["group"],
        )
        for d in raw
    ]


class Node:
    """Wraps a node ID with a Pythonic interface.

    Created by Engine — users never construct Node directly.
    """

    def __init__(self, engine: Engine, node_id: int):
        self._engine = engine
        self._sq: Squeeze = engine._sq
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
        return _ports_from_dicts(self._sq.get_ports(self._id))

    @property
    def audio_inputs(self) -> list[Port]:
        """Audio input ports."""
        return [p for p in self.ports if p.is_audio and p.is_input]

    @property
    def audio_outputs(self) -> list[Port]:
        """Audio output ports."""
        return [p for p in self.ports if p.is_audio and p.is_output]

    @property
    def midi_inputs(self) -> list[Port]:
        """MIDI input ports."""
        return [p for p in self.ports if p.is_midi and p.is_input]

    @property
    def midi_outputs(self) -> list[Port]:
        """MIDI output ports."""
        return [p for p in self.ports if p.is_midi and p.is_output]

    def port(self, name: str) -> PortRef:
        """Return a PortRef for explicit connection.

        Usage: synth.port("sidechain_out") >> output.port("in")
        Raises KeyError if port name not found.
        """
        for p in self.ports:
            if p.name == name:
                return PortRef(self, name)
        raise KeyError(f"no port named {name!r} on {self!r}")

    # --- Parameters ---

    @property
    def params(self) -> ParamMap:
        """Dict-like access to parameters.

        synth.params["Gain"].value = 0.8
        synth.params["Gain"].text   # "0.8 dB"
        """
        return ParamMap(self)

    @property
    def param_descriptors(self) -> list[ParamDescriptor]:
        """Parameter metadata for all parameters on this node."""
        return _descriptors_from_dicts(self._sq.param_descriptors(self._id))

    # --- Connection operator ---

    def __rshift__(self, other: Node) -> Connection:
        """Connect this node to another: synth >> output.

        Auto-matches the first audio output of self to the first audio
        input of other. If no audio ports exist, tries MIDI.

        Raises SqueezeError if no compatible port pair found or cycle.
        """
        from squeeze._low_level import SqueezeError

        if isinstance(other, PortRef):
            return PortRef(self, self._find_output_for(other)).connect(other)

        src_outputs = self.ports
        dst_inputs = other.ports

        out_audio = [p for p in src_outputs if p.is_audio and p.is_output]
        in_audio = [p for p in dst_inputs if p.is_audio and p.is_input]
        if out_audio and in_audio:
            return self._engine.connect(self, out_audio[0].name,
                                        other, in_audio[0].name)

        out_midi = [p for p in src_outputs if p.is_midi and p.is_output]
        in_midi = [p for p in dst_inputs if p.is_midi and p.is_input]
        if out_midi and in_midi:
            return self._engine.connect(self, out_midi[0].name,
                                        other, in_midi[0].name)

        raise SqueezeError(
            f"no compatible port pair: {self!r} outputs="
            f"{[p.name for p in src_outputs if p.is_output]}, "
            f"{other!r} inputs="
            f"{[p.name for p in dst_inputs if p.is_input]}"
        )

    # --- Event scheduling ---

    def note_on(self, beat: float, channel: int, note: int,
                velocity: float) -> bool:
        """Schedule a note-on event at the given beat time."""
        return self._sq.schedule_note_on(self._id, beat, channel, note, velocity)

    def note_off(self, beat: float, channel: int, note: int) -> bool:
        """Schedule a note-off event at the given beat time."""
        return self._sq.schedule_note_off(self._id, beat, channel, note)

    def cc(self, beat: float, channel: int, cc_num: int,
           cc_val: int) -> bool:
        """Schedule a CC event at the given beat time."""
        return self._sq.schedule_cc(self._id, beat, channel, cc_num, cc_val)

    def automate(self, beat: float, param_name: str, value: float) -> bool:
        """Schedule a parameter change at the given beat time."""
        return self._sq.schedule_param_change(self._id, beat, param_name, value)

    # --- Lifecycle ---

    def remove(self) -> bool:
        """Remove this node from the engine. Returns False if not found."""
        return self._sq.remove_node(self._id)

    def __repr__(self) -> str:
        return f"Node({self._id}, {self.name!r})"

    def __eq__(self, other) -> bool:
        if isinstance(other, Node):
            return self._id == other._id
        return NotImplemented

    def __hash__(self) -> int:
        return hash(self._id)


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

    def __rshift__(self, other: PortRef) -> Connection:
        """Connect this port to another port."""
        return self._node._engine.connect(
            self._node, self._port_name,
            other._node, other._port_name,
        )

    def __repr__(self) -> str:
        return f"PortRef({self._node!r}, {self._port_name!r})"


class ParamMap:
    """Dict-like access to a node's parameters.

    map = node.params
    map["Gain"].value           # get
    map["Gain"].value = 0.5     # set
    map["Gain"].text            # display text
    """

    def __init__(self, node: Node):
        self._node = node

    def _descriptor_map(self) -> dict[str, ParamDescriptor]:
        return {d.name: d for d in self._node.param_descriptors}

    def __getitem__(self, name: str) -> Param:
        """Return a Param proxy for the named parameter.
        Raises KeyError if the parameter does not exist on this node.
        """
        descs = self._descriptor_map()
        if name not in descs:
            raise KeyError(f"no parameter named {name!r} on {self._node!r}")
        return Param(self._node, name, descs[name])

    def __contains__(self, name: str) -> bool:
        """Check if a parameter exists by name."""
        return name in self._descriptor_map()

    def __len__(self) -> int:
        """Number of parameters on this node."""
        return len(self._node.param_descriptors)

    def __iter__(self):
        """Iterate over parameter names."""
        for d in self._node.param_descriptors:
            yield d.name

    def items(self):
        """Iterate over (name, Param) pairs."""
        for d in self._node.param_descriptors:
            yield d.name, Param(self._node, d.name, d)

    def keys(self):
        """Iterate over parameter names."""
        return iter(self)

    def values(self):
        """Iterate over Param proxies."""
        for d in self._node.param_descriptors:
            yield Param(self._node, d.name, d)


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
