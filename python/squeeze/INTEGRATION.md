# Squeeze Integration Guide

Python client for the Squeeze audio engine — mixer-centric VST3/AU plugin hosting with beat-accurate MIDI scheduling, routing, and insert effects.

## Install

From source (requires building `libsqueeze_ffi` first — see root README):

```bash
cd python && pip install -e .
```

## Core Concepts

Squeeze is a mixer-centric audio engine. The mental model is a mixing console: **Sources** (instruments) feed into **Buses** (summing points) through insert **Chains** (effects racks). A permanent **Master** bus outputs to the audio device.

All errors raise `SqueezeError`. Import it from `squeeze`.

## Classes

### Squeeze (engine entry point)

```python
from squeeze import Squeeze

s = Squeeze(sample_rate=44100.0, block_size=512)
# or as context manager:
with Squeeze() as s: ...
```

The `plugins` kwarg controls plugin cache loading:
- `plugins=True` (default): searches upward from cwd for `plugin-cache.xml`, loads if found
- `plugins="/path/to/cache.xml"`: loads that file, raises on failure
- `plugins=False`: skips loading

Key methods:
- `s.add_source(name, *, plugin=None) -> Source`
- `s.add_bus(name) -> Bus`
- `s.master -> Bus` (always exists)
- `s.transport -> Transport`
- `s.midi -> Midi`
- `s.clock(resolution, latency_ms, callback) -> Clock`
- `s.perf -> Perf` (performance monitoring)
- `s.start(sample_rate=None, block_size=None)` / `s.stop()` — audio device (defaults to constructor args)
- `s.render(num_samples)` — headless test rendering
- `s.load_plugin_cache(path)` / `s.available_plugins` / `s.num_plugins`
- `s.batch()` — context manager, defers graph rebuild until exit
- `s.close()` — destroy engine (also called by context manager)
- `s.version -> str`
- `s.is_running -> bool` / `s.sample_rate -> float` / `s.block_size -> int`
- `s.source_count -> int` / `s.bus_count -> int`
- `Squeeze.process_events(timeout_ms)` — pump JUCE GUI events (static)

### Perf (performance monitoring)

```python
s.perf.enabled = True           # enable/disable (also gettable)
s.perf.slot_profiling = True    # per-slot profiling (also gettable)
s.perf.xrun_threshold = 0.8    # fraction of budget (also gettable)
s.perf.snapshot() -> dict       # callback_avg_us, cpu_load_percent, xrun_count, ...
s.perf.slots() -> list[dict]    # per-slot handle, avg_us, peak_us
s.perf.reset()                  # reset cumulative counters
```

### Source

```python
src = s.add_source("Lead", plugin="Diva")  # or omit plugin= for GainProcessor
```

Properties: `name`, `gain` (float, settable), `pan` (float -1..1, settable), `bypassed` (bool, settable), `handle -> int`
- `src["param"]` / `src["param"] = val` — shortcut for `src.generator.get_param()` / `set_param()`
- `src.route_to(bus)` — set main output bus
- `src.send(bus, *, level=0.0, tap="post") -> Send` — add send, returns Send object
- `src.chain -> Chain` (insert effects)
- `src.generator -> Processor` (the instrument)
- `src.midi_assign(*, device="", channel=0, note_range=(0, 127))`
- `src.note_on(beat, channel, note, velocity) -> bool`
- `src.note_off(beat, channel, note) -> bool`
- `src.cc(beat, channel, cc_num, cc_val) -> bool`
- `src.pitch_bend(beat, channel, value) -> bool`
- `src.remove() -> bool`

### Bus

```python
bus = s.add_bus("Reverb")
```

Properties: `name`, `gain`, `pan`, `bypassed` (all settable), `peak -> float`, `rms -> float`, `handle -> int`
- `bus.route_to(other_bus)` — route downstream
- `bus.send(other_bus, *, level=0.0, tap="post") -> Send`
- `bus.chain -> Chain`
- `bus.remove() -> bool` (cannot remove Master)

### Chain

```python
chain = src.chain  # or bus.chain
```

- `chain.append(plugin_path="") -> Processor`
- `chain.insert(index, plugin_path="") -> Processor`
- `chain.remove(index)`
- `len(chain) -> int`

### Processor

```python
proc = src.generator  # or from chain.append()/chain.insert()
```

