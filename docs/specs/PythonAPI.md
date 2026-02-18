# Python API Specification

## Motivation

The Python package (`squeeze`) provides a single Pythonic API to the Squeeze audio engine. There is no separate "low-level" and "high-level" layer — the public API is one set of objects (`Squeeze`, `Source`, `Bus`, `Chain`, `Processor`, `Transport`, `Midi`) that call through internal ctypes bindings. This keeps the development cycle simple: one set of Python wrappers, one set of Python tests.

## Responsibilities

- Provide the only public Python interface to the mixer-centric audio engine
- Wrap Source, Bus, Chain, and Processor handles in Python objects
- Group transport, MIDI, and scheduling operations into sub-objects
- Handle error checking, string conversion, and memory management internally
- Delegate all FFI calls to internal ctypes bindings

## Interface

### Module Layout

```
python/squeeze/
├── __init__.py          # re-exports: Squeeze, Source, Bus, Chain, Processor, etc.
├── _ffi.py              # ctypes declarations (internal, mechanical)
├── _helpers.py          # error checking, string/list conversion, memory freeing (internal)
├── squeeze.py           # Squeeze — the public entry point (was Engine)
├── source.py            # Source object
├── bus.py               # Bus object
├── chain.py             # Chain object
├── processor.py         # Processor object
├── transport.py         # Transport sub-object
├── midi.py              # Midi, MidiDevice sub-objects
└── types.py             # ParamDescriptor dataclass
```

### Internal layers (not public API)

- **`_ffi.py`**: Mechanical ctypes declarations — function signatures, argument types, return types. Never imported by users.
- **`_helpers.py`**: Centralizes error-check-and-raise-`SqueezeError` logic, C string encoding/decoding, C list → Python list conversion, and C memory freeing. Called by all public classes. Not a public API.

---

### ParamDescriptor (`types.py`)

```python
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
```

---

### Processor (`processor.py`)

A `Processor` wraps a processor handle and provides parameter access.

```python
class Processor:
    """Wraps a processor in a Source chain or Bus chain."""

    def __init__(self, engine: "Squeeze", handle: int):
        self._engine = engine
        self._handle = handle

    @property
    def handle(self) -> int:
        """The opaque processor handle."""
        return self._handle

    # --- Parameters ---

    def get_param(self, name: str) -> float:
        """Get a parameter value by name."""

    def set_param(self, name: str, value: float) -> None:
        """Set a parameter value by name."""

    def param_text(self, name: str) -> str:
        """Human-readable display text for a parameter."""

    @property
    def param_descriptors(self) -> list[ParamDescriptor]:
        """Parameter metadata for all parameters."""

    @property
    def param_count(self) -> int:
        """Number of parameters."""

    # --- Latency ---

    @property
    def latency(self) -> int:
        """Processing latency in samples."""

    # --- Plugin editor ---

    def open_editor(self) -> None:
        """Open the native plugin editor window."""

    def close_editor(self) -> None:
        """Close the plugin editor window."""

    # --- Automation ---

    def automate(self, beat: float, param_name: str, value: float) -> bool:
        """Schedule a parameter change at the given beat time."""

    def __repr__(self) -> str:
        return f"Processor({self._handle})"

    def __eq__(self, other) -> bool:
        if isinstance(other, Processor):
            return self._handle == other._handle
        return NotImplemented

    def __hash__(self) -> int:
        return hash(self._handle)
```

---

### Chain (`chain.py`)

A `Chain` wraps chain operations on a Source or Bus.

```python
class Chain:
    """Ordered list of processors — the insert rack."""

    def __init__(self, engine: "Squeeze", owner_handle, owner_type: str):
        """owner_type is 'source' or 'bus'."""

    def append(self, plugin_path: str) -> Processor:
        """Append a plugin to the end of the chain. Returns a Processor."""

    def insert(self, index: int, plugin_path: str) -> Processor:
        """Insert a plugin at the given index. Returns a Processor."""

    def remove(self, index: int) -> None:
        """Remove the processor at the given index."""

    def __len__(self) -> int:
        """Number of processors in the chain."""

    def __getitem__(self, index: int) -> Processor:
        """Access a processor by index.
        Raises IndexError if out of range."""

    def __repr__(self) -> str:
        return f"Chain({self._type}, size={len(self)})"
```

---

### Source (`source.py`)

