# Python Release & Consumer Documentation Specification

## Goal

Make the Squeeze Python package readily discoverable, installable, and usable by other Claude Code projects. All consumer-facing artifacts live in `python/` — a developer (or Claude Code session) in a separate project should be able to install the package, read `python/INTEGRATION.md`, and write correct code without ever opening the C++ source tree.

The root `README.md` is unchanged — it serves contributors browsing the full repo.

---

## 1. Type Annotations + `py.typed` Marker

### What

Claude Code (and all type-aware tooling) uses type information to understand APIs. The Python package needs:

- A `py.typed` marker file so tools know annotations are available
- Full type annotations on all public method signatures (parameters and return types)

### Tasks

1. **Create `python/squeeze/py.typed`** — empty file, PEP 561 marker.

2. **Audit every public class for complete annotations.** The following files need every public method signature annotated with parameter types and return types:

   | File | Class |
   |------|-------|
   | `squeeze.py` | `Squeeze` |
   | `source.py` | `Source` |
   | `bus.py` | `Bus` |
   | `chain.py` | `Chain` |
   | `processor.py` | `Processor` |
   | `transport.py` | `Transport` |
   | `midi.py` | `Midi`, `MidiDevice`, `MidiRouteInfo` |
   | `clock.py` | `Clock` |
   | `types.py` | `ParamDescriptor` |
   | `_helpers.py` | `SqueezeError`, `set_log_level`, `set_log_callback` (public re-exports) |

   What "complete" means:
   - Every `def` has typed parameters and a `-> ReturnType`
   - Properties have `-> Type`
   - Use `str`, `int`, `float`, `bool`, `list[X]`, `dict[K, V]`, `tuple[X, Y]`, `X | None`
   - Use `from __future__ import annotations` for forward references
   - Callback types use `typing.Callable[[ArgTypes], ReturnType]`

3. **Update `python/pyproject.toml`** — add to `[tool.setuptools.package-data]`:
   ```toml
   [tool.setuptools.package-data]
   squeeze = ["py.typed"]
   ```

### Acceptance

- `python/squeeze/py.typed` exists
- `mypy python/squeeze/` reports no missing annotations on public methods
- All existing tests still pass

---

## 2. Consumer-Facing README (`python/README.md`)

### What

A separate README inside `python/` that serves two audiences: PyPI package page and Claude Code in other projects. The root `README.md` stays unchanged for repo visitors. `python/pyproject.toml` points `readme = "README.md"` which resolves to `python/README.md`.

This README must put install, quick-start, and API shape in the first 50 lines — LLMs scan top-down.

### Tasks

1. **Create `python/README.md`** with this structure:

   ```markdown
   # Squeeze

   Python client for the Squeeze audio engine — mixer-centric VST3/AU
   plugin hosting with beat-accurate MIDI scheduling.

   ## Install

   ```bash
   pip install squeeze-audio
   ```

   Or from source (requires building libsqueeze_ffi first):
   ```bash
   cd python && pip install -e .
   ```

   ## Quick Start

   ```python
   from squeeze import Squeeze

   with Squeeze() as s:
       synth = s.add_source("Lead", plugin="Diva.vst3")
       synth.route_to(s.master)

       s.transport.tempo = 120.0
       synth.note_on(0.0, channel=1, note=60, velocity=0.8)
       synth.note_off(1.0, channel=1, note=60)

       s.transport.play()
       s.render(512)
   ```

   ## API at a Glance

   | Class | Role |
   |-------|------|
   | `Squeeze` | Engine entry point. Creates sources, buses; controls transport and audio device. |
   | `Source` | Sound generator (plugin synth or gain). Has insert chain, gain/pan, routing, MIDI. |
   | `Bus` | Summing point with insert chain and routing. Master bus always exists. |
   | `Chain` | Ordered list of insert processors on a Source or Bus. |
   | `Processor` | Single effect/instrument. Parameter access by name. |
   | `Transport` | Play, stop, tempo, seek, loop. |
   | `Midi` | MIDI device listing and management. |
   | `Clock` | Beat-driven callbacks for generative music. |

   See [INTEGRATION.md](INTEGRATION.md) for the full API reference.

   ## Requirements

   - Python >= 3.10
   - libsqueeze_ffi shared library (built from the Squeeze C++ engine)

   ## License

   GPLv3
   ```

2. **Update `python/pyproject.toml`** — set `readme = "README.md"`.

### Acceptance

- `python/README.md` exists
- First 50 lines contain: install command, quick-start code, API table
- `pyproject.toml` `readme` field points to it
- Root `README.md` is untouched

---

## 3. Integration Guide (`python/INTEGRATION.md`)

### What

A standalone document optimized for LLM consumption. When another project's `CLAUDE.md` says `See /path/to/squeeze/python/INTEGRATION.md`, Claude Code reads this file and knows everything it needs to write correct Squeeze code. This is NOT a tutorial — it's a compact API reference with patterns.

