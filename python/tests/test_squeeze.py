"""Comprehensive Python API tests for the mixer-centric Squeeze engine."""

import pytest
import threading
import time

from squeeze import (
    Buffer, BufferInfo, PluginInfo, Squeeze, Source, Bus, Chain, Clock, Processor,
    Transport, Midi, MidiDevice, ParamDescriptor, SqueezeError, set_log_level,
    set_log_callback,
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
        snd = src.send(fx, level=-6.0)
        assert snd.send_id > 0
        assert snd.level == -6.0
        assert snd.tap == "post"

    def test_source_send_remove(self, s):
        src = s.add_source("Synth")
        fx = s.add_bus("FX")
        snd = src.send(fx, level=-6.0)
        snd.remove()
        s.render(512)

    def test_source_send_level(self, s):
        src = s.add_source("Synth")
        fx = s.add_bus("FX")
        snd = src.send(fx, level=-6.0)
        snd.level = -12.0
        assert snd.level == -12.0
        s.render(512)

    def test_source_send_tap(self, s):
        src = s.add_source("Synth")
        fx = s.add_bus("FX")
        snd = src.send(fx, level=-6.0)
        snd.tap = "pre"
        assert snd.tap == "pre"
        snd.tap = "post"
        assert snd.tap == "post"
        s.render(512)

    def test_source_send_pre_tap(self, s):
        src = s.add_source("Synth")
        fx = s.add_bus("FX")
        snd = src.send(fx, level=-6.0, tap="pre")
        assert snd.send_id > 0
        assert snd.tap == "pre"
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
        snd = a.send(b, level=-6.0)
        assert snd.send_id > 0
        assert snd.level == -6.0

    def test_bus_send_remove(self, s):
        a = s.add_bus("A")
        b = s.add_bus("B")
        snd = a.send(b, level=-6.0)
        snd.remove()
        s.render(512)

    def test_bus_send_level(self, s):
        a = s.add_bus("A")
        b = s.add_bus("B")
        snd = a.send(b, level=-6.0)
        snd.level = -12.0
        assert snd.level == -12.0
        s.render(512)

    def test_bus_send_tap(self, s):
        a = s.add_bus("A")
        b = s.add_bus("B")
        snd = a.send(b, level=-6.0)
        snd.tap = "pre"
        assert snd.tap == "pre"
        snd.tap = "post"
        assert snd.tap == "post"
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
        result = gen.automate(0.0, "gain", 0.5)
        assert result is True


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
        assert s.transport.tempo == 140.0

    def test_position(self, s):
        assert s.transport.position == 0.0

    def test_playing_default(self, s):
        assert not s.transport.playing

    def test_playing_setter(self, s):
        s.transport.playing = True
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
        assert result is True

    def test_note_off(self, s):
        src = s.add_source("Synth")
        result = src.note_off(1.0, 1, 60)
        assert result is True

    def test_cc(self, s):
        src = s.add_source("Synth")
        result = src.cc(0.0, 1, 1, 64)
        assert result is True

    def test_pitch_bend(self, s):
        src = s.add_source("Synth")
        result = src.pitch_bend(0.0, 1, 8192)
        assert result is True

    def test_param_change_dispatches(self, s):
        src = s.add_source("Synth")
        gen = src.generator
        assert gen.get_param("gain") == 1.0
        gen.automate(0.0, "gain", 0.25)
        s.transport.play()
        s.render(512)
        assert abs(gen.get_param("gain") - 0.25) < 1e-6


# ═══════════════════════════════════════════════════════════════════
# Clock dispatch
# ═══════════════════════════════════════════════════════════════════

class TestClock:
    def test_create_and_destroy(self, s):
        beats = []
        clk = s.clock(1.0, 0.0, lambda beat: beats.append(beat))
        assert isinstance(clk, Clock)
        clk.destroy()

    def test_resolution_property(self, s):
        clk = s.clock(0.25, 50.0, lambda beat: None)
        assert abs(clk.resolution - 0.25) < 1e-9
        clk.destroy()

    def test_latency_ms_property(self, s):
        clk = s.clock(0.25, 50.0, lambda beat: None)
        assert abs(clk.latency_ms - 50.0) < 1e-9
        clk.destroy()

    def test_invalid_resolution_raises(self, s):
        with pytest.raises(ValueError):
            s.clock(0.0, 0.0, lambda beat: None)

    def test_invalid_latency_raises(self, s):
        with pytest.raises(ValueError):
            s.clock(1.0, -1.0, lambda beat: None)

    def test_callback_fires_during_render(self, s):
        beats = []
        lock = threading.Lock()
        event = threading.Event()

        def on_beat(beat):
            with lock:
                beats.append(beat)
                if len(beats) >= 1:
                    event.set()

        clk = s.clock(1.0, 0.0, on_beat)
        s.transport.play()
        s.render(512)  # process play command

        # Render enough blocks to cross beat 1.0
        for _ in range(50):
            s.render(512)

        event.wait(timeout=1.0)

        with lock:
            assert len(beats) >= 1
            assert abs(beats[0] - 1.0) < 1e-9

        clk.destroy()

    def test_double_destroy_is_safe(self, s):
        clk = s.clock(1.0, 0.0, lambda beat: None)
        clk.destroy()
        clk.destroy()

    def test_repr(self, s):
        clk = s.clock(0.25, 50.0, lambda beat: None)
        r = repr(clk)
        assert "0.25" in r
        assert "50.0" in r
        clk.destroy()


# ═══════════════════════════════════════════════════════════════════
# Plugin manager
# ═══════════════════════════════════════════════════════════════════

class TestPluginManager:
    def test_available_plugins_empty(self, s):
        assert s.available_plugins == []

    def test_num_plugins_zero(self, s):
        assert s.num_plugins == 0

    def test_plugin_infos_empty(self):
        with Squeeze(plugins=False) as s:
            infos = s.plugin_infos
            assert isinstance(infos, list)
            assert len(infos) == 0

    def test_plugin_info_importable(self):
        assert PluginInfo is not None


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
# Performance monitoring
# ═══════════════════════════════════════════════════════════════════

class TestPerfMonitor:
    def test_disabled_by_default(self, s):
        assert not s.perf.enabled

    def test_enable_disable(self, s):
        s.perf.enabled = True
        assert s.perf.enabled
        s.perf.enabled = False
        assert not s.perf.enabled

    def test_slot_profiling_disabled_by_default(self, s):
        assert not s.perf.slot_profiling

    def test_enable_disable_slot_profiling(self, s):
        s.perf.slot_profiling = True
        assert s.perf.slot_profiling
        s.perf.slot_profiling = False
        assert not s.perf.slot_profiling

    def test_xrun_threshold_default(self, s):
        assert abs(s.perf.xrun_threshold - 1.0) < 1e-6

    def test_xrun_threshold_roundtrip(self, s):
        s.perf.xrun_threshold = 0.5
        assert abs(s.perf.xrun_threshold - 0.5) < 1e-6

    def test_xrun_threshold_clamped(self, s):
        s.perf.xrun_threshold = 0.01
        assert s.perf.xrun_threshold >= 0.1
        s.perf.xrun_threshold = 10.0
        assert s.perf.xrun_threshold <= 2.0

    def test_snapshot_when_disabled(self, s):
        snap = s.perf.snapshot()
        assert isinstance(snap, dict)
        assert snap["callback_avg_us"] == 0.0
        assert snap["xrun_count"] == 0
        assert snap["callback_count"] == 0

    def test_snapshot_fields(self, s):
        snap = s.perf.snapshot()
        expected_keys = {
            "callback_avg_us", "callback_peak_us", "cpu_load_percent",
            "xrun_count", "callback_count", "sample_rate", "block_size",
            "buffer_duration_us",
        }
        assert set(snap.keys()) == expected_keys

    def test_snapshot_after_render(self, s):
        s.perf.enabled = True
        src = s.add_source("Synth")
        src.route_to(s.master)
        for _ in range(20):
            s.render(512)
        snap = s.perf.snapshot()
        assert snap["callback_count"] >= 1
        assert snap["sample_rate"] == 44100.0
        assert snap["block_size"] == 512

    def test_reset(self, s):
        s.perf.enabled = True
        for _ in range(20):
            s.render(512)
        snap = s.perf.snapshot()
        assert snap["callback_count"] >= 1
        s.perf.reset()
        snap2 = s.perf.snapshot()
        assert snap2["xrun_count"] == 0
        assert snap2["callback_count"] == 0

    def test_slots_empty_when_disabled(self, s):
        slots = s.perf.slots()
        assert isinstance(slots, list)
        assert len(slots) == 0

    def test_slots_with_profiling(self, s):
        s.perf.enabled = True
        s.perf.slot_profiling = True
        src = s.add_source("Synth")
        src.route_to(s.master)
        for _ in range(20):
            s.render(512)
        slots = s.perf.slots()
        assert len(slots) >= 1
        for slot in slots:
            assert "handle" in slot
            assert "avg_us" in slot
            assert "peak_us" in slot

    def test_callback_count_increments(self, s):
        s.perf.enabled = True
        s.render(512)
        s.render(512)
        s.render(512)
        snap = s.perf.snapshot()
        assert snap["callback_count"] >= 3


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
        assert hasattr(squeeze, 'Clock')
        assert hasattr(squeeze, 'Perf')
        assert hasattr(squeeze, 'Processor')
        assert hasattr(squeeze, 'Send')
        assert hasattr(squeeze, 'Transport')
        assert hasattr(squeeze, 'Midi')
        assert hasattr(squeeze, 'MidiDevice')
        assert hasattr(squeeze, 'ParamDescriptor')
        assert hasattr(squeeze, 'SqueezeError')
        assert hasattr(squeeze, 'set_log_level')
        assert hasattr(squeeze, 'set_log_callback')

    def test_buffer_exported(self):
        import squeeze
        assert hasattr(squeeze, 'Buffer')

    def test_plugin_info_exported(self):
        import squeeze
        assert hasattr(squeeze, 'PluginInfo')

    def test_no_old_exports(self):
        import squeeze
        assert not hasattr(squeeze, 'Engine')
        assert not hasattr(squeeze, 'Node')
        assert not hasattr(squeeze, 'PortRef')


# ═══════════════════════════════════════════════════════════════════
# Buffer
# ═══════════════════════════════════════════════════════════════════

class TestBuffer:
    def test_create_buffer(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=2, length=44100, sample_rate=44100.0, name="test")
            assert isinstance(buf, Buffer)
            assert buf.buffer_id >= 1

    def test_buffer_metadata(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=2, length=44100, sample_rate=44100.0, name="kick")
            assert buf.num_channels == 2
            assert buf.length == 44100
            assert buf.sample_rate == 44100.0
            assert buf.name == "kick"
            assert abs(buf.length_seconds - 1.0) < 1e-6

    def test_buffer_count(self):
        with Squeeze(plugins=False) as s:
            assert s.buffer_count == 0
            s.create_buffer(channels=1, length=100, sample_rate=44100.0)
            assert s.buffer_count == 1
            s.create_buffer(channels=1, length=100, sample_rate=44100.0)
            assert s.buffer_count == 2

    def test_buffer_write_and_read(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=1, length=100, sample_rate=44100.0)
            data = [float(i) / 100.0 for i in range(100)]
            written = buf.write(channel=0, data=data)
            assert written == 100

            samples = buf.read(channel=0, num_samples=10)
            assert len(samples) == 10
            assert abs(samples[0] - 0.0) < 1e-6
            assert abs(samples[5] - 0.05) < 1e-6

    def test_buffer_read_default_all(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=1, length=50, sample_rate=44100.0)
            data = [0.5] * 50
            buf.write(channel=0, data=data)
            samples = buf.read(channel=0)
            assert len(samples) == 50

    def test_buffer_read_with_offset(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=1, length=100, sample_rate=44100.0)
            data = [float(i) for i in range(100)]
            buf.write(channel=0, data=data)
            samples = buf.read(channel=0, offset=90, num_samples=10)
            assert len(samples) == 10
            assert abs(samples[0] - 90.0) < 1e-4

    def test_buffer_write_position(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=1, length=100, sample_rate=44100.0)
            assert buf.write_position == 0
            buf.write_position = 50
            assert buf.write_position == 50

    def test_buffer_clear(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=1, length=100, sample_rate=44100.0)
            buf.write(channel=0, data=[1.0] * 100)
            buf.write_position = 100
            buf.clear()
            assert buf.write_position == 0
            samples = buf.read(channel=0, num_samples=1)
            assert abs(samples[0]) < 1e-6

    def test_buffer_remove(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=1, length=100, sample_rate=44100.0)
            assert s.buffer_count == 1
            assert buf.remove()
            assert s.buffer_count == 0

    def test_buffer_repr(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=1, length=100, sample_rate=44100.0, name="sine")
            assert "sine" in repr(buf)

    def test_buffer_equality(self):
        with Squeeze(plugins=False) as s:
            buf1 = s.create_buffer(channels=1, length=100, sample_rate=44100.0)
            buf2 = s.create_buffer(channels=1, length=100, sample_rate=44100.0)
            assert buf1 != buf2
            assert buf1 == Buffer(s, buf1.buffer_id)

    def test_buffer_tempo_default(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=1, length=100, sample_rate=44100.0)
            assert buf.tempo == 0.0

    def test_buffer_tempo_set_and_get(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=1, length=100, sample_rate=44100.0)
            buf.tempo = 120.0
            assert buf.tempo == 120.0
            buf.tempo = 98.5
            assert buf.tempo == 98.5

    def test_buffer_tempo_reset_to_zero(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=1, length=100, sample_rate=44100.0)
            buf.tempo = 140.0
            buf.tempo = 0.0
            assert buf.tempo == 0.0

    def test_buffer_info_includes_tempo(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=1, length=100, sample_rate=44100.0)
            buf.tempo = 128.0
            info = s.buffer_info(buf.buffer_id)
            assert info.tempo == 128.0

    def test_buffer_info_tempo_default(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=1, length=100, sample_rate=44100.0)
            info = s.buffer_info(buf.buffer_id)
            assert info.tempo == 0.0


# ═══════════════════════════════════════════════════════════════════
# BufferLibrary (load_buffer, buffer_info, buffers)
# ═══════════════════════════════════════════════════════════════════

class TestBufferLibrary:
    def test_load_buffer_nonexistent_raises(self):
        with Squeeze(plugins=False) as s:
            with pytest.raises(SqueezeError):
                s.load_buffer("/nonexistent/file.wav")

    def test_buffer_info_metadata(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=2, length=44100, sample_rate=44100.0, name="kick")
            info = s.buffer_info(buf.buffer_id)
            assert isinstance(info, BufferInfo)
            assert info.buffer_id == buf.buffer_id
            assert info.num_channels == 2
            assert info.length == 44100
            assert info.sample_rate == 44100.0
            assert info.name == "kick"
            assert abs(info.length_seconds - 1.0) < 1e-6

    def test_buffer_info_unknown_id(self):
        with Squeeze(plugins=False) as s:
            info = s.buffer_info(999)
            assert info.buffer_id == 0
            assert info.num_channels == 0

    def test_buffers_sorted(self):
        with Squeeze(plugins=False) as s:
            b1 = s.create_buffer(channels=1, length=100, sample_rate=44100.0, name="c")
            b2 = s.create_buffer(channels=1, length=100, sample_rate=44100.0, name="a")
            b3 = s.create_buffer(channels=1, length=100, sample_rate=44100.0, name="b")
            bufs = s.buffers
            assert len(bufs) == 3
            assert bufs[0] == (b1.buffer_id, "c")
            assert bufs[1] == (b2.buffer_id, "a")
            assert bufs[2] == (b3.buffer_id, "b")
            # Verify sorted by ID
            assert bufs[0][0] < bufs[1][0] < bufs[2][0]

    def test_buffers_empty(self):
        with Squeeze(plugins=False) as s:
            assert s.buffers == []

    def test_buffer_info_exported(self):
        """BufferInfo is importable from squeeze package."""
        assert BufferInfo is not None


# ═══════════════════════════════════════════════════════════════════
# PlayerProcessor
# ═══════════════════════════════════════════════════════════════════

class TestPlayerProcessor:
    def test_add_source_player(self):
        with Squeeze(plugins=False) as s:
            src = s.add_source("player1", player=True)
            assert isinstance(src, Source)
            assert src.name == "player1"

    def test_player_has_7_params(self):
        with Squeeze(plugins=False) as s:
            src = s.add_source("p", player=True)
            descs = src.generator.param_descriptors
            assert len(descs) == 7

    def test_player_set_buffer(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=1, length=1000, sample_rate=44100.0)
            src = s.add_source("p", player=True)
            assert src.set_buffer(buf.buffer_id)

    def test_player_set_buffer_invalid_id(self):
        with Squeeze(plugins=False) as s:
            src = s.add_source("p", player=True)
            assert not src.set_buffer(999)

    def test_player_set_buffer_non_player(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=1, length=100, sample_rate=44100.0)
            src = s.add_source("gain")
            assert not src.set_buffer(buf.buffer_id)

    def test_player_playback(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=2, length=44100, sample_rate=44100.0)
            buf.write(channel=0, data=[0.5] * 44100)
            buf.write(channel=1, data=[0.5] * 44100)

            src = s.add_source("p", player=True)
            src.set_buffer(buf.buffer_id)
            src.route_to(s.master)
            src["fade_ms"] = 0.0
            src["playing"] = 1.0
            s.render(512)

    def test_player_param_shortcuts(self):
        with Squeeze(plugins=False) as s:
            src = s.add_source("p", player=True)
            src["speed"] = 2.0
            assert src["speed"] == 2.0
            src["loop_mode"] = 1.0
            assert src["loop_mode"] == 1.0

    def test_player_auto_stop(self):
        with Squeeze(plugins=False) as s:
            buf = s.create_buffer(channels=1, length=32, sample_rate=44100.0)
            buf.write(channel=0, data=[0.3] * 32)
            src = s.add_source("p", player=True)
            src.set_buffer(buf.buffer_id)
            src.route_to(s.master)
            src["fade_ms"] = 0.0
            src["loop_mode"] = 0.0
            src["playing"] = 1.0
            s.render(512)
            assert src["playing"] < 0.5
