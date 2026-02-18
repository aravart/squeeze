"""Chain — ordered list of processors (the insert rack)."""

from __future__ import annotations

from typing import TYPE_CHECKING

from squeeze._ffi import lib
from squeeze.processor import Processor

if TYPE_CHECKING:
    from squeeze.squeeze import Squeeze


class Chain:
    """Ordered list of processors — the insert rack."""

    def __init__(self, engine: Squeeze, owner_handle: int, owner_type: str):
        """owner_type is 'source' or 'bus'."""
        self._engine = engine
        self._owner = owner_handle
        self._type = owner_type

    def append(self, plugin_path: str = "") -> Processor:
        """Append a processor to the end of the chain. Returns a Processor."""
        if self._type == "source":
            h = lib.sq_source_append_proc(self._engine._ptr, self._owner)
        else:
            h = lib.sq_bus_append_proc(self._engine._ptr, self._owner)
        return Processor(self._engine, h)

    def insert(self, index: int, plugin_path: str = "") -> Processor:
        """Insert a processor at the given index. Returns a Processor."""
        if self._type == "source":
            h = lib.sq_source_insert_proc(self._engine._ptr, self._owner, index)
        else:
            h = lib.sq_bus_insert_proc(self._engine._ptr, self._owner, index)
        return Processor(self._engine, h)

    def remove(self, index: int) -> None:
        """Remove the processor at the given index."""
        if self._type == "source":
            lib.sq_source_remove_proc(self._engine._ptr, self._owner, index)
        else:
            lib.sq_bus_remove_proc(self._engine._ptr, self._owner, index)

    def __len__(self) -> int:
        """Number of processors in the chain."""
        if self._type == "source":
            return lib.sq_source_chain_size(self._engine._ptr, self._owner)
        else:
            return lib.sq_bus_chain_size(self._engine._ptr, self._owner)

    def __getitem__(self, index: int) -> Processor:
        """Access a processor by index. Raises IndexError if out of range."""
        size = len(self)
        if index < 0:
            index += size
        if index < 0 or index >= size:
            raise IndexError(f"chain index {index} out of range (size {size})")
        # Chain doesn't expose proc handles by index yet — use append handle tracking
        # For now, this is a limitation. Return a Processor with handle -1 as placeholder.
        raise IndexError(
            "Chain.__getitem__ requires per-index proc handle query (not yet in FFI)"
        )

    def __repr__(self) -> str:
        return f"Chain({self._type}, size={len(self)})"
