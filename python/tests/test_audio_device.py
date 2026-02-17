"""AudioDevice tests — mirrors tests/ffi/AudioDeviceFFITests.cpp."""

import pytest
from squeeze import Squeeze, SqueezeError


def test_is_running_false_initially(engine):
    """is_running returns False before start."""
    assert engine.is_running is False


def test_sample_rate_zero_when_not_running(engine):
    """sample_rate returns 0.0 when not running."""
    assert engine.sample_rate == 0.0


def test_block_size_zero_when_not_running(engine):
    """block_size returns 0 when not running."""
    assert engine.block_size == 0


def test_stop_when_not_running(engine):
    """stop when not running is a no-op."""
    engine.stop()  # must not crash
    assert engine.is_running is False


def test_start_attempts_audio_device(engine):
    """start attempts to open audio device — may fail in CI."""
    try:
        engine.start(44100.0, 512)
        # Real device available
        assert engine.is_running is True
        assert engine.sample_rate > 0.0
        assert engine.block_size > 0
        engine.stop()
        assert engine.is_running is False
    except SqueezeError:
        # No audio device (headless/CI)
        assert engine.is_running is False
        assert engine.sample_rate == 0.0
        assert engine.block_size == 0


def test_stop_resets_state(engine):
    """stop resets sample_rate and block_size to 0."""
    try:
        engine.start(44100.0, 512)
    except SqueezeError:
        pytest.skip("No audio device available")
    engine.stop()
    assert engine.sample_rate == 0.0
    assert engine.block_size == 0


def test_double_stop_is_safe(engine):
    """Calling stop twice does not crash."""
    try:
        engine.start(44100.0, 512)
    except SqueezeError:
        pass  # OK — no device
    engine.stop()
    engine.stop()  # must not crash