No internal details (SPSC queues, snapshots, FFI internals, C++ architecture). Only what a consumer needs to call.

### Tasks

Create `python/INTEGRATION.md` with these sections:

```markdown
# Squeeze Integration Guide

## Install

pip install squeeze-audio

## Core Concepts

Squeeze is a mixer-centric audio engine. The mental model is a mixing
console: Sources (instruments) feed into Buses (summing points) through
insert Chains (effects racks). A permanent Master bus outputs to the
audio device.

## Classes

### Squeeze (engine entry point)

from squeeze import Squeeze

s = Squeeze(sample_rate=44100.0, block_size=512)
# or as context manager:
with Squeeze() as s: ...

Key methods:
- s.add_source(name, *, plugin=None) -> Source
- s.add_bus(name) -> Bus
- s.master -> Bus  (always exists)
- s.transport -> Transport
- s.midi -> Midi
- s.clock(resolution, latency_ms, callback) -> Clock
- s.start() / s.stop()  — audio device (defaults to constructor sample_rate/block_size)
- s.render(num_samples)  — headless test rendering
- plugins=True (default) auto-finds plugin-cache.xml; or pass a path string
- s.load_plugin_cache(path) / s.available_plugins / s.num_plugins
- s.batch()  — context manager, defers graph rebuild
- s.close()

### Source

src = s.add_source("Lead", plugin="Diva.vst3")

Properties: name, gain (float), pan (float, -1..1), bypassed (bool)
- src.route_to(bus) — set main output bus
- src.send(bus, level=-6.0, tap="post") -> send_id
- src.remove_send(send_id)
- src.chain -> Chain (insert effects)
- src.generator -> Processor (the instrument)
- src.midi_assign(device="", channel=0, note_range=(0, 127))
- src.note_on(beat, channel, note, velocity)
- src.note_off(beat, channel, note)
- src.cc(beat, channel, cc_num, cc_val)
- src.remove()

### Bus

bus = s.add_bus("Reverb")

Properties: name, gain, pan, bypassed, peak (float), rms (float)
- bus.route_to(other_bus)
- bus.send(other_bus, level, tap) -> send_id
- bus.chain -> Chain
- bus.remove()  (cannot remove Master)

### Chain

chain = src.chain  # or bus.chain
chain.append("EQ.vst3") -> Processor
chain.insert(index, "Compressor.vst3") -> Processor
chain.remove(index)
chain[index] -> Processor
len(chain) -> int

### Processor

proc = chain[0]
proc.get_param("decay") -> float
proc.set_param("decay", 2.5)
proc.param_text("decay") -> str ("2.5 s")
proc.param_descriptors -> list[ParamDescriptor]
proc.param_count -> int
proc.latency -> int (samples)
proc.open_editor() / proc.close_editor()
proc.automate(beat, param_name, value)

### Transport

s.transport.play()
s.transport.stop()
s.transport.pause()
s.transport.tempo = 120.0  (BPM)
s.transport.position -> float (beats)
s.transport.playing = True/False
s.transport.seek(beats=4.0)
s.transport.set_time_signature(4, 4)
s.transport.set_loop(start=0.0, end=16.0)
s.transport.looping = True

### Midi

s.midi.devices -> list[MidiDevice]
s.midi.open_devices -> list[MidiDevice]
device.name -> str
device.open() / device.close()

### Clock

clock = s.clock(resolution=0.25, latency_ms=50.0, callback=my_fn)
# callback(beat: float) fires on a dedicated thread

## Error Handling

All errors raise SqueezeError.
from squeeze import SqueezeError

## Common Patterns

### Headless render (testing/offline)
with Squeeze() as s:
    src = s.add_source("Synth")
    src.route_to(s.master)
    s.transport.tempo = 120.0
    src.note_on(0.0, 1, 60, 0.8)
    src.note_off(1.0, 1, 60)
    s.transport.play()
    for _ in range(100):
        s.render(512)

### Live audio output
s.start()           # opens audio device
s.transport.play()
Squeeze.process_events(5000)  # run for 5 seconds
s.transport.stop()
s.stop()

### Batch mutations (single graph rebuild)
with s.batch():
    a = s.add_source("A")
    b = s.add_source("B")
    a.route_to(s.master)
    b.route_to(s.master)

### Generative music with ClockDispatch
def on_beat(beat):
    src.note_on(beat, 1, random.choice([60, 64, 67]), 0.7)
    src.note_off(beat + 0.25, 1, 60)

clock = s.clock(resolution=0.25, latency_ms=50.0, callback=on_beat)

## Thread Model

All Python calls are synchronous and control-thread safe. Audio runs
on a separate realtime thread internally. Clock callbacks fire on a
dedicated clock thread — schedule events back into the engine from
there (note_on, note_off, automate are thread-safe).

## Referencing from Your Project

In your project's CLAUDE.md:

  ## Dependencies
  - squeeze-audio: Audio engine for plugin hosting and mixing.
    API ref: <path-to-squeeze>/python/INTEGRATION.md
```

