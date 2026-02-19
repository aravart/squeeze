"""Clock sub-object for the Squeeze Python API."""

from __future__ import annotations

from typing import Callable, TYPE_CHECKING

from squeeze._ffi import lib, SqClockCallbackType

if TYPE_CHECKING:
    from squeeze.squeeze import Squeeze


class Clock:
    """A beat-driven clock that fires a callback at a given resolution.

    Created via ``Squeeze.clock()``. Do not instantiate directly.
    """

    def __init__(self, engine: Squeeze, resolution: float, latency_ms: float,
                 callback: Callable[[float], None]):
        # Wrap user callback into a ctypes-compatible function.
        # Hold a reference to prevent GC of the C function pointer.
        self._py_callback = callback
        self._c_callback = SqClockCallbackType(
            lambda clock_id, beat, user_data: callback(beat)
        )
        self._engine = engine
        self._ptr = lib.sq_clock_create(
            engine._ptr, resolution, latency_ms, self._c_callback, None
        )
        if not self._ptr:
            raise ValueError(
                f"Failed to create clock (resolution={resolution}, "
                f"latency_ms={latency_ms})"
            )

    @property
    def resolution(self) -> float:
        """Beat interval (e.g., 0.25 for sixteenth notes)."""
        if not self._ptr:
            return 0.0
        return lib.sq_clock_get_resolution(self._ptr)

    @property
    def latency_ms(self) -> float:
        """Lookahead in milliseconds."""
        if not self._ptr:
            return 0.0
        return lib.sq_clock_get_latency(self._ptr)

    def destroy(self) -> None:
        """Unsubscribe and release resources."""
        if self._ptr:
            lib.sq_clock_destroy(self._ptr)
            self._ptr = None

    def __del__(self) -> None:
        self.destroy()

    def __repr__(self) -> str:
        return f"Clock(resolution={self.resolution}, latency_ms={self.latency_ms})"
