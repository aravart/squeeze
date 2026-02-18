"""Internal ctypes declarations for libsqueeze_ffi. Not a public API."""

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

    for subdir in ["../../build", "../../build/Release", "../../build/Debug"]:
        path = os.path.join(base, subdir, name)
        if os.path.exists(path):
            return path

    raise FileNotFoundError(
        f"Cannot find {name}. Build the project first: cmake --build build"
    )


# --- C struct mirrors ---

class SqParamDescriptor(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char_p),
        ("default_value", ctypes.c_float),
        ("min_value", ctypes.c_float),
        ("max_value", ctypes.c_float),
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

class SqStringList(ctypes.Structure):
    _fields_ = [
        ("items", ctypes.POINTER(ctypes.c_char_p)),
        ("count", ctypes.c_int),
    ]

class SqMidiRoute(ctypes.Structure):
    _fields_ = [
        ("id", ctypes.c_int),
        ("device", ctypes.c_char_p),
        ("target_handle", ctypes.c_int),
        ("channel_filter", ctypes.c_int),
        ("note_filter", ctypes.c_int),
    ]

class SqMidiRouteList(ctypes.Structure):
    _fields_ = [
        ("routes", ctypes.POINTER(SqMidiRoute)),
        ("count", ctypes.c_int),
    ]


LogCallbackType = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p)

_V = ctypes.c_void_p   # SqEngine
_I = ctypes.c_int
_F = ctypes.c_float
_D = ctypes.c_double
_B = ctypes.c_bool
_S = ctypes.c_char_p
_EP = ctypes.POINTER(ctypes.c_char_p)


