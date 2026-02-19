# Modulation Specification

## Overview

Parameter modulation using dedicated modulation sources routed to processor parameters. ModSources are Engine-level objects that produce control-rate signals. Their outputs are routed to processor parameters via ModConnections with per-route depth control. The Engine applies modulation during processBlock using sub-block splitting — processing modulated sources/buses in small chunks and updating parameters between chunks.

This is a single tier (25, Phase 6) with two deliverables: the modulation infrastructure (ModSource base, ModConnection, Engine integration) and the first concrete source (LfoSource).

---

## Responsibilities

- Provide a `ModSource` base class for modulation signal generators
- Provide an `LfoSource` implementation (sine, triangle, saw up, saw down, square, random S&H)
- Route ModSource outputs to Processor parameters with configurable depth
- Apply modulation during processBlock via sub-block splitting
- Support fan-in (multiple sources → one parameter) and fan-out (one source → many parameters)
- Track base values for modulated parameters separately from modulated values
- Support beat-synced LFO rates via Transport integration

---

## Data Structures

### ModSource (abstract base)

```cpp
class ModSource {
public:
    explicit ModSource(const std::string& name);
    virtual ~ModSource() = default;

    ModSource(const ModSource&) = delete;
    ModSource& operator=(const ModSource&) = delete;

    // --- Lifecycle (control thread) ---
    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void reset() {}

    // --- Processing (audio thread) ---
    // Fills the internal output buffer with numSamples values.
    // Transport state provided for beat-synced modes.
    virtual void process(int numSamples, double tempo,
                         double blockStartBeats) = 0;

    // Access the output buffer (audio thread, valid after process())
    const float* getOutput() const { return outputBuffer_.data(); }

    // --- Parameters (string-based, same pattern as Processor) ---
    virtual int getParameterCount() const { return 0; }
    virtual std::vector<ParamDescriptor> getParameterDescriptors() const { return {}; }
    virtual float getParameter(const std::string& name) const { return 0.0f; }
    virtual void setParameter(const std::string& name, float value) {}

    // --- Identity ---
    const std::string& getName() const { return name_; }
    int getHandle() const { return handle_; }
    void setHandle(int h) { handle_ = h; }

protected:
    std::vector<float> outputBuffer_;  // sized in prepare()

private:
    std::string name_;
    int handle_ = -1;
};
```

ModSource shares the parameter pattern with Processor (string-based get/set, ParamDescriptor) but is **not** a Processor subclass. It produces a float buffer, not in-place audio. Handles are allocated from the same counter as Processors to avoid collisions in the FFI.

### LfoSource : ModSource

The first concrete ModSource. See `docs/specs/LfoSource.md` for full specification including waveform definitions, beat-sync behavior, parameter table, and edge cases.

Summary: six waveforms (sine, triangle, saw up, saw down, square, random S&H), free-running (Hz) or beat-synced (division) rate modes, bipolar output (-1.0 to +1.0), phase offset.

### ModConnection

```cpp
struct ModConnection {
    int id;
    int sourceHandle;           // ModSource handle
    int destProcHandle;         // target Processor handle
    std::string destParamName;  // target parameter name
    std::atomic<float> depth{0.0f};  // scaling factor, typically -1.0 to 1.0

    // Non-copyable (atomic member)
    ModConnection(const ModConnection&) = delete;
    ModConnection& operator=(const ModConnection&) = delete;
};
```

Owned by Engine as `std::unordered_map<int, std::unique_ptr<ModConnection>>`. The `depth` field is `std::atomic<float>` — writable from the control thread, readable from the audio thread without snapshot rebuild.

**ModRouteInfo** — plain data struct for query results:

```cpp
struct ModRouteInfo {
    int id;
    int sourceHandle;
    int destProcHandle;
    std::string destParamName;
    float depth;   // snapshot of current value
};
```

### Base Value Storage

Engine maintains base values for all actively modulated parameters:

```cpp
struct ModTarget {
    int procHandle;
    std::string paramName;
    std::atomic<float> baseValue;  // control thread writes, audio thread reads
};
```

Owned by Engine. A ModTarget is created when the first ModConnection targets a `(procHandle, paramName)` pair, and removed when the last ModConnection to that pair is unrouted.

When a ModConnection is created, Engine snapshots the current parameter value as the initial base value.

