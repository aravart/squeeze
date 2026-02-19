# CLAUDE.md — Squeeze v2

Squeeze is a modern, modular JUCE/C++ audio engine for hosting VST/AU plugins and routing audio through a mixer-centric architecture with a C ABI for multi-language integration.

v2 is a ground-up rewrite. The v1 codebase lives in `./squeeze-v1/` for reference (architecture, specs, DSP algorithms, test patterns) but no code is carried over directly.

---

## Project Stack

- **Language:** C++17
- **Framework:** JUCE (plugin hosting, audio I/O, DSP, MIDI, GUI)
- **Public API:** C ABI (`squeeze_ffi`) — opaque handles, plain C types, no C++ in the public header
- **Python package:** `python/` — proper Python package (`squeeze`) with one public API layer: `Squeeze`, `Source`, `Bus`, `Chain`, `Processor`, etc. Internal ctypes bindings (`_ffi.py`) and helpers (`_helpers.py`) are not public. Maintained alongside `squeeze_ffi.h`. Includes its own unit tests, buildable via `pyproject.toml`.
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

**Every tier ships a working FFI and Python client.** Each tier extends the C++ engine, the C API surface, and the Python package together. Do not build internal components without their corresponding `sq_` functions and Python `Squeeze`/`Source`/`Bus`/`Chain`/`Processor` methods.

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
- **Naming:** Descriptive sentences — `"sq_bus_route rejects routing that would create cycles"`
- **Independence:** Each test creates own fixtures, no shared mutable state
- **Coverage:** All `sq_` functions exercised, all error codes returned, edge cases (0, 1, max, empty, boundary)
- **Engine testing:** `Engine(sr, bs)` is always prepared from construction — use `render()` for headless tests
- **Mocks:** Implement real interfaces, record calls, return configurable values. Keep unit tests with mocks even after integration tests exist.

### Step 3: Implement to Pass Tests

This includes the C++ component, its `sq_` functions in `squeeze_ffi.h`/`.cpp`, and the Python wrappers in `python/squeeze/` (squeeze, source, bus, chain, processor, transport, midi, types as appropriate). All Python code must have complete type annotations (see Type Annotations section).

### Step 4: Review

**Review checklist for generated implementation:**

- [ ] All tests pass (Catch2 + pytest)
- [ ] Public interface matches spec exactly (C++, C ABI, and Python)
- [ ] No functionality beyond what's specified
- [ ] Realtime safety (if applicable): no allocation, no blocking, no unbounded loops
- [ ] Thread safety
- [ ] No obvious performance issues
- [ ] Audit for code duplication, dead code, overly large source code files, and opportunities for refactoring
- [ ] Python type annotations complete — `cd python && mypy squeeze/` passes
- [ ] Consumer docs updated — if the Python API surface changed, update `python/README.md` (API table) and `python/INTEGRATION.md` (method signatures, patterns)

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
- **Parameters:** String-based API on `Processor` — `getParameter(name)`, `setParameter(name, value)`. No index in the public interface (Processor, C ABI, or Python). RT-safe dispatch for sample-accurate automation is an Engine/EventScheduler concern (pre-resolved tokens), not a Processor concern. Subclasses store parameters however they choose internally.
- **No over-engineering:** No features beyond the spec. No premature abstractions. Three similar lines > one premature helper.

### Logging Standards

Every component must include comprehensive logging using the `SQ_*` macros. Logging is not optional — it is part of the implementation, not an afterthought.

**Level guidelines:**

| Level | When to use | Examples |
|-------|-------------|---------|
| `SQ_WARN` / `SQ_WARN_RT` | Something went wrong or was dropped | Xruns, queue overflows, failed operations, dropped events |
| `SQ_INFO` / `SQ_INFO_RT` | High-level operational milestones | Engine start/stop, device open/close, plugin loaded, graph rebuilt |
| `SQ_DEBUG` / `SQ_DEBUG_RT` | Every public API call and state transition | route/send, source/bus add/remove, chain insert, snapshot swap |
| `SQ_TRACE` / `SQ_TRACE_RT` | Per-message/per-event granularity | Individual MIDI messages, per-event scheduling, per-block details |

**Rules:**
- Use `SQ_*_RT` variants on the audio thread, plain `SQ_*` on the control thread. Never `fprintf`/`std::cout` directly.
- Log all public method entry points at `debug` or `info` level with key parameters.
- Log all error paths at `warn` level with enough context to diagnose the problem.
- Format: include relevant IDs, names, and numeric values — `SQ_DEBUG("route: source %d -> bus %d", srcHandle, busHandle)`, not `SQ_DEBUG("routed")`.

---

## Python Package (`python/`)

The `python/` directory is a proper Python package, buildable and eventually publishable. It provides **one public API layer** — no separate low-level/high-level split.

### Structure

