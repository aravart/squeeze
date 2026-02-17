"""Squeeze — Python client for the Squeeze audio engine."""

# High-level API (primary)
from squeeze.engine import Engine
from squeeze.node import Node, PortRef, Param, ParamMap
from squeeze.transport import Transport
from squeeze.midi import Midi, MidiDevice
from squeeze.types import Direction, SignalType, Port, ParamDescriptor, Connection

# Low-level API (still available)
from squeeze._low_level import Squeeze, SqueezeError, set_log_level, set_log_callback
