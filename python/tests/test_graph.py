"""Graph connection tests — mirrors tests/ffi/GraphFFITests.cpp."""

import pytest
from squeeze import Squeeze, SqueezeError


def test_connect_succeeds(engine):
    """connect returns a connection id >= 0."""
    a = engine.add_gain()
    b = engine.add_gain()
    conn_id = engine.connect(a, "out", b, "in")
    assert conn_id >= 0


def test_connect_raises_on_invalid_source(engine):
    """connect raises SqueezeError for nonexistent source node."""
    b = engine.add_gain()
    with pytest.raises(SqueezeError):
        engine.connect(999, "out", b, "in")


def test_connect_raises_on_cycle(engine):
    """connect raises SqueezeError when connection would create a cycle."""
    a = engine.add_gain()
    b = engine.add_gain()
    engine.connect(a, "out", b, "in")
    with pytest.raises(SqueezeError):
        engine.connect(b, "out", a, "in")


def test_disconnect_succeeds(engine):
    """disconnect returns True for valid connection id."""
    a = engine.add_gain()
    b = engine.add_gain()
    conn_id = engine.connect(a, "out", b, "in")
    assert engine.disconnect(conn_id) is True


def test_disconnect_returns_false_for_unknown(engine):
    """disconnect returns False for unknown connection id."""
    assert engine.disconnect(999) is False


def test_connections_returns_correct_data(engine):
    """connections returns list with correct fields."""
    a = engine.add_gain()
    b = engine.add_gain()
    conn_id = engine.connect(a, "out", b, "in")
    conns = engine.connections()
    assert len(conns) == 1
    assert conns[0]["id"] == conn_id
    assert conns[0]["src_node"] == a
    assert conns[0]["src_port"] == "out"
    assert conns[0]["dst_node"] == b
    assert conns[0]["dst_port"] == "in"


def test_connections_empty(engine):
    """connections returns empty list when no connections exist."""
    assert engine.connections() == []


def test_connect_disconnect_roundtrip(engine):
    """Full connect, query, disconnect roundtrip."""
    a = engine.add_gain()
    b = engine.add_gain()
    c = engine.add_gain()
    c1 = engine.connect(a, "out", b, "in")
    c2 = engine.connect(b, "out", c, "in")
    assert len(engine.connections()) == 2

    engine.disconnect(c1)
    conns = engine.connections()
    assert len(conns) == 1
    assert conns[0]["id"] == c2

    # Remove node cascades connections
    engine.remove_node(b)
    assert engine.connections() == []