### ParamDescriptor Extension

```cpp
struct ParamDescriptor {
    // ... existing fields ...
    bool modulatable = false;  // true if this parameter accepts modulation
};
```

Default derivation: `modulatable = true` when `numSteps == 0 && !boolean` (continuous parameters). Processor subclasses may override per-parameter. PluginProcessor sets `modulatable = true` for all continuous plugin parameters, `false` for boolean and discrete.

---

## Interface

### Engine Additions

```cpp
// --- Mod source management (control thread) ---
ModSource* addLfo(const std::string& name);
bool removeModSource(ModSource* src);
ModSource* getModSource(int handle) const;
std::vector<ModSource*> getModSources() const;
int getModSourceCount() const;

// --- Mod routing (control thread) ---
int modRoute(int modSourceHandle, int destProcHandle,
             const std::string& paramName, float depth,
             std::string& errorMessage);
bool modUnroute(int routeId);
bool setModDepth(int routeId, float depth);
float getModDepth(int routeId) const;
std::vector<ModRouteInfo> getModRoutes() const;
```

**`addLfo()`** creates an LfoSource, assigns a handle from the shared handle counter, calls `prepare()`, and triggers a snapshot rebuild.

**`modRoute()`** validates inputs, creates a ModConnection (and a ModTarget if needed), snapshots the base value, and triggers a snapshot rebuild. Returns route ID on success, -1 on failure.

**`setModDepth()`** writes directly to the `ModConnection::depth` atomic. No snapshot rebuild.

**`removeModSource()`** removes all ModConnections referencing the source, then removes the source. Deferred deletion: rebuild snapshot without the source, queue old snapshot and source for GC after the audio thread adopts the new snapshot.

**Base value interception:** When a mod route exists for `(procHandle, paramName)`, Engine's `setParameter()` updates the ModTarget's `baseValue` atomic (and still calls through to the processor). Engine's `getParameter()` returns the base value from ModTarget storage when one exists, so the caller always sees the unmodulated value.

### C ABI

```c
// Opaque handle
typedef struct SqModSourceImpl* SqModSource;

// Mod sources
SqModSource sq_add_lfo(SqEngine engine, const char* name);
SqResult    sq_remove_mod_source(SqEngine engine, SqModSource src);
int         sq_mod_source_count(SqEngine engine);
float       sq_mod_source_get_param(SqEngine engine, SqModSource src,
                                     const char* name);
SqResult    sq_mod_source_set_param(SqEngine engine, SqModSource src,
                                     const char* name, float value);
SqParamList sq_mod_source_param_descriptors(SqEngine engine, SqModSource src);

// Mod routing
int      sq_mod_route(SqEngine engine, SqModSource src, SqProc dest,
                      const char* paramName, float depth);
SqResult sq_mod_unroute(SqEngine engine, int routeId);
SqResult sq_set_mod_depth(SqEngine engine, int routeId, float depth);
float    sq_get_mod_depth(SqEngine engine, int routeId);
```

### Python API

New module: `python/squeeze/mod.py`

```python
class ModSource:
    """A modulation source (LFO, envelope, etc.)."""

    def get_param(self, name: str) -> float: ...
    def set_param(self, name: str, value: float) -> None: ...
    def param_descriptors(self) -> list[ParamDescriptor]: ...

    @property
    def handle(self) -> int: ...
    @property
    def name(self) -> str: ...
```

Methods on `Squeeze`:

```python
def add_lfo(self, name: str) -> ModSource: ...
def remove_mod_source(self, source: ModSource) -> None: ...
def mod_sources(self) -> list[ModSource]: ...
def mod_source_count(self) -> int: ...

def mod_route(self, source: ModSource, dest: Processor,
              param_name: str, depth: float = 1.0) -> int: ...
def mod_unroute(self, route_id: int) -> None: ...
def set_mod_depth(self, route_id: int, depth: float) -> None: ...
def get_mod_depth(self, route_id: int) -> float: ...
def mod_routes(self) -> list[dict]: ...
```

---

## Sub-block Dispatch

### Motivation

