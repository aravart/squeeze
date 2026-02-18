"""Transport sub-object for the Squeeze Python API."""

from __future__ import annotations

from typing import TYPE_CHECKING

from squeeze._ffi import lib

if TYPE_CHECKING:
    from squeeze.squeeze import Squeeze


class Transport:
    """Sub-object for transport control. Accessed via squeeze.transport."""

    def __init__(self, engine: Squeeze):
        self._engine = engine

    def play(self) -> None:
        """Start playback."""
        lib.sq_transport_play(self._engine._ptr)

    def stop(self) -> None:
        """Stop playback and reset position."""
        lib.sq_transport_stop(self._engine._ptr)

    def pause(self) -> None:
        """Pause playback (position preserved)."""
        lib.sq_transport_pause(self._engine._ptr)

    @property
    def tempo(self) -> float:
        """Current tempo in BPM."""
        return lib.sq_transport_tempo(self._engine._ptr)

    @tempo.setter
    def tempo(self, bpm: float) -> None:
        lib.sq_transport_set_tempo(self._engine._ptr, bpm)

    @property
    def position(self) -> float:
        """Current playback position in beats."""
        return lib.sq_transport_position(self._engine._ptr)

    @property
    def playing(self) -> bool:
        """True if transport is currently playing."""
        return lib.sq_transport_is_playing(self._engine._ptr)

    @playing.setter
    def playing(self, value: bool) -> None:
        if value:
            lib.sq_transport_play(self._engine._ptr)
        else:
            lib.sq_transport_stop(self._engine._ptr)

    def seek(self, *, beats: float = None, samples: int = None) -> None:
        """Seek to a position. Specify exactly one of beats or samples."""
        if (beats is None) == (samples is None):
            raise ValueError("specify exactly one of beats= or samples=")
        if beats is not None:
            lib.sq_transport_seek_beats(self._engine._ptr, beats)
        else:
            lib.sq_transport_seek_samples(self._engine._ptr, samples)

    def set_time_signature(self, numerator: int, denominator: int) -> None:
        """Set the time signature (e.g. 4, 4 for 4/4)."""
        lib.sq_transport_set_time_signature(self._engine._ptr, numerator, denominator)

    def set_loop(self, start: float, end: float) -> None:
        """Set loop points in beats."""
        lib.sq_transport_set_loop_points(self._engine._ptr, start, end)

    @property
    def looping(self) -> bool:
        """Whether looping is enabled."""
        return lib.sq_transport_is_looping(self._engine._ptr)

    @looping.setter
    def looping(self, enabled: bool) -> None:
        lib.sq_transport_set_looping(self._engine._ptr, enabled)
