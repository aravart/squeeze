"""Send — a send from a source or bus to a destination bus."""

from __future__ import annotations

from typing import TYPE_CHECKING

from squeeze._ffi import lib

if TYPE_CHECKING:
    from squeeze.squeeze import Squeeze


class Send:
    """A send from a source or bus to a destination bus.

    Returned by ``source.send(bus)`` or ``bus.send(bus)``.
    """

    def __init__(self, engine: Squeeze, owner_handle: int,
                 send_id: int, owner_type: str,
                 level: float, tap: str):
        self._engine = engine
        self._owner = owner_handle
        self._send_id = send_id
        self._type = owner_type  # "source" or "bus"
        self._level = level
        self._tap = tap

    @property
    def send_id(self) -> int:
        """The send ID."""
        return self._send_id

    @property
    def level(self) -> float:
        """Send level in dB."""
        return self._level

    @level.setter
    def level(self, value: float) -> None:
        if self._type == "source":
            lib.sq_set_send_level(
                self._engine._ptr, self._owner, self._send_id, value)
        else:
            lib.sq_bus_set_send_level(
                self._engine._ptr, self._owner, self._send_id, value)
        self._level = value

    @property
    def tap(self) -> str:
        """Tap point: "pre" or "post"."""
        return self._tap

    @tap.setter
    def tap(self, value: str) -> None:
        pre_fader = 1 if value == "pre" else 0
        if self._type == "source":
            lib.sq_set_send_tap(
                self._engine._ptr, self._owner, self._send_id, pre_fader)
        else:
            lib.sq_bus_set_send_tap(
                self._engine._ptr, self._owner, self._send_id, pre_fader)
        self._tap = value

    def remove(self) -> None:
        """Remove this send."""
        if self._type == "source":
            lib.sq_remove_send(self._engine._ptr, self._owner, self._send_id)
        else:
            lib.sq_bus_remove_send(self._engine._ptr, self._owner, self._send_id)

    def __repr__(self) -> str:
        return f"Send(id={self._send_id}, level={self._level}, tap={self._tap!r})"
