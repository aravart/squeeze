"""Shared fixtures for Squeeze Python tests."""

import pytest
from squeeze import Squeeze


@pytest.fixture
def engine():
    """Create a Squeeze engine, destroy it after the test."""
    eng = Squeeze(44100.0, 512)
    yield eng
    eng.close()
