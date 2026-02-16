# CLAUDE.md — Squeeze v2

Squeeze is a modern, modular JUCE/C++ audio engine for hosting VST/AU plugins and routing audio through programmable graphs with a C ABI for multi-language integration.

v2 is a ground-up rewrite. The v1 codebase lives in `./squeeze/` for reference (architecture, specs, DSP algorithms, test patterns) but no code is carried over directly.

---

## Project Stack

- **Language:** C++17
- **Framework:** JUCE (plugin hosting, audio I/O, DSP, MIDI, GUI)
- **Public API:** C ABI (`squeeze_ffi`) — opaque handles, plain C types, no C++ in the public header
- **Python package:** `python/` — proper Python package (`squeeze`) wrapping the C ABI via ctypes. Maintained alongside `squeeze_ffi.h`. Includes its own unit tests, buildable via `pyproject.toml`.
- **Build:** CMake 3.24+
- **Tests:** Catch2 v3
- **Dependencies fetched via CMake FetchContent:** JUCE, signalsmith-stretch, Catch2
- **License:** GPLv3

---

## Build & Test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```

---

## Architecture Overview

Two concentric concerns:

1. **Audio thread** — owns the audio callback, strictly lock-free
2. **Control thread(s)** — FFI callers (Python, Rust, Node.js, etc.); serialized by `Engine::controlMutex_`
3. **Communication** — lock-free SPSC queues bridging control → audio (CommandQueue for infrastructure commands, EventScheduler for beat-timed musical events)

The host language (Python REPL, Rust CLI, etc.) provides the user-facing interface. Squeeze exposes only the C ABI — no scripting runtime, no network servers.

### Thread Model

| Thread | Locks controlMutex? | May block? |
|--------|---------------------|------------|
| Audio (processBlock) | Never | Never |
| FFI caller (host language) | Yes | Yes |
| MIDI callback | Never | No |
| GUI / message thread | Never | Yes |

**Lock ordering:** `controlMutex_` must never be held when acquiring `MessageManagerLock`.

### Component Dependency Order

Build bottom-up. A component may only depend on those above it:

```
0.  Logger                (no dependencies — owns its internal lock-free ring buffer)
1.  Port                  (no dependencies)
2.  Node                  (Port)
3.  Graph                 (Node, Port)
4.  Engine                (Graph — node ownership, ID allocator, topology cascade handling)
5.  GroupNode             (Node, Graph, Engine)
6.  MidiSplitterNode      (Node — graph-level MIDI routing with note-range filtering)
7.  SPSCQueue             (no dependencies)
8.  CommandQueue           (SPSCQueue)
9.  Transport             (no dependencies)
10. Buffer                (no dependencies)
11. PerfMonitor           (no dependencies)
12. PluginNode            (Node, JUCE plugin hosting)
13. SamplerVoice          (Buffer)
14. TimeStretchEngine     (signalsmith-stretch)
15. VoiceAllocator        (SamplerVoice)
16. SamplerNode           (Node, VoiceAllocator, TimeStretchEngine, Buffer)
17. RecorderNode          (Node, Buffer)
18. MidiRouter            (SPSCQueue)
19. EventScheduler        (SPSCQueue)
20. ScopeTap / Metering   (SPSCQueue / SeqLock)
```

Logger is tier 0 — it has no dependencies and every subsequent component uses it. It ships with `sq_set_log_level`, `sq_set_log_callback`, and corresponding Python functions.

Engine grows incrementally across tiers. At tier 4 it provides node ownership, a global ID allocator, and topology cascade handling. Later tiers add CommandQueue, Transport, Buffer, PerfMonitor, and audio device management.

GroupNode is tier 5 — a core graph primitive that depends on Engine for ID allocation. Composite node types (mixers, channel strips, kits) are GroupNode configurations, not separate node classes.

**Every tier ships a working FFI and Python client.** Each tier extends the C++ engine, the C API surface, and `python/squeeze.py` together. Do not build internal components without their corresponding `sq_` functions and Python methods.

Do not skip ahead. Each component: spec → tests → implement → review.

---

## Development Cycle

### Step 1: Write the Specification

Before any code, create a specification document in `docs/specs/{Component}.md`.

**Specification template:**

```markdown
# {Component} Specification

## Responsibilities
- What this component does (bullet points)
- Keep it focused—if the list is long, consider splitting

## Interface
- Public methods with signatures
- Use pseudocode or C++ declarations
- Include parameter and return types

## Invariants
- Properties that must always be true
- These become test cases

## Error Conditions
- What can fail and how failures are reported
- These become test cases

## Does NOT Handle
- Explicitly list out-of-scope concerns
- Prevents scope creep during implementation

## Dependencies
- What other components this one uses
- Must be components that already exist or are mocked

## Thread Safety
- Which thread(s) call which methods
- What synchronization is required (or explicitly: none)

## Example Usage
- Short code snippet showing typical use
```