Modulation needs to update parameters many times per audio block. Calling `processBlock(1)` per sample is impractical (kills vectorization, breaks FFT-based plugins, violates minimum block size assumptions). Instead, the block is split into fixed-size sub-blocks. At 16 samples / 48 kHz, the update rate is 3 kHz — faster than hardware modulation rates (Elektron-class LFOs run at estimated 500–3000 Hz) and smooth enough for any practical LFO.

### Sub-block Size

```cpp
static constexpr int kModSubBlockSize = 16;
```

Fixed at compile time. 16 samples at 48 kHz = 3 kHz update rate. Large enough for FFT-based plugins and vectorization. Small enough for perceptually smooth modulation.

### processBlock Integration

Modulation dispatch integrates into Engine::processBlock between event resolution and source processing:

```
1. commandQueue_.processPending()
2. midiRouter_.dispatch()
3. transport_.advance()
4. eventScheduler_.retrieve()
5. Dispatch resolved events (MIDI + paramChange)

6. NEW — Process all mod sources:
   for each modSource in snapshot:
       modSource->process(numSamples, tempo, blockStartBeats)

7. For each source:
   if source has active mod targets → processSourceSubBlocked()
   else → processSourceFullBlock()

8. For each bus in dependency order:
   if bus has active mod targets → processBusSubBlocked()
   else → processBusFullBlock()

9. Copy master → output
```

### Sub-block Processing (source)

```
processSourceSubBlocked(srcEntry, numSamples):

    for offset = 0; offset < numSamples; offset += kModSubBlockSize:
        subSize = min(kModSubBlockSize, numSamples - offset)

        // Update modulated parameters from mod source buffers
        for each modTarget on this source's processors:
            float base = modTarget.baseValue->load(relaxed)
            float sum = 0
            for each route in modTarget.routes:
                float d = route.depth->load(relaxed)
                sum += route.sourceBuffer[offset] * d
            float final = clamp(base + sum, 0.0f, 1.0f)
            modTarget.proc->setParameter(modTarget.paramName, final)

        // Create sub-buffer view (zero-copy, aliasing existing memory)
        float* channels[maxCh]
        for ch in 0..numChannels-1:
            channels[ch] = buffer.getWritePointer(ch) + offset
        AudioBuffer<float> subBuf(channels, numChannels, subSize)

        // Slice MIDI to sub-block range
        MidiBuffer subMidi = sliceMidiBuffer(srcEntry.midiBuffer, offset, subSize)

        // Process: generator + chain
        if srcEntry.generator:
            srcEntry.generator->process(subBuf, subMidi)
        for each proc in srcEntry.chainProcessors:
            if !proc->isBypassed():
                proc->process(subBuf)

    // Sends, gain/pan, bus accumulation use the full buffer (unchanged)
```

Bus sub-block processing follows the same pattern (chain processors only, no generator/MIDI).

### Mod Source Output Sampling

The mod source produces a buffer at audio rate (one value per sample). For sub-block dispatch, the value at the sub-block start offset is used (`sourceBuffer[offset]`). No interpolation between sub-block boundaries — the step size is small enough that interpolation is unnecessary.

---

## Combine Semantics

Additive. All modulation contributions are summed:

```
final = clamp(base + sum(source_i[offset] * depth_i), 0.0, 1.0)
```

- `base`: the value set via `sq_set_param()` (normalized 0.0–1.0)
- `source_i[offset]`: the i-th mod source's output at the current sub-block offset
- `depth_i`: the i-th route's depth value
- Clamped to [0.0, 1.0] — the universal parameter range in Squeeze v2

**Mod source conventions** (not enforced, but expected):
- Bipolar sources (LFOs): output -1.0 to +1.0

**Example:** Base = 0.5, bipolar sine LFO with depth = 0.3 sweeps 0.2 to 0.8.

**Clamping note:** Additive semantics with clamping mean that extreme base + depth combinations saturate. Base = 0.9, depth = 0.3 → range 0.6 to 1.0 (not 1.2). Users set the base value to accommodate their modulation range. This is standard modular synth behavior.

---

## MixerSnapshot Changes

