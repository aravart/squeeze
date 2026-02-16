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


# --- C struct mirrors ---

class SqPortDescriptor(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char_p),
        ("direction", ctypes.c_int),
        ("signal_type", ctypes.c_int),
        ("channels", ctypes.c_int),
    ]

class SqPortList(ctypes.Structure):
    _fields_ = [
        ("ports", ctypes.POINTER(SqPortDescriptor)),
        ("count", ctypes.c_int),
    ]

class SqParamDescriptor(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char_p),
        ("default_value", ctypes.c_float),
        ("num_steps", ctypes.c_int),
        ("automatable", ctypes.c_bool),
        ("boolean_param", ctypes.c_bool),
        ("label", ctypes.c_char_p),
        ("group", ctypes.c_char_p),
    ]

class SqParamDescriptorList(ctypes.Structure):
    _fields_ = [
        ("descriptors", ctypes.POINTER(SqParamDescriptor)),
        ("count", ctypes.c_int),
    ]


LogCallbackType = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p)


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

    # --- Node management ---

    # sq_add_gain
    lib.sq_add_gain.restype = ctypes.c_int
    lib.sq_add_gain.argtypes = [ctypes.c_void_p]

    # sq_remove_node
    lib.sq_remove_node.restype = ctypes.c_bool
    lib.sq_remove_node.argtypes = [ctypes.c_void_p, ctypes.c_int]

    # sq_node_name
    lib.sq_node_name.restype = ctypes.c_char_p
    lib.sq_node_name.argtypes = [ctypes.c_void_p, ctypes.c_int]

    # sq_get_ports
    lib.sq_get_ports.restype = SqPortList
    lib.sq_get_ports.argtypes = [ctypes.c_void_p, ctypes.c_int]

    # sq_free_port_list
    lib.sq_free_port_list.restype = None
    lib.sq_free_port_list.argtypes = [SqPortList]

    # sq_param_descriptors
    lib.sq_param_descriptors.restype = SqParamDescriptorList
    lib.sq_param_descriptors.argtypes = [ctypes.c_void_p, ctypes.c_int]

    # sq_free_param_descriptor_list
    lib.sq_free_param_descriptor_list.restype = None
    lib.sq_free_param_descriptor_list.argtypes = [SqParamDescriptorList]

    # sq_get_param
    lib.sq_get_param.restype = ctypes.c_float
    lib.sq_get_param.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p]

    # sq_set_param
    lib.sq_set_param.restype = ctypes.c_bool
    lib.sq_set_param.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_float]

    # sq_param_text
    lib.sq_param_text.restype = ctypes.c_char_p
    lib.sq_param_text.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p]

    return lib


lib = _load_lib()


def check_error(error_ptr):
    """Raise SqueezeError if the error pointer was set."""
    from squeeze import SqueezeError

    if error_ptr.value is not None:
        msg = error_ptr.value.decode()
        lib.sq_free_string(error_ptr)
        raise SqueezeError(msg)