### Step 2: Write Acceptance and Unit Tests from the Specification

**FFI tests are the acceptance tests.** If it works through `squeeze_ffi.h`, it works. C++ unit tests are supplementary for internal invariants.

- **Framework:** Catch2 v3 with `Catch2::Catch2WithMain`
- **FFI tests:** Primary. Test every feature through the C API — these are the tests that must pass.
- **C++ unit tests:** Secondary. Test internal invariants, edge cases, and RT-safety properties that aren't visible through the C API.
- **Naming:** Descriptive sentences — `"sq_connect rejects connections that would create cycles"`
- **Independence:** Each test creates own fixtures, no shared mutable state
- **Coverage:** All `sq_` functions exercised, all error codes returned, edge cases (0, 1, max, empty, boundary)
- **Engine testing:** `Engine::prepareForTesting(sr, bs)` bypasses audio device for headless tests
- **Mocks:** Implement real interfaces, record calls, return configurable values. Keep unit tests with mocks even after integration tests exist.

### Step 3: Implement to Pass Tests

This includes the C++ component, its `sq_` functions in `squeeze_ffi.h`/`.cpp`, and the corresponding Python methods in `python/squeeze.py`.

### Step 4: Review

**Review checklist for generated implementation:**

- [ ] All tests pass
- [ ] Public interface matches spec exactly (C++, C ABI, and Python)
- [ ] No functionality beyond what's specified
- [ ] Realtime safety (if applicable): no allocation, no blocking, no unbounded loops
- [ ] Thread safety
- [ ] No obvious performance issues
- [ ] Audit for code duplication, dead code, overly large source code files, and opportunities for refactoring

### Step 5: Integration Check

After implementation, verify the component works with its dependencies:

- If dependencies were mocked, write integration tests with real components
- Run the full test suite to catch regressions
- Update the architecture document if any decisions were made

---

## Realtime Safety Rules

All code on the audio thread must be RT-safe.

### Forbidden on audio thread
- `new` / `delete` / `malloc` / `free`
- `std::vector::push_back`, `std::map` insert, any allocating container op
- `std::mutex`, `std::condition_variable`, any blocking primitive
- File I/O, network I/O, `std::cout`
- `std::string` construction (allocates)

### Allowed on audio thread
- Lock-free SPSC queues
- Atomic operations
- `std::array`, pre-allocated buffers
- Arithmetic, DSP, SIMD
- Calling `Node::process()` (nodes must also be RT-safe)

### Deferred deletion pattern
Old graph snapshots and buffers are pushed to a garbage queue by the audio thread and freed later by the control thread.

---

## Key Design Patterns

### Graph snapshot swap
The control thread builds a `GraphSnapshot` (execution order, pre-allocated buffers per node), then atomically swaps it in via the CommandQueue. The audio thread never modifies the graph.

### Topology is derived
Users declare data flow between nodes; the engine computes execution order automatically.

### Sub-block parameter splitting
When parameter changes arrive mid-block (from EventScheduler), the engine splits processing into sub-blocks at each change point. Nodes are unaware — they just see shorter `numSamples`.

### Error reporting
Internal C++ functions return `bool`/`int` with `std::string& errorMessage` out-params. The C ABI returns error codes (`SqResult`) and provides `sq_error_message()` for the last error string.

### Connections as API
Topology is derived from explicit `connect()` calls. Connections validate: matching signal types, no cycles (BFS detection). Both audio and MIDI allow fan-in — the Engine sums multiple audio sources into the input buffer before calling `process()`.

### Explicit output routing
No auto-summing anywhere. Audio goes where you connect it, nowhere else. Engine provides a built-in output node representing the audio device. All audio chains must explicitly connect to it:

```python
engine.connect(synth, "out", engine.output, "in")
```

Nodes with unconnected audio outputs are warned about at info level ("node 'Diva' output 'out' is unconnected") but produce no sound. This applies uniformly to Engine and GroupNode — all routing is explicit at every level.

### GroupNode (composite subgraph)
A `GroupNode` owns an internal `Graph` and implements the `Node` interface. It exports selected internal ports as group-level ports via `exportPort()` / `unexportPort()`. From the parent graph's perspective, it's just another node. Designed in from the start as a core primitive (tier 4) — composite node types like mixers, channel strips, and kits are GroupNode configurations, not separate classes.

### Dynamic ports
Most nodes declare fixed ports at construction. GroupNode supports dynamic port export/unexport on the control thread — adding an input to a mixer bus or inserting an FX slot does not require tearing down and rebuilding the node. Port changes are graph mutations: modify ports, reconnect, rebuild snapshot, atomic swap. Removing a port auto-disconnects any connections referencing it.

### Scope / meter taps
Audio thread publishes metering data via SeqLock (like PerfMonitor). Scope taps use SPSC ring buffers. FFI callers read the latest values — no IPC, no shared memory, just in-process lock-free reads.