```cpp
struct MixerSnapshot {
    // ... existing SourceEntry, BusEntry fields ...

    struct ModSourceEntry {
        ModSource* source;
    };

    struct ModRouteRef {
        const float* sourceBuffer;         // points into ModSource output
        const std::atomic<float>* depth;   // points into ModConnection's depth
    };

    struct ModTargetEntry {
        Processor* proc;
        std::string paramName;
        const std::atomic<float>* baseValue;  // points into Engine's ModTarget
        std::vector<ModRouteRef> routes;       // fan-in: multiple sources
    };

    std::vector<ModSourceEntry> modSources;

    // Per-source and per-bus mod target lists.
    // Outer index aligns with sources/buses vectors.
    std::vector<std::vector<ModTargetEntry>> sourceModTargets;
    std::vector<std::vector<ModTargetEntry>> busModTargets;
};
```

`buildAndSwapSnapshot()` compiles ModConnections into per-source and per-bus `ModTargetEntry` vectors. For each ModConnection, it determines which source or bus the destination processor belongs to, and adds a `ModRouteRef` to the corresponding `ModTargetEntry`. Fan-in routes (multiple sources targeting the same parameter) are grouped into a single `ModTargetEntry` with multiple `ModRouteRef` entries.

**Pointer lifetime:** `sourceBuffer` points into `ModSource::outputBuffer_`, which is pre-allocated in `prepare()` and stable across blocks. `depth` and `baseValue` point into persistent `ModConnection` and `ModTarget` objects owned by Engine. The snapshot does not own these — deferred deletion ensures they outlive the snapshot.

---

## Deferred Deletion

Same pattern as Sources/Buses/Processors:

**Removing a ModSource:**
1. Remove all ModConnections referencing this source (and their ModTargets if no other routes remain)
2. Rebuild snapshot without the source or its routes
3. Audio thread adopts new snapshot
4. Old snapshot queued for GC
5. After old snapshot is collected, queue the ModSource and ModConnections for destruction

**Removing a Processor that has mod routes:**
1. Remove all ModConnections targeting this processor
2. Rebuild snapshot
3. Deferred deletion as above

**Removing a ModConnection (`modUnroute`):**
1. If this was the last route to a ModTarget, remove the ModTarget
2. Rebuild snapshot
3. Deferred deletion of the ModConnection after old snapshot is collected

---

## Invariants

1. Mod sources are always processed before any source or bus that consumes their output
2. Modulated parameters receive updated values before each sub-block's `process()` call
3. `sq_get_param()` returns the base value for modulated parameters, not the modulated value
4. `sq_set_param()` on a modulated parameter updates the base value; modulation is recomputed from the new base on the next audio block
5. Multiple mod sources targeting the same parameter are summed (commutative, order-independent)
6. Final modulated values are clamped to [0.0, 1.0]
7. No allocation on the audio thread — mod source output buffers and snapshot mod targets are pre-allocated
8. Removing a mod source removes all its mod connections
9. Removing a processor removes all mod connections targeting it
10. A mod source can simultaneously feed multiple mod connections (fan-out)
11. `setModDepth()` takes effect on the next audio block without a snapshot rebuild
12. When all mod routes to a parameter are removed, the parameter retains its base value (restored by Engine)

---

## Error Conditions

| Condition | Behavior |
|-----------|----------|
| Mod source handle not found (modRoute) | Returns -1, error message set |
| Dest processor handle not found | Returns -1, error message |
| Dest parameter name not found on processor | Returns -1, error message |
| Dest parameter not modulatable | Returns -1, error message |
| Duplicate route (same source, same proc, same param) | Returns -1, error message |
| Route ID not found (modUnroute/setModDepth) | Returns false |
| ModSource handle not found (removeModSource) | Returns false |

---

## Thread Safety

| Operation | Thread | Notes |
|-----------|--------|-------|
| `addLfo / removeModSource` | Control | Acquires controlMutex_, rebuilds snapshot |
| `modRoute / modUnroute` | Control | Acquires controlMutex_, rebuilds snapshot |
| `setModDepth` | Control | Writes `depth` atomic — no snapshot rebuild |
| `getModDepth` | Control | Reads `depth` atomic |
| `setParameter (modulated)` | Control | Updates `baseValue` atomic + calls through to processor |
| `getParameter (modulated)` | Control | Returns `baseValue` from ModTarget |
| `ModSource::process()` | Audio | Called by Engine at start of processBlock |
| Read mod source output buffer | Audio | Stable after `process()` returns |
| Read `depth` / `baseValue` atomics | Audio | `load(relaxed)` — no ordering needed |
| `proc->setParameter()` in sub-block loop | Audio | Sequential within block |
| `ModSource::setParameter()` | Control | Under controlMutex_ |

