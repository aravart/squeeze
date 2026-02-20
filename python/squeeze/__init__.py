"""Squeeze — Python client for the Squeeze audio engine."""

from __future__ import annotations

import pathlib as _pathlib

from squeeze.buffer import Buffer
from squeeze.squeeze import Squeeze
from squeeze.source import Source
from squeeze.bus import Bus
from squeeze.chain import Chain
from squeeze.clock import Clock
from squeeze.perf import Perf
from squeeze.processor import Processor
from squeeze.send import Send
from squeeze.transport import Transport
from squeeze.midi import Midi, MidiDevice, MidiRouteInfo
from squeeze.types import ParamDescriptor

from squeeze._helpers import SqueezeError, set_log_level, set_log_callback

INTEGRATION_GUIDE = str(_pathlib.Path(__file__).parent / "INTEGRATION.md")
