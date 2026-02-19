"""Internal helpers for error checking, string/list conversion. Not a public API."""

from __future__ import annotations

import ctypes
from typing import Callable

from squeeze._ffi import lib, LogCallbackType, SqStringList


class SqueezeError(Exception):
    """Raised when a Squeeze operation fails."""


def check_error(error_ptr: ctypes.c_char_p) -> None:
    """Raise SqueezeError if the error pointer was set."""
    if error_ptr.value is not None:
        msg = error_ptr.value.decode()
        lib.sq_free_string(error_ptr)
        raise SqueezeError(msg)


def make_error_ptr() -> ctypes.c_char_p:
    """Create a ctypes error pointer for sq_* functions."""
    return ctypes.c_char_p(None)


def decode_string(ptr: ctypes.c_void_p | None) -> str:
    """Decode a C string (void* pointer) returned by sq_*, free it, return Python str."""
    if ptr is None:
        return ""
    raw = ctypes.cast(ptr, ctypes.c_char_p).value
    if raw is None:
        lib.sq_free_string(ptr)
        return ""
    result = raw.decode()
    lib.sq_free_string(ptr)
    return result


def string_list_to_python(sq_list: SqStringList) -> list[str]:
    """Convert SqStringList to Python list[str] and free the C list."""
    result: list[str] = []
    for i in range(sq_list.count):
        result.append(sq_list.items[i].decode())
    lib.sq_free_string_list(sq_list)
    return result


def encode(s: str | bytes | None) -> bytes | None:
    """Encode a Python string for C ABI (bytes or None)."""
    if s is None:
        return None
    return s.encode() if isinstance(s, str) else s


# --- Logging ---

_active_callback: object | None = None  # prevent GC of the ctypes callback


def set_log_level(level: int) -> None:
    """Set log level globally. 0=off, 1=warn, 2=info, 3=debug, 4=trace."""
    lib.sq_set_log_level(level)


def set_log_callback(callback: Callable[[int, str], None] | None) -> None:
    """Set a callback to receive log messages. Pass None to revert to stderr.

    The callback receives (level: int, message: str).
    """
    global _active_callback
    if callback is None:
        lib.sq_set_log_callback(LogCallbackType(0), None)
        _active_callback = None
    else:
        def _wrapper(level: int, message: bytes, _user_data: ctypes.c_void_p) -> None:
            callback(level, message.decode() if message else "")

        _active_callback = LogCallbackType(_wrapper)
        lib.sq_set_log_callback(_active_callback, None)
