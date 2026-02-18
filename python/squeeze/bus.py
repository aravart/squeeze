"""Bus — a summing point with insert chain and routing."""

from __future__ import annotations

from typing import TYPE_CHECKING

from squeeze._ffi import lib
from squeeze._helpers import decode_string
from squeeze.chain import Chain

if TYPE_CHECKING:
    from squeeze.squeeze import Squeeze


class Bus:
    """A summing point with insert chain and routing."""

    def __init__(self, engine: Squeeze, handle: int):
        self._engine = engine
        self._handle = handle

    @property
    def handle(self) -> int:
        return self._handle

    @property
    def name(self) -> str:
        """Bus name."""
        return decode_string(lib.sq_bus_name(self._engine._ptr, self._handle))

    # --- Insert chain ---

    @property
    def chain(self) -> Chain:
        """The insert effects chain."""
        return Chain(self._engine, self._handle, "bus")

    # --- Gain and Pan ---

    @property
    def gain(self) -> float:
        """Linear gain (0.0-1.0+). Default 1.0 (unity)."""
        return lib.sq_bus_gain(self._engine._ptr, self._handle)

    @gain.setter
    def gain(self, value: float) -> None:
        lib.sq_bus_set_gain(self._engine._ptr, self._handle, value)

    @property
    def pan(self) -> float:
        """Stereo pan (-1.0 left to 1.0 right). Default 0.0 (center)."""
        return lib.sq_bus_pan(self._engine._ptr, self._handle)

    @pan.setter
    def pan(self, value: float) -> None:
        lib.sq_bus_set_pan(self._engine._ptr, self._handle, value)

    # --- Bypass ---

    @property
    def bypassed(self) -> bool:
        return lib.sq_bus_bypassed(self._engine._ptr, self._handle)

    @bypassed.setter
    def bypassed(self, value: bool) -> None:
        lib.sq_bus_set_bypassed(self._engine._ptr, self._handle, value)

    # --- Routing ---

    def route_to(self, bus: Bus) -> None:
        """Route this bus's output to another bus."""
        lib.sq_bus_route(self._engine._ptr, self._handle, bus.handle)

    def send(self, bus: Bus, *, level: float = 0.0, tap: str = "post") -> int:
        """Add a send to a bus. Returns send ID.
        tap: "pre" (pre-fader) or "post" (post-fader, default).
        """
        pre_fader = 1 if tap == "pre" else 0
        send_id = lib.sq_bus_send(self._engine._ptr, self._handle, bus.handle, level, pre_fader)
        return send_id

    def remove_send(self, send_id: int) -> None:
        """Remove a send by ID."""
        lib.sq_bus_remove_send(self._engine._ptr, self._handle, send_id)

    def set_send_level(self, send_id: int, level: float) -> None:
        """Change a send's level in dB."""
        lib.sq_bus_set_send_level(self._engine._ptr, self._handle, send_id, level)

    def set_send_tap(self, send_id: int, tap: str) -> None:
        """Change a send's tap point: "pre" or "post"."""
        pre_fader = 1 if tap == "pre" else 0
        lib.sq_bus_set_send_tap(self._engine._ptr, self._handle, send_id, pre_fader)

    # --- Metering ---

    @property
    def peak(self) -> float:
        """Current peak level (0.0-1.0+)."""
        return lib.sq_bus_peak(self._engine._ptr, self._handle)

    @property
    def rms(self) -> float:
        """Current RMS level."""
        return lib.sq_bus_rms(self._engine._ptr, self._handle)

    # --- Lifecycle ---

    def remove(self) -> bool:
        """Remove this bus from the engine. Cannot remove Master."""
        return lib.sq_remove_bus(self._engine._ptr, self._handle)

    def __repr__(self) -> str:
        return f"Bus({self.name!r})"

    def __eq__(self, other) -> bool:
        if isinstance(other, Bus):
            return self._handle == other._handle
        return NotImplemented

    def __hash__(self) -> int:
        return hash(self._handle)
