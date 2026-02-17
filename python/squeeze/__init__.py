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

    # --- Node management ---

    def add_gain(self) -> int:
        """Add a GainNode. Returns node id."""
        return lib.sq_add_gain(self._handle)

    def add_test_synth(self) -> int:
        """Add a test synth (PluginNode wrapping TestProcessor).
        0 audio in, 2 audio out, accepts MIDI. Has 'Gain' and 'Mix' params.
        Returns node id."""
        return lib.sq_add_test_synth(self._handle)

    def remove_node(self, node_id: int) -> bool:
        """Remove a node by id. Returns False if not found."""
        return lib.sq_remove_node(self._handle, node_id)

    def node_name(self, node_id: int) -> str:
        """Return the node's type name, or empty string if invalid."""
        raw = lib.sq_node_name(self._handle, node_id)
        if raw is None:
            return ""
        return raw.decode()

    def get_ports(self, node_id: int) -> list:
        """Return list of port dicts for a node."""
        port_list = lib.sq_get_ports(self._handle, node_id)
        result = []
        for i in range(port_list.count):
            p = port_list.ports[i]
            result.append({
                "name": p.name.decode(),
                "direction": "input" if p.direction == 0 else "output",
                "signal_type": "audio" if p.signal_type == 0 else "midi",
                "channels": p.channels,
            })
        lib.sq_free_port_list(port_list)
        return result

    def param_descriptors(self, node_id: int) -> list:
        """Return list of parameter descriptor dicts for a node."""
        desc_list = lib.sq_param_descriptors(self._handle, node_id)
        result = []
        for i in range(desc_list.count):
            d = desc_list.descriptors[i]
            result.append({
                "name": d.name.decode(),
                "default_value": d.default_value,
                "num_steps": d.num_steps,
                "automatable": d.automatable,
                "boolean": d.boolean_param,
                "label": d.label.decode(),
                "group": d.group.decode(),
            })
        lib.sq_free_param_descriptor_list(desc_list)
        return result

    def get_param(self, node_id: int, name: str) -> float:
        """Get a parameter value by name."""
        return lib.sq_get_param(self._handle, node_id, name.encode())

    def set_param(self, node_id: int, name: str, value: float) -> bool:
        """Set a parameter value by name."""
        return lib.sq_set_param(self._handle, node_id, name.encode(), value)

    def param_text(self, node_id: int, name: str) -> str:
        """Get parameter display text."""
        raw = lib.sq_param_text(self._handle, node_id, name.encode())
        if raw is None:
            return ""
        return raw.decode()

    # --- Connection management ---

    def connect(self, src_node: int, src_port: str,
                dst_node: int, dst_port: str) -> int:
        """Connect two ports. Returns connection id. Raises SqueezeError on failure."""
        error = ctypes.c_char_p(None)
        conn_id = lib.sq_connect(self._handle,
                                 src_node, src_port.encode(),
                                 dst_node, dst_port.encode(),
                                 ctypes.byref(error))
        if conn_id < 0:
            check_error(error)
            raise SqueezeError("Connection failed")
        return conn_id

    def disconnect(self, conn_id: int) -> bool:
        """Disconnect by connection id. Returns False if not found."""
        return lib.sq_disconnect(self._handle, conn_id)

    def connections(self) -> list:
        """Return list of connection dicts."""
        conn_list = lib.sq_connections(self._handle)
        result = []
        for i in range(conn_list.count):
            c = conn_list.connections[i]
            result.append({
                "id": c.id,
                "src_node": c.src_node,
                "src_port": c.src_port.decode(),
                "dst_node": c.dst_node,
                "dst_port": c.dst_port.decode(),
            })
        lib.sq_free_connection_list(conn_list)
        return result

    # --- Output node ---

    @property
    def output(self) -> int:
        """The built-in output node id."""
        return lib.sq_output_node(self._handle)

    def node_count(self) -> int:
        """Return total number of nodes (including output)."""
        return lib.sq_node_count(self._handle)

    # --- Testing ---

    def prepare_for_testing(self, sample_rate: float = 44100.0, block_size: int = 512):
        """Prepare engine for headless testing."""
        lib.sq_prepare_for_testing(self._handle, sample_rate, block_size)

    def render(self, num_samples: int = 512):
        """Render one block in test mode."""
        lib.sq_render(self._handle, num_samples)

    # --- Transport ---

    def transport_play(self):
        lib.sq_transport_play(self._handle)

    def transport_stop(self):
        lib.sq_transport_stop(self._handle)

    def transport_pause(self):
        lib.sq_transport_pause(self._handle)

    def transport_set_tempo(self, bpm: float):
        lib.sq_transport_set_tempo(self._handle, bpm)

    def transport_set_time_signature(self, numerator: int, denominator: int):
        lib.sq_transport_set_time_signature(self._handle, numerator, denominator)

    def transport_seek_samples(self, samples: int):
        lib.sq_transport_seek_samples(self._handle, samples)

    def transport_seek_beats(self, beats: float):
        lib.sq_transport_seek_beats(self._handle, beats)

    def transport_set_loop_points(self, start_beats: float, end_beats: float):
        lib.sq_transport_set_loop_points(self._handle, start_beats, end_beats)

    def transport_set_looping(self, enabled: bool):
        lib.sq_transport_set_looping(self._handle, enabled)

    @property
    def transport_position(self) -> float:
        return lib.sq_transport_position(self._handle)

    @property
    def transport_tempo(self) -> float:
        return lib.sq_transport_tempo(self._handle)

    @property
    def transport_is_playing(self) -> bool:
        return lib.sq_transport_is_playing(self._handle)

    # --- Event scheduling ---

    def schedule_note_on(self, node_id: int, beat_time: float,
                         channel: int, note: int, velocity: float) -> bool:
        return lib.sq_schedule_note_on(self._handle, node_id, beat_time,
                                       channel, note, velocity)

    def schedule_note_off(self, node_id: int, beat_time: float,
                          channel: int, note: int) -> bool:
        return lib.sq_schedule_note_off(self._handle, node_id, beat_time,
                                        channel, note)

    def schedule_cc(self, node_id: int, beat_time: float,
                    channel: int, cc_num: int, cc_val: int) -> bool:
        return lib.sq_schedule_cc(self._handle, node_id, beat_time,
                                  channel, cc_num, cc_val)

    def schedule_param_change(self, node_id: int, beat_time: float,
                              param_name: str, value: float) -> bool:
        return lib.sq_schedule_param_change(self._handle, node_id, beat_time,
                                            param_name.encode(), value)
