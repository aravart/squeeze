"""Tests for the Squeeze high-level Python API."""

import pytest
from squeeze import (
    Engine, Node, PortRef, Param, ParamMap,
    Transport, Midi, MidiDevice,
    Direction, SignalType, Port, ParamDescriptor, Connection,
    Squeeze, SqueezeError, set_log_level, set_log_callback,
)


@pytest.fixture
def eng():
    """Create a high-level Engine, destroy it after the test."""
    e = Engine(44100.0, 512)
    yield e
    e.close()


# --- Engine lifecycle ---

class TestEngineLifecycle:
    def test_create_and_close(self):
        e = Engine(44100.0, 512)
        e.close()

    def test_context_manager(self):
        with Engine(44100.0, 512) as e:
            assert e.version

    def test_version(self, eng):
        assert isinstance(eng.version, str)
        assert len(eng.version) > 0

    def test_sq_property(self, eng):
        assert isinstance(eng.sq, Squeeze)


# --- Node creation ---

class TestNodeCreation:
    def test_add_gain(self, eng):
        gain = eng.add_gain()
        assert isinstance(gain, Node)
        assert gain.name == "gain"

    def test_add_test_synth(self, eng):
        synth = eng.add_test_synth()
        assert isinstance(synth, Node)
        assert synth.name == "test_synth"

    def test_output_node(self, eng):
        out = eng.output
        assert isinstance(out, Node)
        assert out.id > 0

    def test_node_count(self, eng):
        initial = eng.node_count
        eng.add_gain()
        assert eng.node_count == initial + 1

    def test_node_wrapping(self, eng):
        """engine.node(id) wraps a raw ID."""
        raw_id = eng.sq.add_gain()
        node = eng.node(raw_id)
        assert isinstance(node, Node)
        assert node.id == raw_id
        assert node.name == "gain"


# --- Node properties ---

class TestNodeProperties:
    def test_node_id(self, eng):
        gain = eng.add_gain()
        assert isinstance(gain.id, int)
        assert gain.id > 0

    def test_node_name(self, eng):
        gain = eng.add_gain()
        assert gain.name == "gain"

    def test_ports(self, eng):
        gain = eng.add_gain()
        ports = gain.ports
        assert len(ports) == 2
        assert all(isinstance(p, Port) for p in ports)

    def test_audio_inputs(self, eng):
        gain = eng.add_gain()
        ai = gain.audio_inputs
        assert len(ai) == 1
        assert ai[0].is_audio
        assert ai[0].is_input

    def test_audio_outputs(self, eng):
        gain = eng.add_gain()
        ao = gain.audio_outputs
        assert len(ao) == 1
        assert ao[0].is_audio
        assert ao[0].is_output

    def test_midi_inputs(self, eng):
        synth = eng.add_test_synth()
        mi = synth.midi_inputs
        assert len(mi) >= 1
        assert mi[0].is_midi
        assert mi[0].is_input

    def test_midi_outputs(self, eng):
        gain = eng.add_gain()
        mo = gain.midi_outputs
        assert len(mo) == 0

    def test_port_properties(self, eng):
        gain = eng.add_gain()
        p = gain.ports[0]  # "in", audio, input
        assert p.name == "in"
        assert p.direction == Direction.INPUT
        assert p.signal_type == SignalType.AUDIO
        assert p.channels == 2
        assert p.is_audio
        assert p.is_input
        assert not p.is_midi
        assert not p.is_output


# --- Node.port() ---

class TestPortRef:
    def test_port_returns_portref(self, eng):
        gain = eng.add_gain()
        ref = gain.port("in")
        assert isinstance(ref, PortRef)
        assert ref.node is gain
        assert ref.port_name == "in"

    def test_port_keyerror_on_unknown(self, eng):
        gain = eng.add_gain()
        with pytest.raises(KeyError, match="no_such_port"):
            gain.port("no_such_port")

    def test_portref_rshift(self, eng):
        a = eng.add_gain()
        b = eng.add_gain()
        conn = a.port("out") >> b.port("in")
        assert isinstance(conn, Connection)
        assert conn.src_port == "out"
        assert conn.dst_port == "in"


# --- ParamMap ---

