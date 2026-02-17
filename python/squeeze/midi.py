"""MIDI device management sub-objects for the Squeeze high-level API."""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from squeeze._low_level import Squeeze
    from squeeze.engine import Engine
    from squeeze.node import Node


class Midi:
    """Sub-object for MIDI device management. Accessed via engine.midi."""

    def __init__(self, engine: Engine, sq: Squeeze):
        self._engine = engine
        self._sq = sq

    @property
    def devices(self) -> list[MidiDevice]:
        """Available MIDI input devices (may change due to hot-plug)."""
        return [MidiDevice(self._engine, self._sq, name)
                for name in self._sq.midi_devices]

    @property
    def open_devices(self) -> list[MidiDevice]:
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

    def __init__(self, engine: Engine, sq: Squeeze, name: str):
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

    def route(self, node: Node, *, channel: int = 0,
              note: int = -1) -> int:
        """Route this device to a node. Returns route ID.

        channel: 0=all, 1-16=specific channel.
        note: -1=all, 0-127=specific note.
        Raises SqueezeError on failure.
        """
        return self._sq.midi_route(self._name, node.id, channel, note)

    def __rshift__(self, node: Node) -> int:
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
