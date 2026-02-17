# PDC (Plugin Delay Compensation) Specification

## Responsibilities

- Compute per-connection compensation delays at snapshot build time so that parallel audio paths arriving at a merge point are time-aligned
- Apply delays via ring buffers during `processBlock` fan-in summation
- Report per-node latency and total graph latency through the public API
- Toggle PDC on/off (disable for low-latency live monitoring scenarios)
- Detect latency changes during `buildAndSwapSnapshot()` and recompute compensation

## Overview

Plugins report processing latency (look-ahead compressors, linear-phase EQs, oversampled saturators). When multiple audio paths merge at a single node's input, shorter paths must be delayed to match the longest path — otherwise the signals are misaligned and phase cancellation occurs.

PDC is a **cross-cutting feature** that modifies Node (latency reporting), Engine/GraphSnapshot (computation and application of delays), and adds C ABI + Python surface. It is not a standalone component.

### How it works

1. **Latency reporting.** Each Node can report its processing latency via `getLatencySamples()`. Most nodes return 0. PluginNode delegates to `processor_->getLatencySamples()`. GroupNode returns its max internal path latency.

2. **Compensation computation.** During `buildAndSwapSnapshot()`, the Engine walks the topologically sorted execution order, propagating cumulative path latencies forward. At each node with multiple audio fan-in sources, the maximum incoming path latency is determined. Each fan-in connection receives a compensation delay equal to `maxIncoming - thisPathLatency`.

3. **Delay application.** Compensation delays are applied in `processBlock` during the fan-in summation step. Each fan-in connection with a non-zero compensation delay writes source audio into a ring buffer and reads the delayed output, rather than copying directly.

4. **Toggleable.** When PDC is disabled, all compensation delays are set to 0 in the next snapshot. Node latency values are still queryable.

### What PDC does NOT do

- **MIDI compensation.** Only audio paths are delayed. MIDI is not compensated — this matches standard DAW behavior.
- **Fractional-sample delays.** Latency is integer samples only. No interpolation.
- **Preserve delay state across snapshots.** A topology or latency change rebuilds all delay lines from zero. This causes a brief discontinuity — the accepted trade-off in all major DAW hosts.

## Interface

### C++ — Node addition

```cpp
class Node {
public:
    // ... existing interface ...

    /// Report processing latency in samples. Default: 0 (no latency).
    /// Called on the control thread during buildAndSwapSnapshot().
    virtual int getLatencySamples() const { return 0; }
};
```

PluginNode overrides this:

```cpp
class PluginNode : public Node {
public:
    int getLatencySamples() const override {
        return processor_->getLatencySamples();
    }
};
```

GroupNode overrides this to return the maximum internal path latency (computed the same way Engine computes total latency — walk internal execution order, propagate cumulative latencies, return the max path latency to any exported output node).

### C++ — Engine additions

```cpp
class Engine {
public:
    // ... existing interface ...

    // --- PDC (control thread) ---
    void setPDCEnabled(bool enabled);
    bool isPDCEnabled() const;
    int getNodeLatency(int nodeId) const;   // node's own reported latency
    int getTotalLatency() const;            // max path latency to output node

private:
    bool pdcEnabled_ = true;  // enabled by default
};
```

### C++ — GraphSnapshot additions

```cpp
struct GraphSnapshot {
    // ... existing fields ...

    struct FanIn {
        int sourceNodeId;
        std::string sourcePort;
        std::string destPort;
        int compensationDelay;  // NEW: samples of delay, 0 = no compensation
    };

    // NEW: per-fan-in delay ring buffers (indexed parallel to audioFanIn entries)
    // Only allocated for connections where compensationDelay > 0.
    struct DelayLine {
        std::vector<std::vector<float>> buffer;  // [channel][sample]
        int writePos;
        int delaySamples;
        int numChannels;

        void write(const juce::AudioBuffer<float>& src, int numSamples);
        void read(juce::AudioBuffer<float>& dst, int numSamples, bool addTo);
    };

    // Map: destNodeId → vector of DelayLine (parallel to audioFanIn entries)
    // Only entries with compensationDelay > 0 have non-empty DelayLines.
    std::unordered_map<int, std::vector<std::unique_ptr<DelayLine>>> compensationDelays;

    int totalLatency;  // NEW: max path latency to output node
};
```