```python
class Source:
    """A sound generator with insert chain, routing, and MIDI assignment."""

    def __init__(self, engine: "Squeeze", handle):
        self._engine = engine
        self._handle = handle

    @property
    def handle(self):
        return self._handle

    @property
    def name(self) -> str:
        """Source name."""

    # --- Insert chain ---

    @property
    def chain(self) -> Chain:
        """The insert effects chain."""

    # --- Generator ---

    @property
    def generator(self) -> Processor:
        """The generator processor (synth, sampler, etc.)."""

    # --- Routing ---

    def route_to(self, bus: "Bus") -> None:
        """Route this source's output to a bus."""

    def send(self, bus: "Bus", *, level: float = 0.0) -> int:
        """Add a send to a bus. Returns send ID.
        Level is in dB (0.0 = unity).
        """

    def remove_send(self, send_id: int) -> None:
        """Remove a send by ID."""

    def set_send_level(self, send_id: int, level: float) -> None:
        """Change a send's level in dB."""

    # --- MIDI ---

    def midi_assign(self, *, device: str = "", channel: int = 0,
                    note_range: tuple[int, int] = (0, 127)) -> None:
        """Assign MIDI input to this source.

        device: "" = any device, or specific device name
        channel: 0 = all channels, 1-16 = specific channel
        note_range: (low, high) inclusive note range
        """

    # --- Mute ---

    @property
    def muted(self) -> bool: ...

    @muted.setter
    def muted(self, value: bool) -> None: ...

    # --- Event scheduling ---

    def note_on(self, beat: float, channel: int, note: int,
                velocity: float) -> bool:
        """Schedule a note-on event at the given beat time."""

    def note_off(self, beat: float, channel: int, note: int) -> bool:
        """Schedule a note-off event at the given beat time."""

    def cc(self, beat: float, channel: int, cc_num: int,
           cc_val: int) -> bool:
        """Schedule a CC event at the given beat time."""

    # --- Lifecycle ---

    def remove(self) -> bool:
        """Remove this source from the engine."""

    def __repr__(self) -> str:
        return f"Source({self.name!r})"

    def __eq__(self, other) -> bool:
        if isinstance(other, Processor):
            return self._handle == other._handle
        return NotImplemented

    def __hash__(self) -> int:
        return hash(self._handle)
```

---

### Bus (`bus.py`)

```python
class Bus:
    """A summing point with insert chain and routing."""

    def __init__(self, engine: "Squeeze", handle):
        self._engine = engine
        self._handle = handle

    @property
    def handle(self):
        return self._handle

    @property
    def name(self) -> str:
        """Bus name."""

    # --- Insert chain ---

    @property
    def chain(self) -> Chain:
        """The insert effects chain."""

    # --- Routing ---

    def route_to(self, bus: "Bus") -> None:
        """Route this bus's output to another bus."""

    def send(self, bus: "Bus", *, level: float = 0.0) -> int:
        """Add a send to a bus. Returns send ID."""

    def remove_send(self, send_id: int) -> None:
        """Remove a send by ID."""

    def set_send_level(self, send_id: int, level: float) -> None:
        """Change a send's level in dB."""

    # --- Metering ---

    @property
    def peak(self) -> float:
        """Current peak level (0.0-1.0+)."""

    @property
    def rms(self) -> float:
        """Current RMS level."""

    # --- Mute ---

    @property
    def muted(self) -> bool: ...

    @muted.setter
    def muted(self, value: bool) -> None: ...

    # --- Lifecycle ---

    def remove(self) -> bool:
        """Remove this bus from the engine. Cannot remove Master."""

    def __repr__(self) -> str:
        return f"Bus({self.name!r})"

    def __eq__(self, other) -> bool:
        if isinstance(other, Bus):
            return self._handle == other._handle
        return NotImplemented

    def __hash__(self) -> int:
        return hash(self._handle)
```

---

### Transport (`transport.py`)

```python
class Transport:
    """Sub-object for transport control. Accessed via squeeze.transport."""

    def play(self) -> None: ...
    def stop(self) -> None: ...
    def pause(self) -> None: ...

    @property
    def tempo(self) -> float: ...

    @tempo.setter
    def tempo(self, bpm: float) -> None: ...

    @property
    def position(self) -> float: ...

    @property
    def playing(self) -> bool: ...

    @playing.setter
    def playing(self, value: bool) -> None: ...

    def seek(self, *, beats: float = None, samples: int = None) -> None:
        """Seek to a position. Specify exactly one of beats or samples."""

    def set_time_signature(self, numerator: int, denominator: int) -> None: ...

    def set_loop(self, start: float, end: float) -> None: ...

    @looping.setter
    def looping(self, enabled: bool) -> None: ...
```

---

### Midi and MidiDevice (`midi.py`)

