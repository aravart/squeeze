"""Transport sub-object for the Squeeze high-level API."""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from squeeze._low_level import Squeeze


class Transport:
    """Sub-object for transport control. Accessed via engine.transport."""

    def __init__(self, sq: Squeeze):
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
        if (beats is None) == (samples is None):
            raise ValueError("specify exactly one of beats= or samples=")
        if beats is not None:
            self._sq.transport_seek_beats(beats)
        else:
            self._sq.transport_seek_samples(samples)

    def set_time_signature(self, numerator: int, denominator: int) -> None:
        """Set the time signature (e.g. 4, 4 for 4/4)."""
        self._sq.transport_set_time_signature(numerator, denominator)

    def set_loop(self, start: float, end: float) -> None:
        """Set loop points in beats."""
        self._sq.transport_set_loop_points(start, end)

    @property
    def looping(self) -> bool:
        """Whether looping is enabled."""
        raise NotImplementedError("read-only looping query not yet in C ABI")

    @looping.setter
    def looping(self, enabled: bool) -> None:
        self._sq.transport_set_looping(enabled)
