"""Engine tests — mirrors tests/ffi/EngineFFITests.cpp."""

from squeeze import Squeeze


def test_engine_create(engine):
    """sq_engine_create returns a non-NULL handle."""
    assert engine._handle is not None


def test_engine_close_is_idempotent():
    """sq_engine_destroy with already-closed engine is safe."""
    eng = Squeeze()
    eng.close()
    eng.close()  # must not crash


def test_version_is_nonempty(engine):
    """sq_version returns a non-empty version string."""
    assert len(engine.version) > 0


def test_version_value(engine):
    """sq_version returns expected version."""
    assert engine.version == "0.2.0"


def test_context_manager():
    """Engine works as a context manager."""
    with Squeeze() as eng:
        assert eng._handle is not None
    assert eng._handle is None


def test_multiple_engines():
    """Multiple engines can be created and destroyed independently."""
    a = Squeeze()
    b = Squeeze()
    assert a._handle != b._handle
    assert a.version == b.version
    a.close()
    b.close()


# ═══════════════════════════════════════════════════════════════════
# Output node
# ═══════════════════════════════════════════════════════════════════

def test_output_node_exists(engine):
    """Output node has a valid ID."""
    assert engine.output > 0


def test_output_node_cannot_be_removed(engine):
    """Output node cannot be removed."""
    assert engine.remove_node(engine.output) is False


def test_node_count_includes_output(engine):
    """node_count includes the output node."""
    assert engine.node_count() == 1
    g = engine.add_gain()
    assert engine.node_count() == 2
    engine.remove_node(g)
    assert engine.node_count() == 1


def test_output_node_has_in_port(engine):
    """Output node has an audio input port named 'in'."""
    ports = engine.get_ports(engine.output)
    in_ports = [p for p in ports if p["name"] == "in" and p["direction"] == "input"]
    assert len(in_ports) == 1
    assert in_ports[0]["signal_type"] == "audio"


# ═══════════════════════════════════════════════════════════════════
# Testing and processBlock
# ═══════════════════════════════════════════════════════════════════

def test_prepare_for_testing_and_render(engine):
    """prepare_for_testing and render do not crash."""
    engine.prepare_for_testing(44100.0, 512)
    engine.render(512)


def test_connect_gain_to_output_and_render(engine):
    """Connect gain to output and render."""
    engine.prepare_for_testing(44100.0, 512)
    g = engine.add_gain()
    engine.connect(g, "out", engine.output, "in")
    engine.render(512)


# ═══════════════════════════════════════════════════════════════════
# Transport stubs
# ═══════════════════════════════════════════════════════════════════

def test_transport_stubs_do_not_crash(engine):
    """Transport stubs don't crash and return defaults."""
    engine.prepare_for_testing(44100.0, 512)
    engine.transport_play()
    engine.transport_stop()
    engine.transport_pause()
    engine.transport_set_tempo(140.0)
    engine.transport_set_time_signature(3, 4)
    engine.transport_seek_samples(0)
    engine.transport_seek_beats(0.0)
    engine.transport_set_loop_points(0.0, 4.0)
    engine.transport_set_looping(True)

    assert engine.transport_position == 0.0
    assert engine.transport_tempo == 120.0
    assert engine.transport_is_playing is False

    engine.render(512)


# ═══════════════════════════════════════════════════════════════════
# Event scheduling stubs
# ═══════════════════════════════════════════════════════════════════

def test_event_scheduling_stubs_return_false(engine):
    """Event scheduling stubs return False."""
    assert engine.schedule_note_on(1, 0.0, 1, 60, 0.8) is False
    assert engine.schedule_note_off(1, 1.0, 1, 60) is False
    assert engine.schedule_cc(1, 0.0, 1, 1, 64) is False
    assert engine.schedule_param_change(1, 0.0, "gain", 0.5) is False


# ═══════════════════════════════════════════════════════════════════
# PluginNode / Test Synth
# ═══════════════════════════════════════════════════════════════════

def test_add_test_synth_returns_valid_id(engine):
    """add_test_synth returns a valid node id."""
    synth = engine.add_test_synth()
    assert synth > 0
    assert engine.node_count() == 2  # output + synth


def test_test_synth_ports(engine):
    """Test synth has audio out, MIDI in, and MIDI out ports."""
    synth = engine.add_test_synth()
    ports = engine.get_ports(synth)

    names = {p["name"] for p in ports}
    assert "out" in names
    assert "midi_in" in names
    assert "midi_out" in names

    audio_out = [p for p in ports if p["name"] == "out"]
    assert len(audio_out) == 1
    assert audio_out[0]["signal_type"] == "audio"
    assert audio_out[0]["channels"] == 2


def test_test_synth_parameters(engine):
    """Test synth has Gain and Mix parameters."""
    synth = engine.add_test_synth()
    descs = engine.param_descriptors(synth)
    param_names = {d["name"] for d in descs}
    assert "Gain" in param_names
    assert "Mix" in param_names

    assert engine.get_param(synth, "Gain") == 1.0
    engine.set_param(synth, "Gain", 0.25)
    assert engine.get_param(synth, "Gain") != 1.0


def test_test_synth_connect_and_render(engine):
    """Connect test synth to output and render."""
    engine.prepare_for_testing(44100.0, 512)
    synth = engine.add_test_synth()
    engine.connect(synth, "out", engine.output, "in")
    engine.render(512)
