"""Internal helpers for error checking, string/list conversion. Not a public API."""

import ctypes
from squeeze._ffi import lib, LogCallbackType, SqStringList


class SqueezeError(Exception):
    """Raised when a Squeeze operation fails."""


def check_error(error_ptr):
    """Raise SqueezeError if the error pointer was set."""
    if error_ptr.value is not None:
        msg = error_ptr.value.decode()
        lib.sq_free_string(error_ptr)
        raise SqueezeError(msg)


def make_error_ptr():
    """Create a ctypes error pointer for sq_* functions."""
    return ctypes.c_char_p(None)


def decode_string(ptr):
    """Decode a C string (void* pointer) returned by sq_*, free it, return Python str."""
    if ptr is None:
        return ""
    result = ctypes.cast(ptr, ctypes.c_char_p).value.decode()
    lib.sq_free_string(ptr)
    return result


def string_list_to_python(sq_list):
    """Convert SqStringList to Python list[str] and free the C list."""
    result = []
    for i in range(sq_list.count):
        result.append(sq_list.items[i].decode())
    lib.sq_free_string_list(sq_list)
    return result


def encode(s):
    """Encode a Python string for C ABI (bytes or None)."""
    if s is None:
        return None
    return s.encode() if isinstance(s, str) else s


# --- Logging ---

_active_callback = None  # prevent GC of the ctypes callback


def set_log_level(level):
    """Set log level globally. 0=off, 1=warn, 2=info, 3=debug, 4=trace."""
    lib.sq_set_log_level(level)


def set_log_callback(callback):
    """Set a callback to receive log messages. Pass None to revert to stderr.

    The callback receives (level: int, message: str).
    """
    global _active_callback
    if callback is None:
        lib.sq_set_log_callback(LogCallbackType(0), None)
        _active_callback = None
    else:
        def _wrapper(level, message, _user_data):
            callback(level, message.decode() if message else "")

        _active_callback = LogCallbackType(_wrapper)
        lib.sq_set_log_callback(_active_callback, None)
