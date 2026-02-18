"""Comprehensive Python API tests for the mixer-centric Squeeze engine."""

import pytest
from squeeze import (
    Squeeze, Source, Bus, Chain, Processor, Transport, Midi, MidiDevice,
    ParamDescriptor, SqueezeError, set_log_level, set_log_callback,
)


# ═══════════════════════════════════════════════════════════════════
# Lifecycle
# ═══════════════════════════════════════════════════════════════════

class TestLifecycle:
    def test_create_and_close(self):
        s = Squeeze(44100.0, 512)
        s.close()

    def test_context_manager(self):
        with Squeeze(44100.0, 512) as s:
            assert s.version

    def test_double_close_is_safe(self):
        s = Squeeze(44100.0, 512)
        s.close()
        s.close()

    def test_version(self, s):
        assert s.version == "0.3.0"

    def test_default_args(self):
        with Squeeze() as s:
            assert s.version


# ═══════════════════════════════════════════════════════════════════
# Source management
# ═══════════════════════════════════════════════════════════════════

class TestSourceManagement:
    def test_add_source(self, s):
        src = s.add_source("Synth")
        assert isinstance(src, Source)
        assert src.handle > 0

    def test_source_count(self, s):
        assert s.source_count == 0
        s.add_source("A")
        assert s.source_count == 1
        s.add_source("B")
        assert s.source_count == 2

    def test_source_name(self, s):
        src = s.add_source("Lead")
        assert src.name == "Lead"

    def test_remove_source(self, s):
        src = s.add_source("Synth")
        assert src.remove()
        assert s.source_count == 0

    def test_source_generator(self, s):
        src = s.add_source("Synth")
        gen = src.generator
        assert isinstance(gen, Processor)
        assert gen.handle > 0

    def test_source_repr(self, s):
        src = s.add_source("Lead")
        assert repr(src) == "Source('Lead')"

    def test_source_equality(self, s):
        a = s.add_source("A")
        b = Source(s, a.handle)
        assert a == b
        assert hash(a) == hash(b)


# ═══════════════════════════════════════════════════════════════════
# Bus management
# ═══════════════════════════════════════════════════════════════════

class TestBusManagement:
    def test_master_exists(self, s):
        m = s.master
        assert isinstance(m, Bus)
        assert m.handle > 0

    def test_bus_count_starts_at_1(self, s):
        assert s.bus_count == 1  # Master

    def test_add_bus(self, s):
        bus = s.add_bus("FX")
        assert isinstance(bus, Bus)
        assert bus.handle > 0
        assert s.bus_count == 2

    def test_bus_name(self, s):
        bus = s.add_bus("Reverb")
        assert bus.name == "Reverb"

    def test_remove_bus(self, s):
        bus = s.add_bus("FX")
        assert bus.remove()
        assert s.bus_count == 1

    def test_bus_repr(self, s):
        bus = s.add_bus("Drums")
        assert repr(bus) == "Bus('Drums')"

    def test_bus_equality(self, s):
        a = s.add_bus("A")
        b = Bus(s, a.handle)
        assert a == b
        assert hash(a) == hash(b)


# ═══════════════════════════════════════════════════════════════════
# Routing
# ═══════════════════════════════════════════════════════════════════

class TestRouting:
    def test_route_source_to_bus(self, s):
        src = s.add_source("Synth")
        src.route_to(s.master)
        s.render(512)  # verify no crash

    def test_source_send(self, s):
        src = s.add_source("Synth")
        fx = s.add_bus("FX")
        send_id = src.send(fx, level=-6.0)
        assert send_id > 0

    def test_source_remove_send(self, s):
        src = s.add_source("Synth")
        fx = s.add_bus("FX")
        send_id = src.send(fx, level=-6.0)
        src.remove_send(send_id)
        s.render(512)

    def test_source_set_send_level(self, s):
        src = s.add_source("Synth")
        fx = s.add_bus("FX")
        send_id = src.send(fx, level=-6.0)
        src.set_send_level(send_id, -12.0)
        s.render(512)

    def test_source_set_send_tap(self, s):
        src = s.add_source("Synth")
        fx = s.add_bus("FX")
        send_id = src.send(fx, level=-6.0)
        src.set_send_tap(send_id, "pre")
        src.set_send_tap(send_id, "post")
        s.render(512)

    def test_source_send_pre_tap(self, s):
        src = s.add_source("Synth")
        fx = s.add_bus("FX")
        send_id = src.send(fx, level=-6.0, tap="pre")
        assert send_id > 0
        s.render(512)


