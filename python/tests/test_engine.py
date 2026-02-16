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