def _load_lib():
    """Load the shared library and declare all function signatures."""
    lib = ctypes.cdll.LoadLibrary(_find_lib())

    def _sig(name, restype, argtypes):
        fn = getattr(lib, name)
        fn.restype = restype
        fn.argtypes = argtypes

    # --- String/list free ---
    _sig("sq_free_string", None, [_V])
    _sig("sq_free_string_list", None, [SqStringList])
    _sig("sq_free_param_descriptor_list", None, [SqParamDescriptorList])
    _sig("sq_free_midi_route_list", None, [SqMidiRouteList])

    # --- Logging ---
    _sig("sq_set_log_level", None, [_I])
    _sig("sq_set_log_callback", None, [LogCallbackType, ctypes.c_void_p])

    # --- Engine lifecycle ---
    _sig("sq_engine_create", _V, [_D, _I, _EP])
    _sig("sq_engine_destroy", None, [_V])
    _sig("sq_version", _V, [_V])  # returns char* (must free)
    _sig("sq_engine_sample_rate", _D, [_V])
    _sig("sq_engine_block_size", _I, [_V])

    # --- Source management ---
    _sig("sq_add_source", _I, [_V, _S])
    _sig("sq_remove_source", _B, [_V, _I])
    _sig("sq_source_count", _I, [_V])
    _sig("sq_source_generator", _I, [_V, _I])
    _sig("sq_source_name", _V, [_V, _I])  # returns char* (must free)
    _sig("sq_source_gain", _F, [_V, _I])
    _sig("sq_source_set_gain", None, [_V, _I, _F])
    _sig("sq_source_pan", _F, [_V, _I])
    _sig("sq_source_set_pan", None, [_V, _I, _F])
    _sig("sq_source_bypassed", _B, [_V, _I])
    _sig("sq_source_set_bypassed", None, [_V, _I, _B])
    _sig("sq_source_midi_assign", None, [_V, _I, _S, _I, _I, _I])

    # --- Bus management ---
    _sig("sq_add_bus", _I, [_V, _S])
    _sig("sq_remove_bus", _B, [_V, _I])
    _sig("sq_bus_count", _I, [_V])
    _sig("sq_master", _I, [_V])
    _sig("sq_bus_name", _V, [_V, _I])  # returns char* (must free)
    _sig("sq_bus_gain", _F, [_V, _I])
    _sig("sq_bus_set_gain", None, [_V, _I, _F])
    _sig("sq_bus_pan", _F, [_V, _I])
    _sig("sq_bus_set_pan", None, [_V, _I, _F])
    _sig("sq_bus_bypassed", _B, [_V, _I])
    _sig("sq_bus_set_bypassed", None, [_V, _I, _B])

    # --- Routing ---
    _sig("sq_route", None, [_V, _I, _I])
    _sig("sq_send", _I, [_V, _I, _I, _F])
    _sig("sq_remove_send", None, [_V, _I, _I])
    _sig("sq_set_send_level", None, [_V, _I, _I, _F])
    _sig("sq_set_send_tap", None, [_V, _I, _I, _S])
    _sig("sq_bus_route", _B, [_V, _I, _I])
    _sig("sq_bus_send", _I, [_V, _I, _I, _F])
    _sig("sq_bus_remove_send", None, [_V, _I, _I])
    _sig("sq_bus_set_send_level", None, [_V, _I, _I, _F])
    _sig("sq_bus_set_send_tap", None, [_V, _I, _I, _S])

    # --- Source chain ---
    _sig("sq_source_append_proc", _I, [_V, _I])
    _sig("sq_source_insert_proc", _I, [_V, _I, _I])
    _sig("sq_source_remove_proc", None, [_V, _I, _I])
    _sig("sq_source_chain_size", _I, [_V, _I])

    # --- Bus chain ---
    _sig("sq_bus_append_proc", _I, [_V, _I])
    _sig("sq_bus_insert_proc", _I, [_V, _I, _I])
    _sig("sq_bus_remove_proc", None, [_V, _I, _I])
    _sig("sq_bus_chain_size", _I, [_V, _I])

    # --- Parameters ---
    _sig("sq_get_param", _F, [_V, _I, _S])
    _sig("sq_set_param", _B, [_V, _I, _S, _F])
    _sig("sq_param_text", _V, [_V, _I, _S])  # returns char* (must free)
    _sig("sq_param_descriptors", SqParamDescriptorList, [_V, _I])

    # --- Metering ---
    _sig("sq_bus_peak", _F, [_V, _I])
    _sig("sq_bus_rms", _F, [_V, _I])

    # --- Batching ---
    _sig("sq_batch_begin", None, [_V])
    _sig("sq_batch_commit", None, [_V])

    # --- Transport ---
    _sig("sq_transport_play", None, [_V])
    _sig("sq_transport_stop", None, [_V])
    _sig("sq_transport_pause", None, [_V])
    _sig("sq_transport_set_tempo", None, [_V, _D])
    _sig("sq_transport_set_time_signature", None, [_V, _I, _I])
    _sig("sq_transport_seek_samples", None, [_V, ctypes.c_int64])
    _sig("sq_transport_seek_beats", None, [_V, _D])
    _sig("sq_transport_set_loop_points", None, [_V, _D, _D])
    _sig("sq_transport_set_looping", None, [_V, _B])
    _sig("sq_transport_position", _D, [_V])
    _sig("sq_transport_tempo", _D, [_V])
    _sig("sq_transport_is_playing", _B, [_V])

    # --- Event scheduling ---
    _sig("sq_schedule_note_on", _B, [_V, _I, _D, _I, _I, _F])
    _sig("sq_schedule_note_off", _B, [_V, _I, _D, _I, _I])
    _sig("sq_schedule_cc", _B, [_V, _I, _D, _I, _I, _I])
    _sig("sq_schedule_param_change", _B, [_V, _I, _D, _S, _F])

    # --- Plugin manager ---
    _sig("sq_load_plugin_cache", _B, [_V, _S, _EP])
    _sig("sq_add_plugin", _I, [_V, _S, _EP])
    _sig("sq_available_plugins", SqStringList, [_V])
    _sig("sq_num_plugins", _I, [_V])

    # --- MIDI device management ---
    _sig("sq_midi_devices", SqStringList, [_V])
    _sig("sq_midi_open", _B, [_V, _S, _EP])
    _sig("sq_midi_close", None, [_V, _S])
    _sig("sq_midi_open_devices", SqStringList, [_V])

    # --- MIDI routing ---
    _sig("sq_midi_route", _I, [_V, _S, _I, _I, _I, _EP])
    _sig("sq_midi_unroute", _B, [_V, _I])
    _sig("sq_midi_routes", SqMidiRouteList, [_V])

    # --- Audio device ---
    _sig("sq_start", _B, [_V, _D, _I, _EP])
    _sig("sq_stop", None, [_V])
    _sig("sq_is_running", _B, [_V])
    _sig("sq_sample_rate", _D, [_V])
    _sig("sq_block_size", _I, [_V])

    # --- Plugin editor ---
    _sig("sq_open_editor", _B, [_V, _I, _EP])
    _sig("sq_close_editor", _B, [_V, _I, _EP])
    _sig("sq_has_editor", _B, [_V, _I])
    _sig("sq_process_events", None, [_I])

    # --- Testing ---
    _sig("sq_render", None, [_V, _I])

    return lib


lib = _load_lib()
