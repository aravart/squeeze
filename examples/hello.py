"""Squeeze v2 — Python FFI hello world."""

import ctypes
import os

# Load the shared library
lib_dir = os.path.join(os.path.dirname(__file__), "..", "build")
lib_path = os.path.join(lib_dir, "libsqueeze_ffi.dylib")
sq = ctypes.cdll.LoadLibrary(lib_path)

# Declare function signatures
sq.sq_engine_create.restype = ctypes.c_void_p
sq.sq_engine_create.argtypes = [ctypes.POINTER(ctypes.c_char_p)]

sq.sq_engine_destroy.restype = None
sq.sq_engine_destroy.argtypes = [ctypes.c_void_p]

sq.sq_version.restype = ctypes.c_char_p
sq.sq_version.argtypes = [ctypes.c_void_p]

sq.sq_free_string.restype = None
sq.sq_free_string.argtypes = [ctypes.c_char_p]

# Create engine
error = ctypes.c_char_p(None)
engine = sq.sq_engine_create(ctypes.byref(error))
if not engine:
    print(f"Failed to create engine: {error.value.decode()}")
    sq.sq_free_string(error)
    exit(1)

# Get version
version = sq.sq_version(engine)
print(f"Squeeze {version.decode()}")

# Clean up
sq.sq_engine_destroy(engine)
print("Engine destroyed. Hello from Python!")