# ═══════════════════════════════════════════════════════════════════
# Bus routing
# ═══════════════════════════════════════════════════════════════════

class TestBusRouting:
    def test_bus_route(self, s):
        a = s.add_bus("A")
        a.route_to(s.master)
        s.render(512)

    def test_bus_send(self, s):
        a = s.add_bus("A")
        b = s.add_bus("B")
        send_id = a.send(b, level=-6.0)
        assert send_id > 0

    def test_bus_remove_send(self, s):
        a = s.add_bus("A")
        b = s.add_bus("B")
        send_id = a.send(b, level=-6.0)
        a.remove_send(send_id)
        s.render(512)

    def test_bus_set_send_level(self, s):
        a = s.add_bus("A")
        b = s.add_bus("B")
        send_id = a.send(b, level=-6.0)
        a.set_send_level(send_id, -12.0)
        s.render(512)

    def test_bus_set_send_tap(self, s):
        a = s.add_bus("A")
        b = s.add_bus("B")
        send_id = a.send(b, level=-6.0)
        a.set_send_tap(send_id, "pre")
        a.set_send_tap(send_id, "post")
        s.render(512)


# ═══════════════════════════════════════════════════════════════════
# Source chain
# ═══════════════════════════════════════════════════════════════════

class TestSourceChain:
    def test_chain_starts_empty(self, s):
        src = s.add_source("Synth")
        assert len(src.chain) == 0

    def test_chain_append(self, s):
        src = s.add_source("Synth")
        proc = src.chain.append()
        assert isinstance(proc, Processor)
        assert proc.handle > 0
        assert len(src.chain) == 1

    def test_chain_insert(self, s):
        src = s.add_source("Synth")
        src.chain.append()
        proc = src.chain.insert(0)
        assert proc.handle > 0
        assert len(src.chain) == 2

    def test_chain_remove(self, s):
        src = s.add_source("Synth")
        src.chain.append()
        src.chain.remove(0)
        assert len(src.chain) == 0

    def test_chain_repr(self, s):
        src = s.add_source("Synth")
        assert repr(src.chain) == "Chain(source, size=0)"


# ═══════════════════════════════════════════════════════════════════
# Bus chain
# ═══════════════════════════════════════════════════════════════════

class TestBusChain:
    def test_chain_starts_empty(self, s):
        assert len(s.master.chain) == 0

    def test_chain_append(self, s):
        proc = s.master.chain.append()
        assert isinstance(proc, Processor)
        assert len(s.master.chain) == 1

    def test_chain_remove(self, s):
        s.master.chain.append()
        s.master.chain.remove(0)
        assert len(s.master.chain) == 0


# ═══════════════════════════════════════════════════════════════════
# Processor params
# ═══════════════════════════════════════════════════════════════════

