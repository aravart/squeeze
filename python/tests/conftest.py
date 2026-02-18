"""Shared fixtures for Squeeze Python tests."""

import pytest
from squeeze import Squeeze


@pytest.fixture
def s():
    """Create a Squeeze engine, destroy it after the test."""
    engine = Squeeze(44100.0, 512)
    yield engine
    engine.close()