### Acceptance

- `python/INTEGRATION.md` exists
- Contains every public class and method with signatures
- Contains at least 3 usage patterns (headless, live, batch)
- No internal implementation details (no mention of SPSC, snapshots, FFI internals)
- Fits in ~200 lines (LLM-friendly length)

---

## 4. PyPI Publish

### What

Make the package installable via `pip install squeeze-audio` so any project can depend on it without cloning the repo. The main challenge is that the Python package depends on a native shared library (`libsqueeze_ffi.dylib` / `.so` / `.dll`).

### Package Name

`squeeze-audio` on PyPI (the name `squeeze` is likely taken). Import name stays `squeeze`.

### Tasks

1. **Update `python/pyproject.toml`**:

   ```toml
   [project]
   name = "squeeze-audio"
   version = "0.3.0"
   description = "Python client for the Squeeze audio engine — mixer-centric VST/AU plugin hosting"
   requires-python = ">=3.10"
   license = "GPL-3.0-only"
   readme = "README.md"
   keywords = ["audio", "dsp", "vst", "plugin", "mixer", "juce"]
   classifiers = [
       "Development Status :: 3 - Alpha",
       "Topic :: Multimedia :: Sound/Audio",
       "License :: OSI Approved :: GNU General Public License v3 (GPLv3)",
       "Programming Language :: Python :: 3",
   ]

   [project.urls]
   Homepage = "https://github.com/<owner>/squeeze"
   Documentation = "https://github.com/<owner>/squeeze/blob/main/python/INTEGRATION.md"

   [tool.setuptools.package-data]
   squeeze = ["py.typed"]
   ```

2. **Shared library bundling strategy** — choose one:

   **Option A: Pre-built wheels with bundled `.dylib`/`.so`** (recommended for real distribution)
   - Use `cibuildwheel` or `scikit-build-core` to build platform-specific wheels
   - Bundle `libsqueeze_ffi` inside the wheel
   - Pro: `pip install` just works
   - Con: CI complexity, must build for each platform

   **Option B: Source distribution + system library** (simpler, acceptable for early stage)
   - Publish a pure-Python sdist
   - Document that the user must build and install `libsqueeze_ffi` separately
   - `_ffi.py` finds the library via `LD_LIBRARY_PATH` / `DYLD_LIBRARY_PATH` / system search
   - Pro: Simple publishing
   - Con: Two-step install for users

   **Option C: No PyPI yet — Git dependency** (minimum viable)
   - Other projects use `pip install git+https://github.com/<owner>/squeeze.git#subdirectory=python`
   - Still requires the shared library built locally
   - Pro: Zero publishing overhead
   - Con: Not a real release

   **Recommendation:** Start with Option C now (unblocks integration), migrate to Option A when CI is set up. Update `pyproject.toml` and `python/README.md` for Option C immediately.

3. **Update `squeeze/_ffi.py` library loading** — ensure it searches:
   - `../../build/` relative to the package (development — one level up from `python/`)
   - System library paths (installed)
   - `SQUEEZE_LIB_PATH` environment variable (explicit override)
   - Raise a clear `SqueezeError` with install instructions if not found

4. **Test the install flow:**
   ```bash
   # From a clean venv in a separate directory:
   pip install git+https://github.com/<owner>/squeeze.git#subdirectory=python
   python -c "from squeeze import Squeeze; print('ok')"
   ```

### Acceptance

- `pip install` from git URL succeeds in a clean venv
- `from squeeze import Squeeze` works when `libsqueeze_ffi` is on the library path
- Clear error message when the shared library is not found
- `python/pyproject.toml` has correct metadata, keywords, classifiers, URLs

---

## File Summary

All consumer-facing artifacts live in `python/`:

| File | Purpose |
|------|---------|
| `python/README.md` | PyPI page + first thing Claude Code reads (install, quick-start, API table) |
| `python/INTEGRATION.md` | Full API reference optimized for LLM consumption (~200 lines) |
| `python/squeeze/py.typed` | PEP 561 marker — signals type annotations are available |
| `python/pyproject.toml` | Package metadata, keywords, classifiers, URLs |

The root `README.md` is unchanged — it serves contributors browsing the full GitHub repo.

## Implementation Order

1. **Type annotations + `py.typed`** — no dependencies, immediate value
2. **Integration guide (`python/INTEGRATION.md`)** — can write now from existing API surface
3. **Consumer README (`python/README.md`)** — depends on knowing the install command
4. **PyPI publish** — needs decisions on library bundling; start with git URL

## Does NOT Include

- MCP server integration (separate spec if needed)
- Auto-generated API docs (sphinx/mkdocs) — the integration guide is the LLM-readable alternative
- CI/CD pipeline setup — separate concern
- Versioning policy — follow semver, current version is 0.3.0
- Changes to the root `README.md`