class TestProcessorParams:
    def test_get_set_param(self, s):
        src = s.add_source("Synth")
        gen = src.generator
        assert gen.get_param("gain") == 1.0
        gen.set_param("gain", 0.5)
        assert gen.get_param("gain") == 0.5

    def test_param_text(self, s):
        src = s.add_source("Synth")
        gen = src.generator
        text = gen.param_text("gain")
        assert isinstance(text, str)

    def test_param_descriptors(self, s):
        src = s.add_source("Synth")
        gen = src.generator
        descs = gen.param_descriptors
        assert len(descs) >= 1
        assert isinstance(descs[0], ParamDescriptor)
        assert descs[0].name == "gain"

    def test_param_descriptor_fields(self, s):
        src = s.add_source("Synth")
        gen = src.generator
        d = gen.param_descriptors[0]
        assert d.min_value <= d.default_value <= d.max_value

    def test_param_count(self, s):
        src = s.add_source("Synth")
        gen = src.generator
        assert gen.param_count >= 1

    def test_processor_latency(self, s):
        src = s.add_source("Synth")
        gen = src.generator
        assert gen.latency == 0

    def test_processor_repr(self, s):
        src = s.add_source("Synth")
        gen = src.generator
        assert repr(gen).startswith("Processor(")

    def test_processor_equality(self, s):
        src = s.add_source("Synth")
        a = src.generator
        b = Processor(s, a.handle)
        assert a == b
        assert hash(a) == hash(b)

    def test_processor_automate(self, s):
        src = s.add_source("Synth")
        gen = src.generator
        # Event scheduling stubs return False
        result = gen.automate(0.0, "gain", 0.5)
        assert isinstance(result, bool)


# ═══════════════════════════════════════════════════════════════════
# Source gain/pan/bypass
# ═══════════════════════════════════════════════════════════════════

class TestSourceProperties:
    def test_gain_default(self, s):
        src = s.add_source("Synth")
        assert src.gain == 1.0

    def test_gain_roundtrip(self, s):
        src = s.add_source("Synth")
        src.gain = 0.5
        assert src.gain == 0.5

    def test_pan_default(self, s):
        src = s.add_source("Synth")
        assert src.pan == 0.0

    def test_pan_roundtrip(self, s):
        src = s.add_source("Synth")
        src.pan = -0.5
        assert src.pan == -0.5

    def test_bypassed_default(self, s):
        src = s.add_source("Synth")
        assert not src.bypassed

    def test_bypassed_roundtrip(self, s):
        src = s.add_source("Synth")
        src.bypassed = True
        assert src.bypassed
        src.bypassed = False
        assert not src.bypassed


# ═══════════════════════════════════════════════════════════════════
# Bus gain/pan/bypass
# ═══════════════════════════════════════════════════════════════════

class TestBusProperties:
    def test_gain_default(self, s):
        assert s.master.gain == 1.0

    def test_gain_roundtrip(self, s):
        s.master.gain = 0.75
        assert s.master.gain == 0.75

    def test_pan_default(self, s):
        assert s.master.pan == 0.0

    def test_pan_roundtrip(self, s):
        s.master.pan = 1.0
        assert s.master.pan == 1.0

    def test_bypassed_default(self, s):
        assert not s.master.bypassed

    def test_bypassed_roundtrip(self, s):
        s.master.bypassed = True
        assert s.master.bypassed


# ═══════════════════════════════════════════════════════════════════
# Metering
# ═══════════════════════════════════════════════════════════════════

class TestMetering:
    def test_peak_initial(self, s):
        assert s.master.peak == 0.0

    def test_rms_initial(self, s):
        assert s.master.rms == 0.0


# ═══════════════════════════════════════════════════════════════════
# Batching
# ═══════════════════════════════════════════════════════════════════

class TestBatching:
    def test_batch_context_manager(self, s):
        with s.batch():
            s.add_source("A")
            s.add_source("B")
        assert s.source_count == 2
        s.render(512)


# ═══════════════════════════════════════════════════════════════════
# Transport
# ═══════════════════════════════════════════════════════════════════

class TestTransport:
    def test_transport_type(self, s):
        assert isinstance(s.transport, Transport)

    def test_play_stop(self, s):
        s.transport.play()
        s.transport.stop()

    def test_pause(self, s):
        s.transport.pause()

    def test_tempo_default(self, s):
        assert s.transport.tempo == 120.0

    def test_tempo_set(self, s):
        s.transport.tempo = 140.0
        # Transport stubs don't persist — returns default 120.0
        assert s.transport.tempo == 120.0

    def test_position(self, s):
        assert s.transport.position == 0.0

    def test_playing_default(self, s):
        assert not s.transport.playing

    def test_playing_setter(self, s):
        s.transport.playing = True
        # Transport stubs don't persist — is_playing returns false
        s.transport.playing = False

    def test_seek_beats(self, s):
        s.transport.seek(beats=2.0)

    def test_seek_samples(self, s):
        s.transport.seek(samples=1024)

    def test_seek_requires_one_arg(self, s):
        with pytest.raises(ValueError):
            s.transport.seek()
        with pytest.raises(ValueError):
            s.transport.seek(beats=1.0, samples=512)

    def test_set_time_signature(self, s):
        s.transport.set_time_signature(3, 4)

    def test_set_loop(self, s):
        s.transport.set_loop(0.0, 4.0)

    def test_looping_setter(self, s):
        s.transport.looping = True
        s.transport.looping = False


