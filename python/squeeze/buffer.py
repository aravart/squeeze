"""Buffer — a handle to an audio buffer in the engine."""

from __future__ import annotations

import ctypes
from typing import TYPE_CHECKING

from squeeze._ffi import lib
from squeeze._helpers import decode_string

if TYPE_CHECKING:
    from squeeze.squeeze import Squeeze


class Buffer:
    """A handle to an audio buffer in the engine."""

    def __init__(self, engine: Squeeze, buffer_id: int):
        self._engine = engine
        self._buffer_id = buffer_id

    @property
    def buffer_id(self) -> int:
        """The buffer ID."""
        return self._buffer_id

    @property
    def num_channels(self) -> int:
        """Number of audio channels."""
        return lib.sq_buffer_num_channels(self._engine._ptr, self._buffer_id)

    @property
    def length(self) -> int:
        """Length in samples."""
        return lib.sq_buffer_length(self._engine._ptr, self._buffer_id)

    @property
    def sample_rate(self) -> float:
        """Sample rate in Hz."""
        return lib.sq_buffer_sample_rate(self._engine._ptr, self._buffer_id)

    @property
    def name(self) -> str:
        """Buffer name."""
        ptr = lib.sq_buffer_name(self._engine._ptr, self._buffer_id)
        if ptr is None:
            return ""
        return decode_string(ptr)

    @property
    def length_seconds(self) -> float:
        """Length in seconds."""
        return lib.sq_buffer_length_seconds(self._engine._ptr, self._buffer_id)

    @property
    def write_position(self) -> int:
        """Current write position (samples from buffer start)."""
        return lib.sq_buffer_write_position(self._engine._ptr, self._buffer_id)

    @write_position.setter
    def write_position(self, pos: int) -> None:
        lib.sq_buffer_set_write_position(self._engine._ptr, self._buffer_id, pos)

    @property
    def tempo(self) -> float:
        """Buffer tempo in BPM. 0.0 means not set."""
        return lib.sq_buffer_tempo(self._engine._ptr, self._buffer_id)

    @tempo.setter
    def tempo(self, bpm: float) -> None:
        lib.sq_buffer_set_tempo(self._engine._ptr, self._buffer_id, bpm)

    def read(self, channel: int, offset: int = 0,
             num_samples: int = -1) -> list[float]:
        """Read samples from the buffer.

        Args:
            channel: Channel index.
            offset: Sample offset to start reading from.
            num_samples: Number of samples to read. -1 reads from offset to end.

        Returns:
            List of float sample values.
        """
        if num_samples < 0:
            num_samples = self.length - offset

        if num_samples <= 0:
            return []

        dest = (ctypes.c_float * num_samples)()
        nread = lib.sq_buffer_read(
            self._engine._ptr, self._buffer_id, channel,
            offset, dest, num_samples
        )
        return list(dest[:nread])

    def write(self, channel: int, data: list[float],
              offset: int = 0) -> int:
        """Write samples into the buffer.

        Args:
            channel: Channel index.
            data: List of float sample values to write.
            offset: Sample offset to start writing at.

        Returns:
            Number of samples actually written.
        """
        n = len(data)
        src = (ctypes.c_float * n)(*data)
        return lib.sq_buffer_write(
            self._engine._ptr, self._buffer_id, channel,
            offset, src, n
        )

    def clear(self) -> None:
        """Zero all samples and reset write_position to 0."""
        lib.sq_buffer_clear(self._engine._ptr, self._buffer_id)

    def remove(self) -> bool:
        """Remove this buffer from the engine."""
        return lib.sq_remove_buffer(self._engine._ptr, self._buffer_id)

    def __repr__(self) -> str:
        return f"Buffer({self._buffer_id}, {self.name!r})"

    def __eq__(self, other: object) -> bool:
        if isinstance(other, Buffer):
            return self._buffer_id == other._buffer_id
        return NotImplemented

    def __hash__(self) -> int:
        return hash(self._buffer_id)
