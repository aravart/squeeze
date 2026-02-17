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

class SqConnection(ctypes.Structure):
    _fields_ = [
        ("id", ctypes.c_int),
        ("src_node", ctypes.c_int),
        ("src_port", ctypes.c_char_p),
        ("dst_node", ctypes.c_int),
        ("dst_port", ctypes.c_char_p),
    ]

class SqConnectionList(ctypes.Structure):
    _fields_ = [
        ("connections", ctypes.POINTER(SqConnection)),
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
        ("node_id", ctypes.c_int),
        ("channel_filter", ctypes.c_int),
        ("note_filter", ctypes.c_int),
    ]

class SqMidiRouteList(ctypes.Structure):
    _fields_ = [
        ("routes", ctypes.POINTER(SqMidiRoute)),
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

    # sq_add_test_synth
    lib.sq_add_test_synth.restype = ctypes.c_int
    lib.sq_add_test_synth.argtypes = [ctypes.c_void_p]

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

    # --- Connection management ---

    # sq_connect
    lib.sq_connect.restype = ctypes.c_int
    lib.sq_connect.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p,
                               ctypes.c_int, ctypes.c_char_p,
                               ctypes.POINTER(ctypes.c_char_p)]

    # sq_disconnect
    lib.sq_disconnect.restype = ctypes.c_bool
    lib.sq_disconnect.argtypes = [ctypes.c_void_p, ctypes.c_int]

    # sq_connections
    lib.sq_connections.restype = SqConnectionList
    lib.sq_connections.argtypes = [ctypes.c_void_p]

    # sq_free_connection_list
    lib.sq_free_connection_list.restype = None
    lib.sq_free_connection_list.argtypes = [SqConnectionList]

    # --- Output node and node count ---

    # sq_output_node
    lib.sq_output_node.restype = ctypes.c_int
    lib.sq_output_node.argtypes = [ctypes.c_void_p]

    # sq_node_count
    lib.sq_node_count.restype = ctypes.c_int
    lib.sq_node_count.argtypes = [ctypes.c_void_p]

    # --- Transport ---

    lib.sq_transport_play.restype = None
    lib.sq_transport_play.argtypes = [ctypes.c_void_p]

    lib.sq_transport_stop.restype = None
    lib.sq_transport_stop.argtypes = [ctypes.c_void_p]

    lib.sq_transport_pause.restype = None
    lib.sq_transport_pause.argtypes = [ctypes.c_void_p]

    lib.sq_transport_set_tempo.restype = None
    lib.sq_transport_set_tempo.argtypes = [ctypes.c_void_p, ctypes.c_double]

    lib.sq_transport_set_time_signature.restype = None
    lib.sq_transport_set_time_signature.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]

    lib.sq_transport_seek_samples.restype = None
    lib.sq_transport_seek_samples.argtypes = [ctypes.c_void_p, ctypes.c_int64]

    lib.sq_transport_seek_beats.restype = None
    lib.sq_transport_seek_beats.argtypes = [ctypes.c_void_p, ctypes.c_double]

    lib.sq_transport_set_loop_points.restype = None
    lib.sq_transport_set_loop_points.argtypes = [ctypes.c_void_p, ctypes.c_double, ctypes.c_double]

    lib.sq_transport_set_looping.restype = None
    lib.sq_transport_set_looping.argtypes = [ctypes.c_void_p, ctypes.c_bool]

    lib.sq_transport_position.restype = ctypes.c_double
    lib.sq_transport_position.argtypes = [ctypes.c_void_p]

    lib.sq_transport_tempo.restype = ctypes.c_double
    lib.sq_transport_tempo.argtypes = [ctypes.c_void_p]

    lib.sq_transport_is_playing.restype = ctypes.c_bool
    lib.sq_transport_is_playing.argtypes = [ctypes.c_void_p]

    # --- Event scheduling ---

    lib.sq_schedule_note_on.restype = ctypes.c_bool
    lib.sq_schedule_note_on.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_double,
                                        ctypes.c_int, ctypes.c_int, ctypes.c_float]

    lib.sq_schedule_note_off.restype = ctypes.c_bool
    lib.sq_schedule_note_off.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_double,
                                         ctypes.c_int, ctypes.c_int]

    lib.sq_schedule_cc.restype = ctypes.c_bool
    lib.sq_schedule_cc.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_double,
                                   ctypes.c_int, ctypes.c_int, ctypes.c_int]

    lib.sq_schedule_param_change.restype = ctypes.c_bool
    lib.sq_schedule_param_change.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_double,
                                             ctypes.c_char_p, ctypes.c_float]

    # --- Audio device ---

    lib.sq_start.restype = ctypes.c_bool
    lib.sq_start.argtypes = [ctypes.c_void_p, ctypes.c_double, ctypes.c_int,
                              ctypes.POINTER(ctypes.c_char_p)]

    lib.sq_stop.restype = None
    lib.sq_stop.argtypes = [ctypes.c_void_p]

    lib.sq_is_running.restype = ctypes.c_bool
    lib.sq_is_running.argtypes = [ctypes.c_void_p]

    lib.sq_sample_rate.restype = ctypes.c_double
    lib.sq_sample_rate.argtypes = [ctypes.c_void_p]

    lib.sq_block_size.restype = ctypes.c_int
    lib.sq_block_size.argtypes = [ctypes.c_void_p]

    # --- Plugin manager ---

    lib.sq_load_plugin_cache.restype = ctypes.c_bool
    lib.sq_load_plugin_cache.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                          ctypes.POINTER(ctypes.c_char_p)]

    lib.sq_add_plugin.restype = ctypes.c_int
    lib.sq_add_plugin.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                   ctypes.POINTER(ctypes.c_char_p)]

    lib.sq_available_plugins.restype = SqStringList
    lib.sq_available_plugins.argtypes = [ctypes.c_void_p]

    lib.sq_free_string_list.restype = None
    lib.sq_free_string_list.argtypes = [SqStringList]

    lib.sq_num_plugins.restype = ctypes.c_int
    lib.sq_num_plugins.argtypes = [ctypes.c_void_p]

    # --- MIDI device management ---

    lib.sq_midi_devices.restype = SqStringList
    lib.sq_midi_devices.argtypes = [ctypes.c_void_p]

    lib.sq_midi_open.restype = ctypes.c_bool
    lib.sq_midi_open.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                  ctypes.POINTER(ctypes.c_char_p)]

    lib.sq_midi_close.restype = None
    lib.sq_midi_close.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

    lib.sq_midi_open_devices.restype = SqStringList
    lib.sq_midi_open_devices.argtypes = [ctypes.c_void_p]

    # --- MIDI routing ---

    lib.sq_midi_route.restype = ctypes.c_int
    lib.sq_midi_route.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int,
                                   ctypes.c_int, ctypes.c_int,
                                   ctypes.POINTER(ctypes.c_char_p)]

    lib.sq_midi_unroute.restype = ctypes.c_bool
    lib.sq_midi_unroute.argtypes = [ctypes.c_void_p, ctypes.c_int]

    lib.sq_midi_routes.restype = SqMidiRouteList
    lib.sq_midi_routes.argtypes = [ctypes.c_void_p]

    lib.sq_free_midi_route_list.restype = None
    lib.sq_free_midi_route_list.argtypes = [SqMidiRouteList]

    # --- Plugin editor ---

    lib.sq_open_editor.restype = ctypes.c_bool
    lib.sq_open_editor.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                    ctypes.POINTER(ctypes.c_char_p)]

    lib.sq_close_editor.restype = ctypes.c_bool
    lib.sq_close_editor.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                     ctypes.POINTER(ctypes.c_char_p)]

    lib.sq_has_editor.restype = ctypes.c_bool
    lib.sq_has_editor.argtypes = [ctypes.c_void_p, ctypes.c_int]

    lib.sq_process_events.restype = None
    lib.sq_process_events.argtypes = [ctypes.c_int]

    # --- Testing ---

    lib.sq_prepare_for_testing.restype = None
    lib.sq_prepare_for_testing.argtypes = [ctypes.c_void_p, ctypes.c_double, ctypes.c_int]

    lib.sq_render.restype = None
    lib.sq_render.argtypes = [ctypes.c_void_p, ctypes.c_int]

    return lib


lib = _load_lib()


def check_error(error_ptr):
    """Raise SqueezeError if the error pointer was set."""
    from squeeze import SqueezeError

    if error_ptr.value is not None:
        msg = error_ptr.value.decode()
        lib.sq_free_string(error_ptr)
        raise SqueezeError(msg)
