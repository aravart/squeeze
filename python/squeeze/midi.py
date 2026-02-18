"""MIDI device management sub-objects for the Squeeze Python API."""

from __future__ import annotations

from typing import TYPE_CHECKING

from squeeze._ffi import lib
from squeeze._helpers import SqueezeError, make_error_ptr, check_error, string_list_to_python, encode

if TYPE_CHECKING:
    from squeeze.squeeze import Squeeze


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
