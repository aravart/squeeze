# CLAUDE.md — Squeeze v2

Squeeze is a modern, modular JUCE/C++ audio engine for hosting VST/AU plugins and routing audio through programmable graphs with a C ABI for multi-language integration.

v2 is a ground-up rewrite. The v1 codebase lives in `./squeeze/` for reference (architecture, specs, DSP algorithms, test patterns) but no code is carried over directly.

---

## Project Stack

- **Language:** C++17
- **Framework:** JUCE (plugin hosting, audio I/O, DSP, MIDI, GUI)
- **Public API:** C ABI (`squeeze_ffi`) — opaque handles, plain C types, no C++ in the public header
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
3. **Communication** — lock-free SPSC queues bridging control → audio (Scheduler for infrastructure commands, EventQueue for beat-timed musical events)

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
1.  Port                  (no dependencies)
2.  Node                  (Port)
3.  Graph                 (Node, Port)
4.  SPSCQueue             (no dependencies)
5.  Scheduler             (SPSCQueue)
6.  Transport             (no dependencies)
7.  Buffer                (no dependencies)
8.  PerfMonitor           (no dependencies)
9.  Engine                (Graph, Scheduler, Transport, Buffer, PerfMonitor)
10. PluginNode            (Node, JUCE plugin hosting)
11. SamplerVoice          (Buffer)
12. TimeStretchEngine     (signalsmith-stretch)
13. VoiceAllocator        (SamplerVoice)
14. SamplerNode           (Node, VoiceAllocator, TimeStretchEngine, Buffer)
15. RecorderNode          (Node, Buffer)
16. MidiRouter            (SPSCQueue)
17. EventQueue            (SPSCQueue)
18. GroupNode             (Node, Graph — composite subgraph node)
19. ScopeTap / Metering   (SPSCQueue / SeqLock)
20. C ABI (squeeze_ffi)   (Engine, all node types)
```

**Every tier ships a working FFI.** Each tier extends both the C++ engine and the C API surface together. Do not build internal components without their corresponding `sq_` functions.

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

### Step 4: Review

**Review checklist for generated implementation:**

- [ ] All tests pass
- [ ] Public interface matches spec exactly
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
The control thread builds a `GraphSnapshot` (execution order, pre-allocated buffers per node), then atomically swaps it in via the Scheduler. The audio thread never modifies the graph.

### Topology is derived
Users declare data flow between nodes; the engine computes execution order automatically.

### Sub-block parameter splitting
When parameter changes arrive mid-block (from EventQueue), the engine splits processing into sub-blocks at each change point. Nodes are unaware — they just see shorter `numSamples`.

### Error reporting
Internal C++ functions return `bool`/`int` with `std::string& errorMessage` out-params. The C ABI returns error codes (`SqResult`) and provides `sq_error_message()` for the last error string.

### Connections as API
Topology is derived from explicit `connect()` calls. Connections validate: matching signal types, no audio fan-in, no cycles (BFS detection). MIDI allows fan-in.

### Audio leaf summing
Nodes with audio outputs that nothing reads are "audio leaves." Their outputs are summed to the device output.

### GroupNode (composite subgraph)
A `GroupNode` owns an internal `Graph` and implements the `Node` interface. It exports selected internal ports as group-level ports. From the parent graph's perspective, it's just another node. Designed in from the start (v1 lacked this).

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
- **Parameters:** Index-based virtual system on `Node`. `PluginNode` uses `std::unordered_map<std::string, int>` for O(1) name lookup. `SamplerNode` uses flat normalized `float[]` with per-parameter mapping functions.
- **No over-engineering:** No features beyond the spec. No premature abstractions. Three similar lines > one premature helper.

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
