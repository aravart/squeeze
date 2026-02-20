"""Data types for the Squeeze Python API."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class ParamDescriptor:
    """Metadata for a processor parameter."""
    name: str
    default_value: float
    min_value: float
    max_value: float
    num_steps: int          # 0 = continuous, >0 = stepped
    automatable: bool
    boolean: bool
    label: str              # unit: "dB", "Hz", "%", ""
    group: str              # "" = ungrouped


@dataclass(frozen=True)
class PluginInfo:
    """Metadata about an available plugin."""
    name: str
    manufacturer: str
    category: str
    version: str
    is_instrument: bool
    num_inputs: int
    num_outputs: int


@dataclass(frozen=True)
class BufferInfo:
    """Metadata about an audio buffer."""
    buffer_id: int
    num_channels: int
    length: int
    sample_rate: float
    name: str
    file_path: str
    length_seconds: float
