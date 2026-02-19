# Squeeze

A modular C++17 audio engine for hosting VST3/AU plugins, built on JUCE with a mixer-centric architecture and a C ABI for multi-language integration.

Squeeze replaces the general-purpose node-graph model used by most DAW engines with purpose-built mixer primitives — Sources, Buses, Chains, and Processors — giving you a structured, predictable signal flow without the complexity of arbitrary routing.

## Key Features

- **Mixer-centric architecture** — Sources (instruments) route through insert Chains to Buses (summing points) with pre/post-fader sends. A permanent Master bus outputs to the audio device.
- **VST3/AU plugin hosting** — Load any VST3 or AU plugin as a Source generator or Chain insert. Open plugin editor GUIs from Python.
- **Realtime safe** — The audio thread never allocates, never blocks, never locks. All mutations go through lock-free SPSC queues with snapshot-swap garbage collection on the control thread.
- **Beat-accurate event scheduling** — Schedule MIDI notes, CCs, pitch bends, and parameter changes at beat positions. The EventScheduler resolves them to sample-accurate offsets within each audio block.
- **ClockDispatch** — Beat-driven callback system for generative music. A Python callback fires N ms ahead of a beat, scheduling events back into the engine with sample-accurate timing.
- **C ABI first** — `squeeze_ffi.h` exposes opaque handles and plain C types. Python, Rust, Node.js, or any language with a C FFI can drive the engine.
- **Python package** — `squeeze` wraps the C ABI with a clean, Pythonic API: `Squeeze`, `Source`, `Bus`, `Chain`, `Processor`, `Transport`, `Midi`.
- **Performance monitoring** — Seqlock-based audio thread instrumentation with rolling stats, xrun detection, and per-source/bus slot profiling.

## Quick Start

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```

**Requirements:** CMake 3.24+, C++17 compiler. Dependencies (JUCE, Catch2) are fetched automatically via CMake FetchContent.

### Install the Python package

```bash
cd python
pip install -e .
```

### Hello World

```python
from squeeze import Squeeze

with Squeeze() as engine:
    print(f"Squeeze {engine.version}")
```

### Build a mixer and render

```python
from squeeze import Squeeze

with Squeeze(44100.0, 512) as s:
    # create sources
    keys = s.add_source("Keys")
    bass = s.add_source("Bass")

    # create an aux return bus
    reverb = s.add_bus("Reverb")

    # routing: keys → master + reverb send, bass → master
    keys.route_to(s.master)
    keys.send(reverb, level=-6.0, tap="pre")
    bass.route_to(s.master)
    reverb.route_to(s.master)

    # mix levels
    keys.gain, keys.pan = 0.8, -0.3
    bass.gain = 0.9
    s.master.gain = 0.85

    # schedule a melody (beat-accurate)
    s.transport.tempo = 120.0
    for beat, note in [(0, 60), (1, 64), (2, 67), (3, 72)]:
        keys.note_on(beat, channel=1, note=note, velocity=0.8)
        keys.note_off(beat + 0.9, channel=1, note=note)

    # render headless
    s.transport.play()
    for _ in range(200):
        s.render(512)

    print(f"master peak: {s.master.peak:.4f}")
```

### Load a VST3 synth and play audio

```python
from squeeze import Squeeze

with Squeeze(44100.0, 512) as s:
    s.load_plugin_cache("plugin-cache.xml")

    synth = s.add_source("Lead", plugin="Vital")
    synth.route_to(s.master)

    s.transport.tempo = 120.0
    for note in [60, 64, 67]:
        synth.note_on(0.0, channel=1, note=note, velocity=0.75)
        synth.note_off(7.5, channel=1, note=note)

    s.start(44100.0, 512)       # open audio device
    s.transport.play()

    Squeeze.process_events(6000) # play for 6 seconds

    s.transport.stop()
    s.stop()
```

## Architecture

```
Source ("Keys")          Source ("Bass")
  │ generator (plugin)     │ generator (plugin)
  │ insert chain           │ insert chain
  │ gain / pan             │ gain / pan
  ├──► Bus "Reverb"        │
  │    (pre-fader send)    │
  │                        │
  ▼                        ▼
Bus "Master" ◄──────── Bus "Reverb"
  │                      insert chain
  ▼
Audio Device
```

**Signal flow:** Sources generate audio through insert chains, then route to buses. Buses sum their inputs, process through their own insert chains, and route downstream. The Master bus is the final output.

**Thread model:**
- **Audio thread** — `Engine::processBlock` runs the mixer graph. Never allocates or locks.
- **Control thread** — Mutates the mixer via `CommandQueue` (lock-free SPSC). Old snapshots are garbage-collected here.
- **Message thread** — JUCE GUI for plugin editor windows.
- **Clock thread** — `ClockDispatch` fires beat callbacks on a dedicated thread with configurable lookahead.

See `docs/ARCHITECTURE.md` for the full system design.

## Project Structure

```
src/
├── core/           C++ engine (Engine, Source, Bus, Chain, Processor,
│                   Transport, EventScheduler, ClockDispatch, PerfMonitor,
│                   MidiRouter, PluginProcessor, PluginManager, AudioDevice, ...)
├── ffi/            C ABI — squeeze_ffi.h / .cpp
└── gui/            Plugin editor window management

tests/
├── core/           Catch2 unit tests
└── ffi/            Catch2 FFI acceptance tests

python/
├── squeeze/        Python package (Squeeze, Source, Bus, Chain, Processor, ...)
├── tests/          pytest suite
└── examples/       Demo scripts (quickstart, play_synth, live_midi, clock, perf)

docs/
├── ARCHITECTURE.md
├── specs/          26 component specifications
└── discussion/     Design rationale documents
```

## Examples

| Script | What it does |
|--------|-------------|
| `examples/hello.py` | Minimal hello world |
| `python/examples/quickstart.py` | Two-source mixer, sends, sequencing, headless render |
| `python/examples/play_synth.py` | Load a VST3 synth, schedule MIDI, play audio, optional GUI |
| `python/examples/live_midi.py` | MIDI keyboard → VST synth → speakers |
| `python/examples/clock_pentatonic.py` | Generative music with ClockDispatch beat callbacks |
| `python/examples/perf_monitor.py` | Performance monitoring dashboard |

## Running Tests

```bash
# C++ tests (Catch2)
cd build && ctest --output-on-failure

# Python tests (pytest)
cd python && pytest
```

## License

GPLv3
