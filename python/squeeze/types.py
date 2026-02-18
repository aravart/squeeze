"""Data types for the Squeeze Python API."""

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