### C ABI (`squeeze_ffi.h`)

```c
// PDC control
void sq_set_pdc_enabled(SqEngine engine, bool enabled);
bool sq_pdc_enabled(SqEngine engine);

// Latency queries
int  sq_node_latency(SqEngine engine, int node_id);    // node's reported latency
int  sq_total_latency(SqEngine engine);                 // graph total latency
```

### Python — Low-level (`_low_level.py`)

```python
class Squeeze:
    # ... existing methods ...

    def set_pdc_enabled(self, enabled: bool) -> None: ...
    def pdc_enabled(self) -> bool: ...
    def node_latency(self, node_id: int) -> int: ...
    def total_latency(self) -> int: ...
```

### Python — High-level

On `Engine` (`engine.py`):

```python
class Engine:
    @property
    def pdc_enabled(self) -> bool:
        """Whether plugin delay compensation is active."""
        return self._sq.pdc_enabled()

    @pdc_enabled.setter
    def pdc_enabled(self, enabled: bool) -> None:
        """Enable or disable PDC. Takes effect on next snapshot rebuild."""
        self._sq.set_pdc_enabled(enabled)

    @property
    def total_latency(self) -> int:
        """Total graph latency in samples (max path to output node)."""
        return self._sq.total_latency()
```

On `Node` (`node.py`):

```python
class Node:
    @property
    def latency(self) -> int:
        """This node's reported processing latency in samples."""
        return self._sq.node_latency(self._id)
```

## Algorithm

### Latency propagation (runs in `buildAndSwapSnapshot()`)

```
Input:  executionOrder (topologically sorted node IDs)
        audioFanIn map (destNodeId → list of FanIn)
        nodes map (nodeId → Node*)
        pdcEnabled flag

Output: compensationDelay field on each FanIn entry
        totalLatency (max path latency to output node)

1. Initialize pathLatency[nodeId] = 0 for all nodes

2. For each nodeId in executionOrder:
     a. Find max incoming path latency:
        maxIncoming = 0
        For each fanIn in audioFanIn[nodeId]:
            srcPathLatency = pathLatency[fanIn.sourceNodeId]
            if srcPathLatency > maxIncoming:
                maxIncoming = srcPathLatency

     b. Compute compensation delay for each fan-in:
        For each fanIn in audioFanIn[nodeId]:
            srcPathLatency = pathLatency[fanIn.sourceNodeId]
            if pdcEnabled:
                fanIn.compensationDelay = maxIncoming - srcPathLatency
            else:
                fanIn.compensationDelay = 0

     c. Propagate: this node's output path latency includes its own latency
        pathLatency[nodeId] = maxIncoming + nodes[nodeId]->getLatencySamples()

3. totalLatency = pathLatency[outputNodeId]
```

### Worked example

```
Graph: synth → [eq(256) → comp(512)] → output
       synth → output

Nodes and their reported latencies:
  synth:  0 samples
  eq:     256 samples (linear-phase EQ)
  comp:   512 samples (look-ahead compressor)
  output: 0 samples

Execution order (topological): [synth, eq, comp, output]

Step 2, nodeId = synth:
  audioFanIn[synth] = []  (no inputs)
  pathLatency[synth] = 0 + 0 = 0

Step 2, nodeId = eq:
  audioFanIn[eq] = [{source: synth}]
  maxIncoming = pathLatency[synth] = 0
  fanIn[synth→eq].compensationDelay = 0 - 0 = 0
  pathLatency[eq] = 0 + 256 = 256

Step 2, nodeId = comp:
  audioFanIn[comp] = [{source: eq}]
  maxIncoming = pathLatency[eq] = 256
  fanIn[eq→comp].compensationDelay = 256 - 256 = 0
  pathLatency[comp] = 256 + 512 = 768

Step 2, nodeId = output:
  audioFanIn[output] = [{source: comp}, {source: synth}]
  maxIncoming = max(pathLatency[comp], pathLatency[synth])
             = max(768, 0)
             = 768
  fanIn[comp→output].compensationDelay = 768 - 768 = 0
  fanIn[synth→output].compensationDelay = 768 - 0 = 768  ← DELAY APPLIED
  pathLatency[output] = 768 + 0 = 768

Result:
  The direct synth→output connection gets a 768-sample delay ring buffer.
  totalLatency = 768 samples.
```