- `proc["name"] -> float` / `proc["name"] = value` — get/set parameter by name
- `proc.get_param(name) -> float` / `proc.set_param(name, value)` — same, explicit form
- `proc.param_text(name) -> str` (e.g. "2.5 s")
- `proc.param_descriptors -> list[ParamDescriptor]`
- `proc.param_count -> int`
- `proc.has_editor -> bool`
- `proc.open_editor()` / `proc.close_editor()`
- `proc.automate(beat, param_name, value) -> bool`
- `proc.latency -> int` (samples)
- `proc.handle -> int`

`ParamDescriptor` fields: `name`, `default_value`, `min_value`, `max_value`, `num_steps`, `automatable`, `boolean`, `label`, `group`

### Send

```python
snd = src.send(reverb_bus, level=-6.0, tap="pre")
```

- `snd.level -> float` (settable) — send level in dB
- `snd.tap -> str` (settable) — "pre" or "post"
- `snd.send_id -> int`
- `snd.remove()` — remove this send

### Transport

```python
s.transport.play()
s.transport.stop()
s.transport.pause()
s.transport.tempo = 120.0      # BPM (also gettable)
s.transport.position -> float   # beats
s.transport.playing = True      # also gettable
s.transport.seek(beats=4.0)     # or seek(samples=44100)
s.transport.set_time_signature(4, 4)
s.transport.set_loop(start=0.0, end=16.0)
s.transport.looping = True      # also gettable
```

### Midi

```python
s.midi.devices -> list[MidiDevice]       # available devices
s.midi.open_devices -> list[MidiDevice]  # currently open
s.midi.route(device, source_handle, *, channel=0, note_range=(0, 127)) -> int
s.midi.unroute(route_id) -> bool
s.midi.routes -> list[MidiRouteInfo]     # active routes

device.name -> str
device.open()
device.close()
```

### Clock

```python
clock = s.clock(resolution=0.25, latency_ms=50.0, callback=my_fn)
# callback(beat: float) fires on a dedicated thread
clock.resolution -> float
clock.latency_ms -> float
clock.destroy()  # also called on GC
```

## Common Patterns

### Headless render (testing/offline)

```python
with Squeeze() as s:
    src = s.add_source("Synth")
    src.route_to(s.master)
    s.transport.tempo = 120.0
    src.note_on(0.0, 1, 60, 0.8)
    src.note_off(1.0, 1, 60)
    s.transport.play()
    for _ in range(100):
        s.render(512)
```

### Live audio output

```python
with Squeeze() as s:
    src = s.add_source("Lead", plugin="Vital")
    src.route_to(s.master)
    s.start()  # uses constructor sample_rate/block_size
    s.transport.play()
    Squeeze.process_events(5000)  # run for 5 seconds
    s.transport.stop()
    s.stop()
```

### Batch mutations (single graph rebuild)

```python
with s.batch():
    a = s.add_source("A")
    b = s.add_source("B")
    a.route_to(s.master)
    b.route_to(s.master)
```

### Generative music with ClockDispatch

```python
import random

def on_beat(beat):
    src.note_on(beat, 1, random.choice([60, 64, 67, 72]), 0.7)
    src.note_off(beat + 0.2, 1, 60)

clock = s.clock(resolution=0.25, latency_ms=50.0, callback=on_beat)
```

### Mixer with sends

```python
with Squeeze() as s:
    keys = s.add_source("Keys")
    bass = s.add_source("Bass")
    reverb = s.add_bus("Reverb")

    keys.route_to(s.master)
    snd = keys.send(reverb, level=-6.0, tap="pre")
    snd.level = -3.0   # adjust later
    bass.route_to(s.master)
    reverb.route_to(s.master)

    keys.gain, keys.pan = 0.8, -0.3
    bass.gain = 0.9
```

## Thread Model

All Python calls are synchronous and control-thread safe. Audio runs on a separate realtime thread internally. Clock callbacks fire on a dedicated clock thread — schedule events back into the engine from there (`note_on`, `note_off`, `automate` are thread-safe).

## Logging

```python
from squeeze import set_log_level, set_log_callback

set_log_level(2)  # 0=off, 1=warn, 2=info, 3=debug, 4=trace
set_log_callback(lambda level, msg: print(f"[{level}] {msg}"))
```

## Referencing from Your Project

In your project's `CLAUDE.md`:

```markdown
## Dependencies
- squeeze-audio: Audio engine (installed via pip).
  API reference: run `python -c "import squeeze; print(squeeze.INTEGRATION_GUIDE)"`
  and read that file before writing any Squeeze code.
```
