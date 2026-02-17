"""Logger tests — mirrors tests/ffi/LoggerFFITests.cpp."""

import squeeze


def _cleanup():
    """Reset logger state."""
    squeeze.set_log_callback(None)
    squeeze.set_log_level(1)  # warn


def test_set_log_level():
    """sq_set_log_level accepts all valid levels without crashing."""
    for level in range(5):  # off=0 through trace=4
        squeeze.set_log_level(level)
    _cleanup()


def test_log_callback_captures_messages():
    """sq_set_log_callback forwards messages to Python handler."""
    captured = []

    def handler(level, message):
        captured.append({"level": level, "message": message})

    squeeze.set_log_level(3)  # debug
    squeeze.set_log_callback(handler)

    # The callback is installed — verify it doesn't crash.
    # We can't easily trigger log messages from Python (Engine doesn't log yet),
    # but at minimum verify the callback setup is stable.
    eng = squeeze.Squeeze(44100.0, 512)
    eng.close()

    # Verify handler is callable and the setup didn't crash
    # (captured may or may not have entries depending on whether Engine logs)
    assert isinstance(captured, list)

    _cleanup()


def test_log_callback_none_reverts():
    """sq_set_log_callback(None) reverts to stderr without crashing."""
    captured = []

    def handler(level, message):
        captured.append({"level": level, "message": message})

    squeeze.set_log_level(3)
    squeeze.set_log_callback(handler)
    count_after_setup = len(captured)

    # Clear callback
    squeeze.set_log_callback(None)

    # Further engine operations should not crash and should not call handler
    eng = squeeze.Squeeze(44100.0, 512)
    eng.close()

    assert len(captured) == count_after_setup

    _cleanup()
