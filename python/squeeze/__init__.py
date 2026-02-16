"""Squeeze — Python client for the Squeeze audio engine."""

import ctypes

from squeeze._ffi import lib, check_error


class SqueezeError(Exception):
    """Raised when a Squeeze FFI call fails."""
    pass


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
