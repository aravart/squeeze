"""Shared fixtures for Squeeze Python tests."""

import pytest
from squeeze import Squeeze


@pytest.fixture
def engine():
    """Create a Squeeze engine, destroy it after the test."""
    eng = Squeeze()
    yield eng
    eng.close()