**Relaxed ordering rationale:** `depth` and `baseValue` are independent values with no ordering dependencies on other state. A one-block delay between write and read is acceptable.

---

## Does NOT Handle

- **Modulation source types beyond LFO:** Envelope follower, step sequencer, random walk, automation curves — separate components
- **True per-sample modulation for built-in processors:** Phase 2 — add `readParam(name, sampleIndex)` to Processor with per-sample mod buffers
- **Multiply combine mode:** Phase 2 — `final = base * (1 + sum(signal * depth))`
- **Per-sample depth modulation:** Depth is constant per block (single atomic read)
- **Modulation of non-parameter targets:** Tempo, source gain/pan, send levels, routing
- **ModSource-to-ModSource modulation:** No chaining mod sources in Phase 1
- **Modulation UI / visualization:** Application layer concern
- **Parameter smoothing:** Processors handle their own internal smoothing if desired
- **Configurable sub-block size at runtime:** Compile-time constant in Phase 1

---

## Dependencies

- `Processor` (target of modulation, `ParamDescriptor` extended with `modulatable`)
- `Engine` (owns ModSources, ModConnections, ModTargets; processBlock integration)
- `MixerSnapshot` (extended with mod source entries, mod target entries)
- `Transport` (tempo and beat position for beat-synced LFO)
- No new external dependencies

---

## Example Usage

### C++

```cpp
Engine engine(48000, 512);
auto* src = engine.addSource("synth", std::make_unique<MySynth>());
auto* gen = src->getGenerator();

// Create an LFO
auto* lfo = engine.addLfo("tremolo");
lfo->setParameter("shape", 0);    // sine
lfo->setParameter("rate", 4.0);   // 4 Hz

// Route LFO to synth volume with depth 0.3
std::string err;
int routeId = engine.modRoute(lfo->getHandle(), gen->getHandle(),
                               "volume", 0.3f, err);

// Depth change — no snapshot rebuild, takes effect next block
engine.setModDepth(routeId, 0.5f);

// Base value change — modulation recomputed from new base
engine.setParameter(gen->getHandle(), "volume", 0.7f);

// Remove route — parameter restored to base value
engine.modUnroute(routeId);
```

### Python

```python
sq = Squeeze(48000, 512)
src = sq.add_source("synth", plugin_path)

lfo = sq.add_lfo("tremolo")
lfo.set_param("shape", 0)    # sine
lfo.set_param("rate", 4.0)   # 4 Hz

route_id = sq.mod_route(lfo, src.generator, "volume", depth=0.3)
sq.set_mod_depth(route_id, 0.5)
sq.mod_unroute(route_id)
```

### C ABI

```c
SqEngine engine = sq_create_engine(48000, 512);
SqSource src = sq_add_source(engine, "synth", proc);

SqModSource lfo = sq_add_lfo(engine, "tremolo");
sq_mod_source_set_param(engine, lfo, "shape", 0);
sq_mod_source_set_param(engine, lfo, "rate", 4.0f);

int route = sq_mod_route(engine, lfo, proc, "volume", 0.3f);
sq_set_mod_depth(engine, route, 0.5f);
sq_mod_unroute(engine, route);
```

---

## Phase 2 Candidates

- **Per-sample mod buffers for built-in processors:** Add `readParam(name, sampleIndex)` to Processor for true audio-rate modulation without sub-block overhead
- **Multiply combine mode:** `final = base * (1 + sum(signal * depth))` — scales around base value instead of offset
- **Per-processor sub-block optimization:** Only sub-block the specific modulated processor within a chain, not the entire source/bus pipeline
- **Runtime-configurable sub-block size:** `sq_set_mod_resolution(engine, samples)` for user control of update rate vs. CPU tradeoff
- **Additional ModSource types:** Envelope follower, step sequencer, random walk, sample-and-hold with slew, automation curve reader
- **ModSource chaining:** One mod source modulating another's rate or depth
- **Macro controls:** One parameter driving multiple mod routes with different depths
- **PluginProcessor breakpoint extraction:** Scan mod buffer for inflection points, deliver as JUCE `IParameterChanges` for sub-block accuracy within the plugin
