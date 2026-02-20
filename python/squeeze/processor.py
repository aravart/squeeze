"""Processor — wraps a processor handle in a Source or Bus chain."""

from __future__ import annotations

from typing import TYPE_CHECKING

from squeeze._ffi import lib
from squeeze._helpers import SqueezeError, make_error_ptr, check_error, decode_string, encode
from squeeze.types import ParamDescriptor

if TYPE_CHECKING:
    from squeeze.squeeze import Squeeze


class Processor:
    """Wraps a processor in a Source chain or Bus chain."""

    def __init__(self, engine: Squeeze, handle: int):
        self._engine = engine
        self._handle = handle

    @property
    def handle(self) -> int:
        return self._handle

    # --- Parameters ---

    def get_param(self, name: str) -> float:
        """Get a parameter value by name."""
        return lib.sq_get_param(self._engine._ptr, self._handle, encode(name))

    def set_param(self, name: str, value: float) -> None:
        """Set a parameter value by name."""
        lib.sq_set_param(self._engine._ptr, self._handle, encode(name), value)

    def param_text(self, name: str) -> str:
        """Human-readable display text for a parameter."""
        return decode_string(lib.sq_param_text(self._engine._ptr, self._handle, encode(name)))

    @property
    def param_descriptors(self) -> list[ParamDescriptor]:
        """Parameter metadata for all parameters."""
        desc_list = lib.sq_param_descriptors(self._engine._ptr, self._handle)
        result = []
        for i in range(desc_list.count):
            d = desc_list.descriptors[i]
            result.append(ParamDescriptor(
                name=d.name.decode() if d.name else "",
                default_value=d.default_value,
                min_value=d.min_value,
                max_value=d.max_value,
                num_steps=d.num_steps,
                automatable=d.automatable,
                boolean=d.boolean_param,
                label=d.label.decode() if d.label else "",
                group=d.group.decode() if d.group else "",
            ))
        lib.sq_free_param_descriptor_list(desc_list)
        return result

    @property
    def param_count(self) -> int:
        """Number of parameters."""
        return len(self.param_descriptors)

    @property
    def latency(self) -> int:
        """Processing latency in samples."""
        return 0  # No FFI function yet

    # --- Plugin editor ---

    @property
    def has_editor(self) -> bool:
        """True if this processor has a native editor window."""
        return lib.sq_has_editor(self._engine._ptr, self._handle)

    def open_editor(self) -> None:
        """Open the native plugin editor window."""
        err = make_error_ptr()
        ok = lib.sq_open_editor(self._engine._ptr, self._handle, err)
        if not ok:
            check_error(err)

    def close_editor(self) -> None:
        """Close the plugin editor window."""
        err = make_error_ptr()
        ok = lib.sq_close_editor(self._engine._ptr, self._handle, err)
        if not ok:
            check_error(err)

    # --- Automation ---

    def automate(self, beat: float, param_name: str, value: float) -> bool:
        """Schedule a parameter change at the given beat time."""
        return lib.sq_schedule_param_change(
            self._engine._ptr, self._handle, beat, encode(param_name), value
        )

    def __getitem__(self, name: str) -> float:
        """Get parameter value: ``proc["cutoff"]``."""
        return self.get_param(name)

    def __setitem__(self, name: str, value: float) -> None:
        """Set parameter value: ``proc["cutoff"] = 0.5``."""
        self.set_param(name, value)

    def __repr__(self) -> str:
        return f"Processor({self._handle})"

    def __eq__(self, other: object) -> bool:
        if isinstance(other, Processor):
            return self._handle == other._handle
        return NotImplemented

    def __hash__(self) -> int:
        return hash(self._handle)
