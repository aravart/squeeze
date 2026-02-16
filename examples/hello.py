"""Squeeze v2 — Python hello world."""

import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "python"))

from squeeze import Squeeze

with Squeeze() as engine:
    print(f"Squeeze {engine.version}")
    print("Hello from Python!")