# ═══════════════════════════════════════════════════════════════════
# Event scheduling
# ═══════════════════════════════════════════════════════════════════

class TestEventScheduling:
    def test_note_on(self, s):
        src = s.add_source("Synth")
        result = src.note_on(0.0, 1, 60, 0.8)
        assert isinstance(result, bool)

    def test_note_off(self, s):
        src = s.add_source("Synth")
        result = src.note_off(1.0, 1, 60)
        assert isinstance(result, bool)

    def test_cc(self, s):
        src = s.add_source("Synth")
        result = src.cc(0.0, 1, 1, 64)
        assert isinstance(result, bool)


# ═══════════════════════════════════════════════════════════════════
# Plugin manager
# ═══════════════════════════════════════════════════════════════════

class TestPluginManager:
    def test_available_plugins_empty(self, s):
        assert s.available_plugins == []

    def test_num_plugins_zero(self, s):
        assert s.num_plugins == 0


# ═══════════════════════════════════════════════════════════════════
# MIDI devices
# ═══════════════════════════════════════════════════════════════════

class TestMidiDevices:
    def test_midi_type(self, s):
        assert isinstance(s.midi, Midi)

    def test_devices_list(self, s):
        devs = s.midi.devices
        assert isinstance(devs, list)

    def test_open_devices_empty(self, s):
        assert s.midi.open_devices == []


# ═══════════════════════════════════════════════════════════════════
# MIDI assignment
# ═══════════════════════════════════════════════════════════════════

class TestMidiAssignment:
    def test_midi_assign(self, s):
        src = s.add_source("Synth")
        src.midi_assign(device="Keylab", channel=1, note_range=(36, 72))
        s.render(512)

    def test_midi_assign_defaults(self, s):
        src = s.add_source("Synth")
        src.midi_assign()
        s.render(512)


# ═══════════════════════════════════════════════════════════════════
# Render
# ═══════════════════════════════════════════════════════════════════

class TestRender:
    def test_render_empty(self, s):
        s.render(512)

    def test_render_with_routing(self, s):
        src = s.add_source("Synth")
        src.route_to(s.master)
        s.render(512)

    def test_render_default_arg(self, s):
        s.render()


# ═══════════════════════════════════════════════════════════════════
# Process events
# ═══════════════════════════════════════════════════════════════════

class TestProcessEvents:
    def test_process_events(self, s):
        Squeeze.process_events(0)


# ═══════════════════════════════════════════════════════════════════
# Import assertions
# ═══════════════════════════════════════════════════════════════════

class TestImports:
    def test_public_exports(self):
        import squeeze
        assert hasattr(squeeze, 'Squeeze')
        assert hasattr(squeeze, 'Source')
        assert hasattr(squeeze, 'Bus')
        assert hasattr(squeeze, 'Chain')
        assert hasattr(squeeze, 'Processor')
        assert hasattr(squeeze, 'Transport')
        assert hasattr(squeeze, 'Midi')
        assert hasattr(squeeze, 'MidiDevice')
        assert hasattr(squeeze, 'ParamDescriptor')
        assert hasattr(squeeze, 'SqueezeError')
        assert hasattr(squeeze, 'set_log_level')
        assert hasattr(squeeze, 'set_log_callback')

    def test_no_old_exports(self):
        import squeeze
        assert not hasattr(squeeze, 'Engine')
        assert not hasattr(squeeze, 'Node')
        assert not hasattr(squeeze, 'PortRef')
