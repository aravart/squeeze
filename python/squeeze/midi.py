"""MIDI device management sub-objects for the Squeeze Python API."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from squeeze._ffi import lib
from squeeze._helpers import SqueezeError, make_error_ptr, check_error, string_list_to_python, encode

if TYPE_CHECKING:
    from squeeze.squeeze import Squeeze


@dataclass
class MidiRouteInfo:
    """Information about an active MIDI route."""
    id: int
    device: str
    target_handle: int
    channel_filter: int
    note_low: int
    note_high: int


class Midi:
    """Sub-object for MIDI device management. Accessed via squeeze.midi."""

    def __init__(self, engine: Squeeze):
        self._engine = engine

    @property
    def devices(self) -> list[MidiDevice]:
        """Available MIDI input devices."""
        names = string_list_to_python(lib.sq_midi_devices(self._engine._ptr))
        return [MidiDevice(self._engine, name) for name in names]

    @property
    def open_devices(self) -> list[MidiDevice]:
        """Currently open MIDI devices."""
        names = string_list_to_python(lib.sq_midi_open_devices(self._engine._ptr))
        return [MidiDevice(self._engine, name) for name in names]

    def route(self, device: str, source_handle: int, *,
              channel: int = 0, note_range: tuple[int, int] = (0, 127)) -> int:
        """Create a MIDI route from a device to a source. Returns route ID.

        Args:
            device: MIDI device name (must be open).
            source_handle: Target source handle.
            channel: Channel filter (0 = all, 1-16 = specific).
            note_range: (low, high) note range filter.
        """
        err = make_error_ptr()
        route_id = lib.sq_midi_route(
            self._engine._ptr, encode(device), source_handle,
            channel, note_range[0], note_range[1], err
        )
        if route_id < 0:
            check_error(err)
            raise SqueezeError("Failed to create MIDI route")
        return route_id

    def unroute(self, route_id: int) -> bool:
        """Remove a MIDI route by ID. Returns False if not found."""
        return lib.sq_midi_unroute(self._engine._ptr, route_id)

    @property
    def routes(self) -> list[MidiRouteInfo]:
        """Active MIDI routes."""
        route_list = lib.sq_midi_routes(self._engine._ptr)
        result = []
        for i in range(route_list.count):
            r = route_list.routes[i]
            result.append(MidiRouteInfo(
                id=r.id,
                device=r.device.decode() if r.device else "",
                target_handle=r.target_handle,
                channel_filter=r.channel_filter,
                note_low=r.note_low,
                note_high=r.note_high,
            ))
        lib.sq_free_midi_route_list(route_list)
        return result


class MidiDevice:
    """Represents a MIDI input device."""

    def __init__(self, engine: Squeeze, name: str):
        self._engine = engine
        self._name = name

    @property
    def name(self) -> str:
        return self._name

    def open(self) -> None:
        """Open this MIDI device. Raises SqueezeError on failure."""
        err = make_error_ptr()
        ok = lib.sq_midi_open(self._engine._ptr, encode(self._name), err)
        if not ok:
            check_error(err)

    def close(self) -> None:
        """Close this MIDI device. No-op if not open."""
        lib.sq_midi_close(self._engine._ptr, encode(self._name))

    def __repr__(self) -> str:
        return f"MidiDevice({self._name!r})"
