"""Tests for Node FFI — mirrors tests/ffi/NodeFFITests.cpp."""

import pytest


def test_add_gain(engine):
    node_id = engine.add_gain()
    assert node_id > 0


def test_node_name(engine):
    node_id = engine.add_gain()
    assert engine.node_name(node_id) == "gain"


def test_node_name_invalid_id(engine):
    assert engine.node_name(9999) == ""


def test_get_ports(engine):
    node_id = engine.add_gain()
    ports = engine.get_ports(node_id)
    assert len(ports) == 2

    assert ports[0]["name"] == "in"
    assert ports[0]["direction"] == "input"
    assert ports[0]["signal_type"] == "audio"
    assert ports[0]["channels"] == 2

    assert ports[1]["name"] == "out"
    assert ports[1]["direction"] == "output"
    assert ports[1]["signal_type"] == "audio"
    assert ports[1]["channels"] == 2


def test_get_ports_invalid_id(engine):
    ports = engine.get_ports(9999)
    assert len(ports) == 0


def test_param_descriptors(engine):
    node_id = engine.add_gain()
    descs = engine.param_descriptors(node_id)
    assert len(descs) == 1
    assert descs[0]["name"] == "gain"
    assert descs[0]["default_value"] == pytest.approx(1.0)
    assert descs[0]["automatable"] is True
    assert descs[0]["boolean"] is False


def test_get_set_param(engine):
    node_id = engine.add_gain()
    assert engine.get_param(node_id, "gain") == pytest.approx(1.0)
    assert engine.set_param(node_id, "gain", 0.5)
    assert engine.get_param(node_id, "gain") == pytest.approx(0.5)


def test_param_text(engine):
    node_id = engine.add_gain()
    text = engine.param_text(node_id, "gain")
    assert len(text) > 0


def test_param_text_unknown(engine):
    node_id = engine.add_gain()
    assert engine.param_text(node_id, "unknown") == ""


def test_remove_node(engine):
    node_id = engine.add_gain()
    assert engine.remove_node(node_id)
    assert engine.node_name(node_id) == ""


def test_remove_node_invalid_id(engine):
    assert not engine.remove_node(9999)