### C ABI conventions
- Opaque handles: `SqEngine`, `SqNode`, `SqScope`, etc. (pointers to internal C++ objects)
- All functions prefixed `sq_`
- Error returns via `SqResult` enum
- No C++ types in the public header — plain C, `extern "C"`
- Caller manages handle lifetime via `sq_*_create` / `sq_*_destroy` pairs

---

## Coding Conventions

- **Namespace:** `squeeze`
- **Headers:** `.h` with `#pragma once`
- **Naming:** `camelCase` for methods/variables, `PascalCase` for types, `kConstantName` for enum values, `member_` suffix for private members
- **Parameters:** String-based API on `Node` — `getParameter(name)`, `setParameter(name, value)`. No index in the public interface (Node, C ABI, or Python). RT-safe dispatch for sample-accurate automation is an Engine/EventScheduler concern (pre-resolved tokens), not a Node concern. Subclasses store parameters however they choose internally.
- **No over-engineering:** No features beyond the spec. No premature abstractions. Three similar lines > one premature helper.

### Logging Standards

Every component must include comprehensive logging using the `SQ_*` macros. Logging is not optional — it is part of the implementation, not an afterthought.

**Level guidelines:**

| Level | When to use | Examples |
|-------|-------------|---------|
| `SQ_WARN` / `SQ_WARN_RT` | Something went wrong or was dropped | Xruns, queue overflows, failed operations, dropped events |
| `SQ_INFO` / `SQ_INFO_RT` | High-level operational milestones | Engine start/stop, device open/close, plugin loaded, graph rebuilt |
| `SQ_DEBUG` / `SQ_DEBUG_RT` | Every public API call and state transition | connect/disconnect, node add/remove, buffer load, snapshot swap |
| `SQ_TRACE` / `SQ_TRACE_RT` | Per-message/per-event granularity | Individual MIDI messages, per-event scheduling, per-block details |

**Rules:**
- Use `SQ_*_RT` variants on the audio thread, plain `SQ_*` on the control thread. Never `fprintf`/`std::cout` directly.
- Log all public method entry points at `debug` or `info` level with key parameters.
- Log all error paths at `warn` level with enough context to diagnose the problem.
- Format: include relevant IDs, names, and numeric values — `SQ_DEBUG("connect: %d:%s -> %d:%s", srcId, srcPort, dstId, dstPort)`, not `SQ_DEBUG("connected")`.

---

## Python Package (`python/`)

The `python/` directory is a proper Python package, buildable and eventually publishable.

### Structure

```
python/
├── pyproject.toml          # Package metadata, dependencies, build config
├── squeeze/
│   ├── __init__.py         # Public API re-exports
│   ├── engine.py           # Engine wrapper class
│   ├── ...                 # Other modules as the API surface grows
│   └── _ffi.py             # ctypes bindings (internal)
└── tests/
    ├── conftest.py          # Shared fixtures (e.g. lib path, engine setup)
    └── test_engine.py       # Tests mirroring the C FFI acceptance tests
```

### Conventions

- **Test framework:** pytest
- **Test location:** `python/tests/`
- **Run tests:** `cd python && pytest`
- **Every `sq_` function** exposed through the C ABI must have a corresponding Python method and a Python test
- **Python tests mirror C FFI tests.** If a Catch2 FFI test exercises `sq_foo()`, there should be a pytest case exercising `squeeze.Engine.foo()` (or equivalent).
- **No standalone scripts.** `squeeze.py` as a single-file module is replaced by the package layout above.

### Build & Install

```bash
cd python
pip install -e .          # Editable install for development
pytest                    # Run Python tests
```

### Development Cycle Integration

The existing rule — **every tier ships a working FFI and Python client** — extends to the Python test suite. When implementing a new component:

1. Add the `sq_` C ABI functions
2. Add the Python wrapper methods in `squeeze/`
3. Add Python tests in `python/tests/`
4. All three (C++ Catch2 tests, C FFI Catch2 tests, Python pytest tests) must pass before the tier is complete

---

## v1 Reference

The `./squeeze/` directory contains the complete v1 implementation. Key reference material:

- `squeeze/ARCHITECTURE.md` — full architectural overview
- `squeeze/docs/specs/` — 25 component specifications
- `squeeze/docs/AUDIT.md` — component audit with gap analysis
- `squeeze/docs/transition_plan.md` — C ABI transition plan
- `squeeze/docs/discussion/` — design rationale documents
- `squeeze/src/core/` — all DSP and engine implementations
- `squeeze/tests/core/` — 589+ test cases as patterns

Consult v1 for domain knowledge (DSP algorithms, parameter ranges, thread safety patterns, edge cases discovered during development) but implement fresh.

---

## Git Workflow

- One component per branch: `feature/{component-name}`
- Commit format: `{component}: {description}`
- PR requires: spec complete, all tests pass, no static analysis warnings
