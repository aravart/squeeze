"""MidiDeviceManager tests — mirrors tests/ffi/MidiDeviceManagerFFITests.cpp."""

import pytest
from squeeze import Squeeze, SqueezeError


def test_midi_devices_returns_list(engine):
    """midi_devices returns a list (may be empty in CI)."""
    devices = engine.midi_devices
    assert isinstance(devices, list)


def test_midi_open_devices_empty_initially(engine):
    """midi_open_devices returns empty list initially."""
    assert engine.midi_open_devices == []


def test_midi_open_unknown_device_raises(engine):
    """midi_open with unknown device raises SqueezeError."""
    with pytest.raises(SqueezeError):
        engine.midi_open("NonexistentMidiDevice12345")


def test_midi_close_unknown_is_noop(engine):
    """midi_close with unknown name does not crash."""
    engine.midi_close("NonexistentMidiDevice12345")


def test_midi_route_unregistered_device_raises(engine):
    """midi_route with unregistered device raises SqueezeError."""
    with pytest.raises(SqueezeError):
        engine.midi_route("no_such_device", 1, 0, -1)


def test_midi_unroute_invalid_id_returns_false(engine):
    """midi_unroute with invalid id returns False."""
    assert engine.midi_unroute(999) is False


def test_midi_routes_empty_initially(engine):
    """midi_routes returns empty list initially."""
    assert engine.midi_routes == []


def test_midi_open_real_device_if_available(engine):
    """Open a real MIDI device if any are available."""
    devices = engine.midi_devices
    if not devices:
        pytest.skip("No MIDI devices available")

    first = devices[0]
    try:
        engine.midi_open(first)
    except SqueezeError:
        pytest.skip("MIDI device open failed")

    # Verify in open devices
    open_devs = engine.midi_open_devices
    assert len(open_devs) == 1
    assert open_devs[0] == first

    # Close
    engine.midi_close(first)
    assert engine.midi_open_devices == []