### processBlock fan-in modification

The existing fan-in loop in `processBlock`:

```
for each fanIn in audioFanIn[nodeId]:
    sum source outputAudio into dest inputAudio
```

Becomes:

```
for each fanIn in audioFanIn[nodeId]:
    if fanIn has a DelayLine (compensationDelay > 0):
        delayLine.write(source outputAudio, numSamples)
        delayLine.read(dest inputAudio, numSamples, addTo=true)
    else:
        sum source outputAudio into dest inputAudio (unchanged)
```

## DelayLine

A minimal RT-safe ring buffer for integer-sample delays. Not a standalone component — it lives inside `GraphSnapshot`.

```cpp
struct DelayLine {
    std::vector<std::vector<float>> buffer;  // [channel][delaySamples]
    int writePos = 0;
    int delaySamples = 0;
    int numChannels = 0;

    // Allocate during snapshot build (control thread)
    void allocate(int channels, int delay);

    // Write numSamples from src into the ring buffer (audio thread, RT-safe)
    void write(const juce::AudioBuffer<float>& src, int numSamples);

    // Read numSamples of delayed output, adding to dst (audio thread, RT-safe)
    void read(juce::AudioBuffer<float>& dst, int numSamples, bool addTo);
};
```

**Implementation notes:**

- Buffer size equals `delaySamples`. Write position wraps modulo `delaySamples`.
- `write()` copies current source audio into the ring buffer at `writePos`, advances `writePos`.
- `read()` reads from `(writePos - delaySamples + readOffset)` wrapped — i.e., the oldest samples in the buffer.
- Since `delaySamples` is always >= `blockSize` in practice (plugin latencies are typically multiples of block size or larger), the read and write regions don't overlap. If `delaySamples < blockSize`, the implementation must handle the wrap correctly (two-part copy).
- Buffer is zero-initialized at allocation. The first `delaySamples` worth of output will be silence — this is the expected behavior (the delay line is "filling up").
- No interpolation needed — delays are integer samples only.
- Approximately 30 lines of code. No JUCE dependency beyond `juce::AudioBuffer` for the read/write interface.

## Invariants

- `getLatencySamples()` returns >= 0 (never negative)
- Compensation delays are >= 0 for every fan-in connection
- The sum `compensationDelay + sourcePathLatency` is equal for all fan-in connections at any given merge node (when PDC is enabled)
- `totalLatency` equals the maximum cumulative path latency to the output node
- Delay ring buffers are pre-allocated at snapshot build time — no allocation on the audio thread
- DelayLine read/write operations are RT-safe (no allocation, no blocking, bounded loops)
- When PDC is disabled, all `compensationDelay` values are 0 (no delay buffers allocated)
- Latency queries (`getNodeLatency`, `getTotalLatency`) reflect the current snapshot's computed values
- A topology change or latency change triggers snapshot rebuild, which recomputes all compensation delays
- Delay line state is not preserved across snapshot swaps — fresh buffers are zero-initialized

## Error Conditions

- `sq_node_latency()` with unknown node ID: returns 0
- `sq_total_latency()` with no snapshot built: returns 0
- `sq_set_pdc_enabled()` when audio is running: takes effect on next `buildAndSwapSnapshot()` (not immediate — no mid-block changes)
- A node reports a negative latency: clamped to 0, logged at warn level

## Does NOT Handle

- **MIDI delay compensation** — only audio fan-in is compensated. MIDI is not delayed.
- **Fractional-sample (sub-sample) delays** — integer samples only, no interpolation
- **Delay state preservation across topology changes** — delay buffers are rebuilt from zero on every snapshot swap
- **Per-node PDC bypass** — PDC is a global toggle, not per-node. Individual nodes can be bypassed through other means.
- **Latency reporting for nodes other than PluginNode/GroupNode** — built-in nodes (GainNode, OutputNode, TestSynthNode) return 0 by default
- **Automatic latency change detection mid-block** — latency is only queried during `buildAndSwapSnapshot()`. If a plugin changes its latency at runtime (rare), the host must trigger a snapshot rebuild.
- **Parallel thread processing** — PDC computation assumes single-threaded execution order. If parallel graph processing is added later, PDC must be extended to account for thread-pool scheduling.

