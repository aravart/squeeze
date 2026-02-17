# CLAUDE.md — Squeeze v2

Squeeze is a modern, modular JUCE/C++ audio engine for hosting VST/AU plugins and routing audio through programmable graphs with a C ABI for multi-language integration.

v2 is a ground-up rewrite. The v1 codebase lives in `./squeeze/` for reference (architecture, specs, DSP algorithms, test patterns) but no code is carried over directly.

---

## Project Stack

- **Language:** C++17
- **Framework:** JUCE (plugin hosting, audio I/O, DSP, MIDI, GUI)
- **Public API:** C ABI (`squeeze_ffi`) — opaque handles, plain C types, no C++ in the public header
- **Python package:** `python/` — proper Python package (`squeeze`) with two layers: a low-level 1:1 ctypes wrapper (`_low_level.Squeeze`) and a high-level Pythonic API (`engine.Engine`, `node.Node`, etc.). Both layers are maintained alongside `squeeze_ffi.h`. Includes its own unit tests, buildable via `pyproject.toml`.
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

## Architecture

See `docs/ARCHITECTURE.md` for system design, component architecture, thread model, dependency order, data flow examples, realtime safety rules, and design patterns.

**Every tier ships a working FFI and Python client.** Each tier extends the C++ engine, the C API surface, and both Python layers together. Do not build internal components without their corresponding `sq_` functions, low-level `Squeeze` methods, and high-level `Engine`/`Node` methods.

Do not skip ahead. Each component: spec → tests → implement → review.

---

## Development Cycle

### Step 1: Write or Update the Specification

Before any code, create or update a specification document in `docs/specs/{Component}.md`.

- **New component:** Write the spec from scratch before any implementation.
- **Existing component:** If the implementation will change the interface, add new behavior, or expand responsibilities, **update the spec first**. The spec is the source of truth — implementation must not diverge from it. If you discover during implementation that the spec needs to change, stop and update the spec before continuing.
- **Spec drift is a bug.** Every public-facing change (new method, new command type, new parameter, changed behavior) must be reflected in the spec.

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

This includes the C++ component, its `sq_` functions in `squeeze_ffi.h`/`.cpp`, the corresponding low-level Python methods in `python/squeeze/_low_level.py`, and the high-level Python wrappers in `python/squeeze/` (engine, node, transport, midi, types as appropriate).

### Step 4: Review

**Review checklist for generated implementation:**

- [ ] All tests pass
- [ ] Public interface matches spec exactly (C++, C ABI, low-level Python, and high-level Python)
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
│   ├── __init__.py         # Re-exports from both high-level and low-level APIs
│   ├── _ffi.py             # ctypes bindings (internal)
│   ├── _low_level.py       # Low-level 1:1 wrapper: Squeeze, SqueezeError, set_log_level
│   ├── engine.py           # High-level Engine entry point
│   ├── node.py             # Node, PortRef, ParamMap, Param
│   ├── transport.py        # Transport sub-object
│   ├── midi.py             # Midi, MidiDevice sub-objects
│   └── types.py            # Direction, SignalType enums; Port, ParamDescriptor, Connection dataclasses
└── tests/
    ├── conftest.py          # Shared fixtures (e.g. lib path, engine setup)
    ├── test_engine.py       # Low-level tests mirroring the C FFI acceptance tests
    └── test_highlevel.py    # High-level API tests
```

### Conventions

- **Test framework:** pytest
- **Test location:** `python/tests/`
- **Run tests:** `cd python && pytest`
- **Every `sq_` function** exposed through the C ABI must have a corresponding low-level `Squeeze` method, a high-level wrapper (on `Engine`, `Node`, `Transport`, `Midi`, etc. as appropriate), and Python tests for both layers
- **Low-level Python tests mirror C FFI tests.** If a Catch2 FFI test exercises `sq_foo()`, there should be a pytest case exercising `squeeze.Squeeze.foo()`.
- **High-level Python tests** verify the Pythonic API (`Engine`, `Node`, `>>` operator, `ParamMap`, etc.) in `test_highlevel.py`.
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
2. Add the low-level Python wrapper method to `squeeze/_low_level.py`
3. Add the high-level Python wrapper to the appropriate module (`engine.py`, `node.py`, `transport.py`, `midi.py`, `types.py`)
4. Add low-level Python tests in `python/tests/`
5. Add high-level Python tests in `python/tests/test_highlevel.py`
6. All four (C++ Catch2 tests, C FFI Catch2 tests, low-level Python tests, high-level Python tests) must pass before the tier is complete

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