```python
class Midi:
    """Sub-object for MIDI device management. Accessed via squeeze.midi."""

    @property
    def devices(self) -> list["MidiDevice"]:
        """Available MIDI input devices."""

    @property
    def open_devices(self) -> list["MidiDevice"]:
        """Currently open MIDI devices."""


class MidiDevice:
    """Represents a MIDI input device."""

    @property
    def name(self) -> str: ...

    def open(self) -> None: ...
    def close(self) -> None: ...

    def __repr__(self) -> str:
        return f"MidiDevice({self._name!r})"
```

---

### Squeeze (`squeeze.py`)

The top-level entry point. Named `Squeeze` so the import reads naturally: `from squeeze import Squeeze`.

```python
class Squeeze:
    """Squeeze audio engine — mixer-centric Pythonic interface.

    Usage:
        from squeeze import Squeeze

        with Squeeze() as s:
            synth = s.add_source("Lead", plugin="Diva.vst3")
            synth.route_to(s.master)
            s.start()
    """

    def __init__(self, sample_rate: float = 44100.0, block_size: int = 512):
        """Create a new Squeeze engine.

        Args:
            sample_rate: Audio sample rate in Hz. Default: 44100.0
            block_size: Audio block size in samples. Default: 512
        """
        # Internally calls sq_create(sample_rate, block_size)

    def close(self) -> None:
        """Destroy the engine. Safe to call multiple times."""

    def __enter__(self) -> "Squeeze":
        return self

    def __exit__(self, *args) -> None:
        self.close()

    def __del__(self):
        self.close()

    @property
    def version(self) -> str:
        """Engine version string."""

    # --- Sub-objects ---

    @property
    def transport(self) -> Transport:
        """Transport control (play, stop, tempo, seek, loop)."""

    @property
    def midi(self) -> Midi:
        """MIDI device management and routing."""

    # --- Sources ---

    def add_source(self, name: str, *, plugin: str = None,
                   sampler: bool = False, hw_channel: int = -1) -> Source:
        """Add a source to the engine.

        Specify exactly one of:
          plugin="Diva.vst3"  — plugin generator
          sampler=True        — sampler generator
          hw_channel=1        — audio hardware input
        """

    # --- Buses ---

    def add_bus(self, name: str) -> Bus:
        """Add a bus to the engine."""

    @property
    def master(self) -> Bus:
        """The master bus (always exists)."""

    # --- Sidechain ---

    def sidechain(self, proc: Processor, *, source: Source) -> None:
        """Route a source's audio as sidechain input to a processor."""

    # --- MIDI CC mapping ---

    def midi_cc_map(self, *, device: str, cc: int,
                    target: Processor, param: str) -> None:
        """Map a MIDI CC to a processor parameter."""

    # --- PDC ---

    @property
    def pdc_enabled(self) -> bool: ...

    @pdc_enabled.setter
    def pdc_enabled(self, enabled: bool) -> None: ...

    @property
    def total_latency(self) -> int: ...

    # --- Plugin management ---

    def load_plugin_cache(self, path: str) -> None:
        """Load plugin cache from XML file. Raises SqueezeError on failure."""

    @property
    def available_plugins(self) -> list[str]:
        """Available plugin names (sorted alphabetically)."""

    @property
    def num_plugins(self) -> int:
        """Number of plugins in the loaded cache."""

    # --- Audio device ---

    def start(self) -> None:
        """Start the audio device."""

    def stop(self) -> None:
        """Stop the audio device."""

    # --- Plugin editor ---

    @staticmethod
    def process_events(timeout_ms: int = 0) -> None:
        """Process pending JUCE GUI/message events."""

    # --- Testing ---

    def render(self, num_samples: int = 512) -> None:
        """Render one block in test mode."""

    # --- Query ---

    @property
    def source_count(self) -> int: ...

    @property
    def bus_count(self) -> int: ...
```

---

## `__init__.py` Public Exports

```python
from squeeze.squeeze import Squeeze
from squeeze.source import Source
from squeeze.bus import Bus
from squeeze.chain import Chain
from squeeze.processor import Processor
from squeeze.transport import Transport
from squeeze.midi import Midi, MidiDevice
from squeeze.types import ParamDescriptor

# Module-level utilities
from squeeze._helpers import SqueezeError, set_log_level, set_log_callback
```

---

## Invariants

- `Source`, `Bus`, `Processor` objects are lightweight proxies — they hold a handle and a reference
- `Processor.get_param()` always reads through the engine — no stale caching
- All error conditions raise `SqueezeError`
- Sub-objects (`Transport`, `Midi`) hold internal references — they do not outlive `Squeeze`
- There is one public Python API, not two layers
- `_ffi.py` and `_helpers.py` are internal implementation details

## Error Conditions