## Dependencies

- **Node** — adds `getLatencySamples()` virtual
- **PluginNode** — overrides to delegate to `processor_->getLatencySamples()`
- **GroupNode** — overrides to compute max internal path latency
- **Engine** — `buildAndSwapSnapshot()` runs the compensation algorithm; `processBlock()` applies delays in fan-in loop
- **GraphSnapshot** — extended `FanIn` struct, `DelayLine` storage, `totalLatency` field
- **Graph** — unchanged (topology and connections are unaffected)
- **JUCE** — `AudioProcessor::getLatencySamples()` for plugin latency; `juce::AudioBuffer<float>` for delay buffer I/O

## Thread Safety

| Method / Operation | Thread | Notes |
|---|---|---|
| `Node::getLatencySamples()` | Control | Called during `buildAndSwapSnapshot()` with `controlMutex_` held |
| `PluginNode::getLatencySamples()` | Control | Delegates to `processor_->getLatencySamples()` (safe — same thread as `prepare()`) |
| `GroupNode::getLatencySamples()` | Control | Walks internal graph (control thread only) |
| `Engine::setPDCEnabled()` | Control | Acquires `controlMutex_`, sets flag, triggers snapshot rebuild |
| `Engine::isPDCEnabled()` | Control | Acquires `controlMutex_`, reads flag |
| `Engine::getNodeLatency()` | Control | Acquires `controlMutex_`, calls `getLatencySamples()` on node |
| `Engine::getTotalLatency()` | Control | Acquires `controlMutex_`, reads from current snapshot |
| Compensation computation | Control | Runs inside `buildAndSwapSnapshot()` |
| DelayLine allocation | Control | Runs inside `buildAndSwapSnapshot()` (heap allocation is fine) |
| `DelayLine::write()` / `read()` | Audio | RT-safe — fixed-size array access, no allocation |
| Fan-in loop with delay | Audio | Reads active snapshot's delay lines |

## Example Usage

### C ABI

```c
SqEngine engine = sq_engine_create(44100.0, 512, &error);

// Add a linear-phase EQ (reports 256 samples latency)
int eq = sq_add_plugin(engine, "Linear Phase EQ", &error);
int output = sq_output_node(engine);
sq_connect(engine, eq, "out", output, "in", &error);

// Check PDC state
printf("PDC enabled: %s\n", sq_pdc_enabled(engine) ? "yes" : "no");
printf("EQ latency: %d samples\n", sq_node_latency(engine, eq));
printf("Total latency: %d samples\n", sq_total_latency(engine));

// Disable for live monitoring
sq_set_pdc_enabled(engine, false);

sq_engine_destroy(engine);
```

### Python (high-level)

```python
from squeeze import Engine

with Engine() as engine:
    engine.load_plugin_cache("plugin-cache.xml")

    eq = engine.add_plugin("Linear Phase EQ")
    comp = engine.add_plugin("Look-Ahead Compressor")

    # Build a chain with parallel dry path
    synth = engine.add_test_synth()
    synth >> eq
    eq >> comp
    comp >> engine.output
    synth >> engine.output  # parallel dry path — PDC will delay this

    print(f"EQ latency: {eq.latency} samples")
    print(f"Comp latency: {comp.latency} samples")
    print(f"Total latency: {engine.total_latency} samples")

    # Disable for live use
    engine.pdc_enabled = False
    print(f"PDC enabled: {engine.pdc_enabled}")
```

### Headless testing (C++)

```cpp
Engine engine(44100.0, 512);

// Create a node that reports 256 samples of latency
auto latencyNode = std::make_unique<TestLatencyNode>(256);
int latId = engine.addNode(std::move(latencyNode), "latency");

auto gain = std::make_unique<GainNode>();
int gainId = engine.addNode(std::move(gain), "dry");

int output = engine.getOutputNodeId();

// Both paths to output
std::string error;
engine.connect(latId, "out", output, "in", error);
engine.connect(gainId, "out", output, "in", error);

// Verify compensation
assert(engine.getTotalLatency() == 256);
assert(engine.getNodeLatency(latId) == 256);
assert(engine.getNodeLatency(gainId) == 0);

// The dry path (gainId→output) should have 256 samples of compensation delay
engine.render(512);
// ... verify output alignment ...
```
