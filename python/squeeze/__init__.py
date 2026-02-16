"""Squeeze — Python client for the Squeeze audio engine."""

import ctypes

from squeeze._ffi import lib, check_error, LogCallbackType


class SqueezeError(Exception):
    """Raised when a Squeeze FFI call fails."""
    pass


# --- Module-level Logger API ---

_log_callback_ref = None  # prevent GC of ctypes callback wrapper


def set_log_level(level: int) -> None:
    """Set the global log level. 0=off, 1=warn, 2=info, 3=debug, 4=trace."""
    lib.sq_set_log_level(level)


def set_log_callback(handler=None) -> None:
    """Set a callback to receive log messages. Pass None to revert to stderr.

    The handler signature is: handler(level: int, message: str) -> None
    """
    global _log_callback_ref

    if handler is None:
        _log_callback_ref = None
        lib.sq_set_log_callback(LogCallbackType(0), None)
        return

    def _c_callback(level, message, _user_data):
        handler(level, message.decode() if isinstance(message, bytes) else message)

    _log_callback_ref = LogCallbackType(_c_callback)
    lib.sq_set_log_callback(_log_callback_ref, None)


class Squeeze:
    """Squeeze audio engine."""

    def __init__(self):
        error = ctypes.c_char_p(None)
        self._handle = lib.sq_engine_create(ctypes.byref(error))
        if not self._handle:
            check_error(error)
            raise SqueezeError("Failed to create engine")

    def __del__(self):
        self.close()

    def close(self):
        """Destroy the engine. Safe to call multiple times."""
        if self._handle:
            lib.sq_engine_destroy(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    @property
    def version(self) -> str:
        """Engine version string."""
        raw = lib.sq_version(self._handle)
        return raw.decode()
