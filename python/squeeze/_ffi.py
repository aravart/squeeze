"""Low-level ctypes bindings to libsqueeze_ffi. Internal module."""

import ctypes
import os
import platform


def _find_lib():
    """Locate libsqueeze_ffi relative to this file."""
    base = os.path.dirname(os.path.abspath(__file__))
    name = {
        "Darwin": "libsqueeze_ffi.dylib",
        "Linux": "libsqueeze_ffi.so",
        "Windows": "squeeze_ffi.dll",
    }.get(platform.system(), "libsqueeze_ffi.so")

    # Check common build locations (relative to python/squeeze/)
    for subdir in ["../../build", "../../build/Release", "../../build/Debug"]:
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

    # sq_set_log_level
    lib.sq_set_log_level.restype = None
    lib.sq_set_log_level.argtypes = [ctypes.c_int]

    # sq_set_log_callback
    lib.sq_set_log_callback.restype = None
    lib.sq_set_log_callback.argtypes = [LogCallbackType, ctypes.c_void_p]

    return lib


LogCallbackType = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p)

lib = _load_lib()


def check_error(error_ptr):
    """Raise SqueezeError if the error pointer was set."""
    from squeeze import SqueezeError

    if error_ptr.value is not None:
        msg = error_ptr.value.decode()
        lib.sq_free_string(error_ptr)
        raise SqueezeError(msg)
