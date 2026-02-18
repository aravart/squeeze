"""Source — a sound generator with insert chain, routing, and MIDI assignment."""

from __future__ import annotations

from typing import TYPE_CHECKING

from squeeze._ffi import lib
from squeeze._helpers import decode_string, encode
from squeeze.chain import Chain
from squeeze.processor import Processor

if TYPE_CHECKING:
    from squeeze.bus import Bus
    from squeeze.squeeze import Squeeze


class Source:
    """A sound generator with insert chain, routing, and MIDI assignment."""

    def __init__(self, engine: Squeeze, handle: int):
        self._engine = engine
        self._handle = handle

    @property
    def handle(self) -> int:
        return self._handle

    @property
    def name(self) -> str:
        """Source name."""
        return decode_string(lib.sq_source_name(self._engine._ptr, self._handle))

    # --- Insert chain ---

    @property
    def chain(self) -> Chain:
        """The insert effects chain."""
        return Chain(self._engine, self._handle, "source")

    # --- Generator ---

    @property
    def generator(self) -> Processor:
        """The generator processor (synth, sampler, etc.)."""
        h = lib.sq_source_generator(self._engine._ptr, self._handle)
        return Processor(self._engine, h)

    # --- Gain and Pan ---

    @property
    def gain(self) -> float:
        """Linear gain (0.0-1.0+). Default 1.0 (unity)."""
        return lib.sq_source_gain(self._engine._ptr, self._handle)

    @gain.setter
    def gain(self, value: float) -> None:
        lib.sq_source_set_gain(self._engine._ptr, self._handle, value)

    @property
    def pan(self) -> float:
        """Stereo pan (-1.0 left to 1.0 right). Default 0.0 (center)."""
        return lib.sq_source_pan(self._engine._ptr, self._handle)

    @pan.setter
    def pan(self, value: float) -> None:
        lib.sq_source_set_pan(self._engine._ptr, self._handle, value)

    # --- Bypass ---

    @property
    def bypassed(self) -> bool:
        return lib.sq_source_bypassed(self._engine._ptr, self._handle)

    @bypassed.setter
    def bypassed(self, value: bool) -> None:
        lib.sq_source_set_bypassed(self._engine._ptr, self._handle, value)

    # --- Routing ---

    def route_to(self, bus: Bus) -> None:
        """Route this source's output to a bus."""
        lib.sq_route(self._engine._ptr, self._handle, bus.handle)

    def send(self, bus: Bus, *, level: float = 0.0, tap: str = "post") -> int:
        """Add a send to a bus. Returns send ID.
        Level is in dB (0.0 = unity).
        tap: "pre" (pre-fader) or "post" (post-fader, default).
        """
        pre_fader = 1 if tap == "pre" else 0
        send_id = lib.sq_send(self._engine._ptr, self._handle, bus.handle, level, pre_fader)
        return send_id

    def remove_send(self, send_id: int) -> None:
        """Remove a send by ID."""
        lib.sq_remove_send(self._engine._ptr, self._handle, send_id)

    def set_send_level(self, send_id: int, level: float) -> None:
        """Change a send's level in dB."""
        lib.sq_set_send_level(self._engine._ptr, self._handle, send_id, level)

    def set_send_tap(self, send_id: int, tap: str) -> None:
        """Change a send's tap point: "pre" or "post"."""
        pre_fader = 1 if tap == "pre" else 0
        lib.sq_set_send_tap(self._engine._ptr, self._handle, send_id, pre_fader)

    # --- MIDI ---

    def midi_assign(self, *, device: str = "", channel: int = 0,
                    note_range: tuple[int, int] = (0, 127)) -> None:
        """Assign MIDI input to this source."""
        lib.sq_source_midi_assign(
            self._engine._ptr, self._handle,
            encode(device), channel, note_range[0], note_range[1]
        )

    # --- Event scheduling ---

    def note_on(self, beat: float, channel: int, note: int,
                velocity: float) -> bool:
        """Schedule a note-on event at the given beat time."""
        return lib.sq_schedule_note_on(
            self._engine._ptr, self._handle, beat, channel, note, velocity
        )

    def note_off(self, beat: float, channel: int, note: int) -> bool:
        """Schedule a note-off event at the given beat time."""
        return lib.sq_schedule_note_off(
            self._engine._ptr, self._handle, beat, channel, note
        )

    def cc(self, beat: float, channel: int, cc_num: int, cc_val: int) -> bool:
        """Schedule a CC event at the given beat time."""
        return lib.sq_schedule_cc(
            self._engine._ptr, self._handle, beat, channel, cc_num, cc_val
        )

    # --- Lifecycle ---

    def remove(self) -> bool:
        """Remove this source from the engine."""
        return lib.sq_remove_source(self._engine._ptr, self._handle)

    def __repr__(self) -> str:
        return f"Source({self.name!r})"

    def __eq__(self, other) -> bool:
        if isinstance(other, Source):
            return self._handle == other._handle
        return NotImplemented

    def __hash__(self) -> int:
        return hash(self._handle)