```
python/
├── pyproject.toml          # Package metadata, dependencies, build config
├── README.md               # Consumer-facing README (PyPI page, LLM quick-start)
├── INTEGRATION.md          # Compact API reference for LLM consumption
├── squeeze/
│   ├── __init__.py         # Re-exports: Squeeze, Source, Bus, Chain, Processor, etc.
│   ├── py.typed            # PEP 561 marker — signals type annotations are available
│   ├── _ffi.py             # ctypes declarations (internal, mechanical)
│   ├── _helpers.py         # Error checking, string/list conversion (internal)
│   ├── squeeze.py          # Squeeze — the public entry point
│   ├── source.py           # Source object
│   ├── bus.py              # Bus object
│   ├── chain.py            # Chain object
│   ├── processor.py        # Processor object
│   ├── transport.py        # Transport sub-object
│   ├── midi.py             # Midi, MidiDevice sub-objects
│   └── types.py            # ParamDescriptor dataclass
└── tests/
    ├── conftest.py          # Shared fixtures (e.g. lib path, engine setup)
    └── test_squeeze.py      # Python API tests
```

### Conventions

- **Test framework:** pytest
- **Test location:** `python/tests/`
- **Run tests:** `cd python && pytest`
- **Every `sq_` function** exposed through the C ABI must have a corresponding Python method on the appropriate class (`Squeeze`, `Source`, `Bus`, `Chain`, `Processor`, `Transport`, `Midi`) and a pytest case
- **One set of Python tests.** No separate low-level/high-level test files. Each `sq_*` function is tested through the public Python API.
- **`_ffi.py` and `_helpers.py` are internal.** Users import `Squeeze`, `Source`, `Bus`, etc. — never `_ffi` or `_helpers`.

### Type Annotations

All Python code must be fully typed. This is not optional.

- **`py.typed`:** The PEP 561 marker file `python/squeeze/py.typed` must exist. It signals to type checkers and Claude Code that annotations are available.
- **Every public method** must have typed parameters and a `-> ReturnType`. Properties must have `-> Type`.
- **Type style:** Use `str`, `int`, `float`, `bool`, `list[X]`, `dict[K, V]`, `tuple[X, Y]`, `X | None`. Use `from __future__ import annotations` for forward references. Callback types use `typing.Callable[[ArgTypes], ReturnType]`.
- **Validation:** `cd python && mypy squeeze/` must report no errors. Strict mode is configured in `pyproject.toml` (with `warn_return_any = false` for ctypes, and `_ffi.py` excluded). Do not pass `--strict` on the CLI — it overrides the config. Run this as part of the review step.

### Consumer Documentation (`python/README.md`, `python/INTEGRATION.md`)

The `python/` directory contains two consumer-facing documents that other projects (and their Claude Code sessions) use to understand and integrate with Squeeze. These are the external API contract — **doc drift is a bug**, same as spec drift.

- **`python/README.md`** — Package README shown on PyPI and read first by LLMs. Contains: install command, quick-start code, API-at-a-glance table. Must have install + quick-start + class table in the first 50 lines.
- **`python/INTEGRATION.md`** — Compact (~200 line) API reference optimized for LLM consumption. Lists every public class, method signature, and common usage patterns. No internal details (no SPSC, snapshots, FFI internals).

**When to update:** Any change that adds, removes, or modifies a public Python method, class, or parameter must be reflected in both documents. This includes new `sq_` functions that get Python wrappers.

### Build & Install

```bash
cd python
pip install -e .          # Editable install for development
pytest                    # Run Python tests
```

### Development Cycle Integration

The existing rule — **every tier ships a working FFI and Python client** — extends to the Python test suite. When implementing a new component:

1. Add the `sq_` C ABI functions
2. Add the ctypes declaration to `squeeze/_ffi.py`
3. Add the Python wrapper to the appropriate module (`squeeze.py`, `source.py`, `bus.py`, `chain.py`, `processor.py`, `transport.py`, `midi.py`, `types.py`) — **with full type annotations**
4. Add Python tests in `python/tests/`
5. Update `python/INTEGRATION.md` with new class/method signatures
6. Update `python/README.md` API table if a new class was added
7. All three (C++ Catch2 tests, C FFI Catch2 tests, Python tests) must pass before the tier is complete
8. `cd python && mypy squeeze/` must pass

---

## v1 Reference

The `./squeeze-v1/` directory contains the complete v1 implementation. Key reference material:

- `squeeze-v1/ARCHITECTURE.md` — full architectural overview
- `squeeze-v1/docs/specs/` — 25 component specifications
- `squeeze-v1/docs/AUDIT.md` — component audit with gap analysis
- `squeeze-v1/docs/transition_plan.md` — C ABI transition plan
- `squeeze-v1/docs/discussion/` — design rationale documents
- `squeeze-v1/src/core/` — all DSP and engine implementations
- `squeeze-v1/tests/core/` — 589+ test cases as patterns

Consult v1 for domain knowledge (DSP algorithms, parameter ranges, thread safety patterns, edge cases discovered during development) but implement fresh.

---

## Git Workflow

- One component per branch: `feature/{component-name}`
- Commit format: `{component}: {description}`
- PR requires: spec complete, all tests pass, no static analysis warnings