class TestParamMap:
    def test_getitem(self, eng):
        gain = eng.add_gain()
        p = gain.params["gain"]
        assert isinstance(p, Param)

    def test_getitem_keyerror(self, eng):
        gain = eng.add_gain()
        with pytest.raises(KeyError, match="unknown"):
            gain.params["unknown"]

    def test_contains(self, eng):
        gain = eng.add_gain()
        assert "gain" in gain.params
        assert "unknown" not in gain.params

    def test_len(self, eng):
        gain = eng.add_gain()
        assert len(gain.params) == 1

    def test_iter(self, eng):
        gain = eng.add_gain()
        names = list(gain.params)
        assert "gain" in names

    def test_items(self, eng):
        gain = eng.add_gain()
        items = list(gain.params.items())
        assert len(items) == 1
        name, param = items[0]
        assert name == "gain"
        assert isinstance(param, Param)

    def test_keys(self, eng):
        gain = eng.add_gain()
        assert "gain" in list(gain.params.keys())

    def test_values(self, eng):
        gain = eng.add_gain()
        vals = list(gain.params.values())
        assert len(vals) == 1
        assert isinstance(vals[0], Param)


# --- Param ---

class TestParam:
    def test_value_get(self, eng):
        gain = eng.add_gain()
        p = gain.params["gain"]
        assert p.value == pytest.approx(1.0)

    def test_value_set(self, eng):
        gain = eng.add_gain()
        gain.params["gain"].value = 0.5
        assert gain.params["gain"].value == pytest.approx(0.5)

    def test_text(self, eng):
        gain = eng.add_gain()
        p = gain.params["gain"]
        assert isinstance(p.text, str)
        assert len(p.text) > 0

    def test_descriptor(self, eng):
        gain = eng.add_gain()
        p = gain.params["gain"]
        d = p.descriptor
        assert isinstance(d, ParamDescriptor)
        assert d.name == "gain"
        assert d.default_value == pytest.approx(1.0)
        assert d.automatable is True
        assert d.boolean is False

    def test_default(self, eng):
        gain = eng.add_gain()
        p = gain.params["gain"]
        assert p.default == pytest.approx(1.0)

    def test_name(self, eng):
        gain = eng.add_gain()
        p = gain.params["gain"]
        assert p.name == "gain"


# --- >> operator ---

class TestRshiftOperator:
    def test_auto_connect_audio(self, eng):
        """synth >> output connects audio out to audio in."""
        synth = eng.add_test_synth()
        out = eng.output
        conn = synth >> out
        assert isinstance(conn, Connection)
        assert conn.src_node == synth.id
        assert conn.dst_node == out.id

    def test_auto_connect_between_gains(self, eng):
        a = eng.add_gain()
        b = eng.add_gain()
        conn = a >> b
        assert isinstance(conn, Connection)
        assert conn.src_port == "out"
        assert conn.dst_port == "in"

    def test_cycle_raises(self, eng):
        a = eng.add_gain()
        b = eng.add_gain()
        a >> b
        with pytest.raises(SqueezeError):
            b >> a

    def test_returns_connection(self, eng):
        a = eng.add_gain()
        b = eng.add_gain()
        conn = a >> b
        assert isinstance(conn, Connection)
        assert conn.id >= 0


# --- Engine.connect / disconnect ---

class TestEngineConnections:
    def test_connect(self, eng):
        a = eng.add_gain()
        b = eng.add_gain()
        conn = eng.connect(a, "out", b, "in")
        assert isinstance(conn, Connection)
        assert conn.src_node == a.id
        assert conn.src_port == "out"
        assert conn.dst_node == b.id
        assert conn.dst_port == "in"

    def test_disconnect_with_connection(self, eng):
        a = eng.add_gain()
        b = eng.add_gain()
        conn = a >> b
        assert eng.disconnect(conn) is True
        assert len(eng.connections) == 0

    def test_disconnect_with_int(self, eng):
        a = eng.add_gain()
        b = eng.add_gain()
        conn = a >> b
        assert eng.disconnect(conn.id) is True

    def test_connections_property(self, eng):
        a = eng.add_gain()
        b = eng.add_gain()
        conn = a >> b
        conns = eng.connections
        assert len(conns) == 1
        assert isinstance(conns[0], Connection)
        assert conns[0].id == conn.id

    def test_connections_empty(self, eng):
        assert eng.connections == []


# --- Transport ---