- `Squeeze()` construction failure: raises `SqueezeError`
- `Squeeze.add_source()` with no generator type: raises `ValueError`
- `Source.send()` with cycle-creating routing: raises `SqueezeError`
- `Bus.route_to()` with cycle-creating routing: raises `SqueezeError`
- `Bus.remove()` on Master: raises `SqueezeError`
- `Chain[index]` out of range: raises `IndexError`
- `Processor.set_param()` with unknown name: no-op (matches C++ behavior)
- All FFI failures: raises `SqueezeError` with message from C layer

## Does NOT Handle

- **Async or threaded Python access** — all calls are synchronous
- **Audio buffer access from Python** — no direct access to audio data
- **Pattern sequencing or composition helpers** — exposes scheduling primitives only

## Dependencies

- `squeeze._ffi` (internal ctypes bindings)
- `squeeze._helpers` (internal error/string/memory helpers)
- Python standard library only: `dataclasses`, `enum`, `typing`

## Thread Safety

All calls from a single thread (the control thread). Objects are proxies to the engine, which serializes under `controlMutex_`.

## Testing Strategy

- **One set of Python tests** covering the public API
- Source tests: create, routing, chain ops, MIDI assignment
- Bus tests: create, routing, sends, metering, mute
- Chain tests: append/insert/remove/index
- Processor tests: get/set params, descriptors, latency
- Transport tests: play/stop/pause, tempo, seek, loop
- Midi tests: device listing, open/close

## Example Usage

### Minimal

```python
from squeeze import Squeeze

s = Squeeze()
synth = s.add_source("Lead", plugin="Diva.vst3")
synth.route_to(s.master)
s.start()
```

### Complete session

```python
from squeeze import Squeeze, set_log_level

set_log_level(2)

with Squeeze() as s:
    s.load_plugin_cache("plugin-cache.xml")

    # Sources
    synth = s.add_source("Lead", plugin="Diva.vst3")
    drums = s.add_source("Drums", plugin="BFD.vst3")

    # Buses
    drum_bus = s.add_bus("Drum Bus")
    reverb_bus = s.add_bus("Reverb")

    # Insert effects
    drum_bus.chain.append("SSL_Channel.vst3")
    drum_bus.chain.append("API_2500.vst3")
    reverb_bus.chain.append("ValhallaRoom.vst3")

    # Routing
    synth.route_to(s.master)
    synth.send(reverb_bus, level=-6.0)
    drums.route_to(drum_bus)
    drum_bus.route_to(s.master)
    reverb_bus.route_to(s.master)

    # MIDI
    synth.midi_assign(device="Keylab", channel=1)
    drums.midi_assign(device="MPD", channel=10)

    # Parameters
    reverb_bus.chain[0].set_param("decay", 2.5)
    reverb_bus.chain[0].set_param("mix", 0.3)

    # Transport
    s.transport.tempo = 128
    s.transport.playing = True

    # Live: hot-swap an insert
    saturator = drum_bus.chain.insert(1, "Saturator.vst3")
    drum_bus.chain.remove(1)

    # Metering
    print(drum_bus.peak, drum_bus.rms)

    s.start()
```

### Drum kit with per-slice routing

```python
with Squeeze() as s:
    kick = s.add_source("Kick", plugin="sampler")
    snare = s.add_source("Snare", plugin="sampler")
    hat = s.add_source("Hat", plugin="sampler")

    drum_bus = s.add_bus("Drums")
    kick.route_to(drum_bus)
    snare.route_to(drum_bus)
    hat.route_to(drum_bus)

    kick.midi_assign(device="MPD", channel=10, note_range=(36, 36))
    snare.midi_assign(device="MPD", channel=10, note_range=(38, 38))
    hat.midi_assign(device="MPD", channel=10, note_range=(42, 42))

    drum_bus.chain.append("Bus Compressor.vst3")
    drum_bus.route_to(s.master)
```

### Sidechain compression

```python
with Squeeze() as s:
    bass = s.add_source("Bass", plugin="bass_synth.vst3")
    kick = s.add_source("Kick", plugin="sampler")

    sc_comp = bass.chain.append("Compressor.vst3")
    s.sidechain(sc_comp, source=kick)
```

### Offline rendering

```python
with Squeeze() as s:
    synth = s.add_source("Synth", plugin="test_synth")

    s.transport.tempo = 120.0
    for i, note in enumerate([60, 64, 67, 72]):
        synth.note_on(beat=i * 0.5, channel=1, note=note, velocity=0.8)
        synth.note_off(beat=i * 0.5 + 0.4, channel=1, note=note)

    s.transport.playing = True
    for _ in range(256):
        s.render(512)
```
