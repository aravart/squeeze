"""Typed enums and dataclasses for the Squeeze high-level API."""

from dataclasses import dataclass
from enum import Enum


class Direction(Enum):
    INPUT = "input"
    OUTPUT = "output"


class SignalType(Enum):
    AUDIO = "audio"
    MIDI = "midi"


@dataclass(frozen=True)
class Port:
    """Describes a connection endpoint on a node."""
    name: str
    direction: Direction
    signal_type: SignalType
    channels: int

    @property
    def is_audio(self) -> bool:
        return self.signal_type == SignalType.AUDIO

    @property
    def is_midi(self) -> bool:
        return self.signal_type == SignalType.MIDI

    @property
    def is_input(self) -> bool:
        return self.direction == Direction.INPUT

    @property
    def is_output(self) -> bool:
        return self.direction == Direction.OUTPUT


@dataclass(frozen=True)
class ParamDescriptor:
    """Metadata for a node parameter."""
    name: str
    default_value: float
    num_steps: int          # 0 = continuous, >0 = stepped
    automatable: bool
    boolean: bool
    label: str              # unit: "dB", "Hz", "%", ""
    group: str              # "" = ungrouped


@dataclass(frozen=True)
class Connection:
    """An active connection between two ports."""
    id: int
    src_node: int
    src_port: str
    dst_node: int
    dst_port: str