class TestTransport:
    def test_transport_is_transport(self, eng):
        assert isinstance(eng.transport, Transport)

    def test_transport_lazy(self, eng):
        """transport property returns same object."""
        assert eng.transport is eng.transport

    def test_play_stop_do_not_crash(self, eng):

        eng.transport.play()
        eng.transport.stop()

    def test_pause_does_not_crash(self, eng):

        eng.transport.play()
        eng.transport.pause()

    def test_tempo_default(self, eng):
        # Transport is currently a stub — tempo reads as 120.0
        assert eng.transport.tempo == pytest.approx(120.0)

    def test_tempo_setter_does_not_crash(self, eng):
        eng.transport.tempo = 140.0

    def test_position_default(self, eng):
        assert eng.transport.position == pytest.approx(0.0)

    def test_playing_default(self, eng):
        assert eng.transport.playing is False

    def test_seek_beats(self, eng):

        eng.transport.seek(beats=4.0)  # stub — may not change position

    def test_seek_samples(self, eng):

        eng.transport.seek(samples=44100)  # stub

    def test_seek_neither_raises(self, eng):
        with pytest.raises(ValueError, match="exactly one"):
            eng.transport.seek()

    def test_seek_both_raises(self, eng):
        with pytest.raises(ValueError, match="exactly one"):
            eng.transport.seek(beats=1.0, samples=100)

    def test_set_time_signature(self, eng):
        eng.transport.set_time_signature(3, 4)

    def test_set_loop(self, eng):
        eng.transport.set_loop(0.0, 8.0)

    def test_looping_setter(self, eng):
        eng.transport.looping = True

    def test_looping_getter_raises(self, eng):
        with pytest.raises(NotImplementedError):
            _ = eng.transport.looping


# --- Event scheduling ---

class TestEventScheduling:
    """Event scheduling is currently a stub layer — calls don't crash."""

    def test_note_on(self, eng):
        synth = eng.add_test_synth()
        # Stubs return False (no scheduler yet)
        result = synth.note_on(0.0, 1, 60, 0.8)
        assert isinstance(result, bool)

    def test_note_off(self, eng):
        synth = eng.add_test_synth()
        result = synth.note_off(0.0, 1, 60)
        assert isinstance(result, bool)

    def test_cc(self, eng):
        synth = eng.add_test_synth()
        result = synth.cc(0.0, 1, 64, 127)
        assert isinstance(result, bool)

    def test_automate(self, eng):
        gain = eng.add_gain()
        result = gain.automate(0.0, "gain", 0.5)
        assert isinstance(result, bool)


# --- Node lifecycle ---

class TestNodeLifecycle:
    def test_remove(self, eng):
        gain = eng.add_gain()
        assert gain.remove() is True
        assert gain.name == ""

    def test_equality(self, eng):
        gain = eng.add_gain()
        same = eng.node(gain.id)
        assert gain == same
        assert hash(gain) == hash(same)

    def test_inequality(self, eng):
        a = eng.add_gain()
        b = eng.add_gain()
        assert a != b

    def test_repr(self, eng):
        gain = eng.add_gain()
        r = repr(gain)
        assert "Node(" in r
        assert "gain" in r


# --- param_descriptors ---

class TestParamDescriptors:
    def test_param_descriptors(self, eng):
        gain = eng.add_gain()
        descs = gain.param_descriptors
        assert len(descs) == 1
        assert isinstance(descs[0], ParamDescriptor)
        assert descs[0].name == "gain"


# --- Testing / render ---

class TestTestingMode:
    def test_prepare_and_render(self, eng):
        synth = eng.add_test_synth()
        synth >> eng.output

        eng.render(512)


# --- Editor ---

class TestEditor:
    def test_open_editor_on_gain_raises(self, eng):
        """Gain is not a plugin — open_editor should raise."""
        gain = eng.add_gain()
        with pytest.raises(SqueezeError):
            gain.open_editor()

    def test_editor_open_false_by_default(self, eng):
        """editor_open returns False when no editor is open."""
        gain = eng.add_gain()
        assert gain.editor_open is False

    def test_close_editor_on_gain_raises(self, eng):
        """No editor open — close_editor should raise."""
        gain = eng.add_gain()
        with pytest.raises(SqueezeError):
            gain.close_editor()

    def test_process_events_does_not_crash(self, eng):
        """Engine.process_events() should not crash."""
        Engine.process_events()
        Engine.process_events(timeout_ms=0)

    def test_open_editor_on_test_synth_raises(self, eng):
        """Test synth has no editor — open_editor should raise."""
        synth = eng.add_test_synth()
        with pytest.raises(SqueezeError):
            synth.open_editor()


# --- Imports ---

class TestImports:
    """Verify all expected symbols are accessible from the squeeze package."""

    def test_high_level_imports(self):
        from squeeze import Engine, Node, PortRef, Param, ParamMap
        from squeeze import Transport, Midi, MidiDevice
        from squeeze import Direction, SignalType, Port, ParamDescriptor, Connection

    def test_low_level_imports(self):
        from squeeze import Squeeze, SqueezeError, set_log_level, set_log_callback
