"""Pythonic wrapper around the Squeeze C ABI (squeeze_ffi)."""

import ctypes
import os
import platform


class SqueezeError(Exception):
    """Raised when a Squeeze FFI call fails."""
    pass


def _find_lib():
    """Locate libsqueeze_ffi relative to this file."""
    base = os.path.dirname(os.path.abspath(__file__))
    name = {
        "Darwin": "libsqueeze_ffi.dylib",
        "Linux": "libsqueeze_ffi.so",
        "Windows": "squeeze_ffi.dll",
    }.get(platform.system(), "libsqueeze_ffi.so")

    # Check common build locations
    for subdir in ["../build", "../build/Release", "../build/Debug"]:
        path = os.path.join(base, subdir, name)
        if os.path.exists(path):
            return path

    raise FileNotFoundError(
        f"Cannot find {name}. Build the project first: cmake --build build"
    )


def _load_lib():
    """Load the shared library and declare all function signatures."""
    lib = ctypes.cdll.LoadLibrary(_find_lib())

    # sq_free_string
    lib.sq_free_string.restype = None
    lib.sq_free_string.argtypes = [ctypes.c_char_p]

    # sq_engine_create
    lib.sq_engine_create.restype = ctypes.c_void_p
    lib.sq_engine_create.argtypes = [ctypes.POINTER(ctypes.c_char_p)]

    # sq_engine_destroy
    lib.sq_engine_destroy.restype = None
    lib.sq_engine_destroy.argtypes = [ctypes.c_void_p]

    # sq_version
    lib.sq_version.restype = ctypes.c_char_p
    lib.sq_version.argtypes = [ctypes.c_void_p]

    return lib


_lib = _load_lib()


def _check_error(error_ptr):
    """Raise SqueezeError if the error pointer was set."""
    if error_ptr.value is not None:
        msg = error_ptr.value.decode()
        _lib.sq_free_string(error_ptr)
        raise SqueezeError(msg)


class Squeeze:
    """Squeeze audio engine."""

    def __init__(self):
        error = ctypes.c_char_p(None)
        self._handle = _lib.sq_engine_create(ctypes.byref(error))
        if not self._handle:
            _check_error(error)
            raise SqueezeError("Failed to create engine")

    def __del__(self):
        self.close()

    def close(self):
        """Destroy the engine. Safe to call multiple times."""
        if self._handle:
            _lib.sq_engine_destroy(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    @property
    def version(self) -> str:
        """Engine version string."""
        raw = _lib.sq_version(self._handle)
        return raw.decode()
