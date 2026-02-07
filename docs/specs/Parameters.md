# Parameters Specification

## Overview

Parameters are named, automatable values on a Node. They are the primary control surface for shaping sound — filter cutoffs, reverb mixes, oscillator pitches, envelope times. This specification defines how parameters are declared, described, accessed, snapshotted, and smoothed across the system.

Parameters are distinct from Ports. Ports carry audio and MIDI signals between nodes at sample rate. Parameters are scalar control values that change less frequently — from Lua scripts, MIDI CC, scene crossfades, or plugin GUIs.

## ParameterDescriptor

Every parameter is described by a descriptor. Descriptors are metadata — they don't hold the current value, they describe the parameter's identity and characteristics.

```cpp
struct ParameterDescriptor {
    std::string name;       // unique within the node, used for lookup
    int index;              // stable position in the node's parameter list
    float defaultValue;     // normalized 0.0–1.0
    int numSteps;           // 0 = continuous, N = discrete (N positions)
    bool automatable;       // safe to modulate at control rate
    bool boolean;           // on/off toggle (numSteps == 2)
    std::string label;      // unit suffix: "dB", "Hz", "%", "ms", ""
    std::string group;      // grouping name: "Filter", "Envelope", "" = ungrouped
};
```

### Design decisions

**Normalized values throughout.** All parameter values in the system are normalized 0.0–1.0. This is what JUCE uses internally, and it makes crossfading trivial — lerp between two normalized values. The node is responsible for mapping normalized to its internal representation (linear, logarithmic, frequency scale, etc.). The system never needs to know the mapping.

**Display text via method, not stored range.** Rather than storing min/max/skew in the descriptor (which varies wildly across plugins and is hard to represent generically), the node provides a `getParameterText(index)` method that returns a human-readable string for the current value. For PluginNode this delegates to JUCE's `getCurrentValueAsText()` which returns strings like `"440 Hz"`, `"-3.2 dB"`, `"Sawtooth"`. For custom nodes the author formats it however they like.

**Index-based fast path.** String-based lookup is fine for Lua and the control thread. But the audio thread and batch operations use integer indices for speed. The descriptor's `index` field is stable for the lifetime of the node — it never changes after construction.

## Node Interface

```cpp
class Node {
public:
    // ... existing lifecycle and port methods ...

    // Parameter discovery
    virtual std::vector<ParameterDescriptor> getParameterDescriptors() const { return {}; }

    // Value access (normalized 0.0–1.0)
    virtual float getParameter(int index) const { return 0.0f; }
    virtual void setParameter(int index, float value) {}

    // Display
    virtual std::string getParameterText(int index) const { return ""; }

    // Convenience: name-based access (delegates through descriptors)
    float getParameterByName(const std::string& name) const;
    bool setParameterByName(const std::string& name, float value);
    int findParameterIndex(const std::string& name) const;
};
```

### Changes from current interface

The current interface uses `getParameter(const std::string& name)` and `setParameter(const std::string& name, float value)` as the primary virtual methods. This spec changes the primary virtual interface to index-based and makes name-based access a non-virtual convenience layer on the base class:

- `getParameterDescriptors()` replaces `getParameterNames()` — returns richer metadata
- `getParameter(int index)` / `setParameter(int index, float value)` replace `getParameter(name)` / `setParameter(name, value)` as the virtual interface
- `getParameterByName()` / `setParameterByName()` are non-virtual, implemented once on Node using `findParameterIndex()` + the index-based virtuals
- `setParameterByIndex()` is folded into the new `setParameter(int index, float value)`
- `getParameterText(int index)` is new

### Name-based convenience implementation

```cpp
// In Node base class (non-virtual, implemented once)
int Node::findParameterIndex(const std::string& name) const {
    auto descs = getParameterDescriptors();
    for (const auto& d : descs)
        if (d.name == name) return d.index;
    return -1;
}

float Node::getParameterByName(const std::string& name) const {
    int idx = findParameterIndex(name);
    return (idx >= 0) ? getParameter(idx) : 0.0f;
}

bool Node::setParameterByName(const std::string& name, float value) {
    int idx = findParameterIndex(name);
    if (idx < 0) return false;
    setParameter(idx, value);
    return true;
}
```

Nodes that need fast name-to-index lookup (like PluginNode with hundreds of parameters) should cache the mapping internally rather than scanning descriptors each time.

## PluginNode Implementation

PluginNode extracts descriptors from JUCE's `AudioProcessorParameter` objects:

```cpp
std::vector<ParameterDescriptor> PluginNode::getParameterDescriptors() const {
    std::vector<ParameterDescriptor> descs;
    auto& params = processor_->getParameters();
    for (int i = 0; i < (int)params.size(); ++i) {
        auto* p = params[i];
        ParameterDescriptor d;
        d.name = p->getName(256).toStdString();
        d.index = i;
        d.defaultValue = p->getDefaultValue();
        d.numSteps = p->isDiscrete() ? p->getNumSteps() : 0;
        d.automatable = p->isAutomatable();
        d.boolean = p->isBoolean();
        d.label = p->getLabel().toStdString();
        // JUCE 7: getGroup() returns the parent group name if parameter
        // was added to an AudioProcessorParameterGroup, empty otherwise
        d.group = "";  // extract from group hierarchy if available
        descs.push_back(d);
    }
    return descs;
}

std::string PluginNode::getParameterText(int index) const {
    auto& params = processor_->getParameters();
    if (index >= 0 && index < (int)params.size())
        return params[index]->getCurrentValueAsText().toStdString();
    return "";
}
```

PluginNode retains its internal `paramNameToIndex_` map for fast name-based lookup. The name-based convenience methods on Node will work, but PluginNode can also override `findParameterIndex()` to use its cached map if performance matters.

## Custom Node Parameters

Non-plugin nodes (GainNode, future SamplerNode, etc.) declare parameters by overriding `getParameterDescriptors()`:

```cpp
class GainNode : public Node {
    float gain_ = 1.0f;

public:
    std::vector<ParameterDescriptor> getParameterDescriptors() const override {
        return {{
            .name = "gain",
            .index = 0,
            .defaultValue = 0.5f,       // normalized: 0.5 maps to gain=1.0
            .numSteps = 0,
            .automatable = true,
            .boolean = false,
            .label = "dB",
            .group = ""
        }};
    }

    float getParameter(int index) const override {
        if (index == 0) return gainToNormalized(gain_);
        return 0.0f;
    }

    void setParameter(int index, float value) override {
        if (index == 0) gain_ = normalizedToGain(value);
    }

    std::string getParameterText(int index) const override {
        if (index == 0) {
            float db = juce::Decibels::gainToDecibels(gain_);
            return std::to_string(db) + " dB";
        }
        return "";
    }
};
```

The node author is responsible for the normalized-to-internal mapping. This keeps the system simple — the rest of the world only sees 0.0–1.0.

## Engine API

Engine delegates parameter calls to nodes, adding node ID lookup.

```cpp
class Engine {
public:
    // Discovery
    std::vector<ParameterDescriptor> getParameterDescriptors(int nodeId) const;

    // Value access (normalized 0.0–1.0)
    float getParameter(int nodeId, int paramIndex) const;
    bool setParameter(int nodeId, int paramIndex, float value);

    // Name-based convenience
    float getParameterByName(int nodeId, const std::string& name) const;
    bool setParameterByName(int nodeId, const std::string& name, float value);

    // Display
    std::string getParameterText(int nodeId, int paramIndex) const;

    // Snapshots
    ParameterSnapshot captureSnapshot() const;
    ParameterSnapshot captureSnapshot(const std::vector<int>& nodeIds) const;
    void restoreSnapshot(const ParameterSnapshot& snapshot);
};
```

### Backward compatibility

The existing `getParameterNames(int nodeId)` can be kept as a convenience that extracts names from descriptors, or removed in favor of `getParameterDescriptors()`. The existing `setParameter(nodeId, name, value)` and `getParameter(nodeId, name)` map directly to the new name-based methods.

## Snapshots

A snapshot is a frozen copy of parameter state — the foundation for scenes, presets, and undo.

```cpp
struct ParameterSnapshot {
    struct Entry {
        int nodeId;
        int paramIndex;
        float value;        // normalized 0.0–1.0
    };
    std::vector<Entry> entries;
};
```

### Capture

`captureSnapshot()` iterates all nodes, all parameters, stores current values. The no-arg version captures everything. The vector version captures only specified nodes.

### Restore

`restoreSnapshot()` iterates entries, calls `setParameter()` for each. Entries referencing deleted nodes or out-of-range indices are silently skipped (graceful degradation — you don't want a snapshot restore to fail because one node was removed).

### Crossfade

Crossfading between two snapshots is **not** an Engine method — it's arithmetic that Lua can do trivially:

```lua
function crossfade(snap_a, snap_b, t)
    for i, entry in ipairs(snap_a) do
        local a = entry.value
        local b = snap_b[i].value
        sq.set_param(entry.node_id, entry.param_index, a + (b - a) * t)
    end
end
```

This keeps the C++ layer simple and gives Lua full control over the interpolation curve, per-parameter weighting, and timing.

## Parameter Change Notifications

When a parameter changes — from Lua, from the plugin GUI, from automation — other parts of the system may need to respond (web UI update, MIDI controller feedback, reactive Lua logic).

### Listener interface (C++)

```cpp
class ParameterListener {
public:
    virtual ~ParameterListener() = default;
    virtual void parameterChanged(int nodeId, int paramIndex, float newValue) = 0;
};
```

Engine maintains a list of listeners. When `setParameter()` is called, listeners are notified synchronously on the control thread.

### Plugin GUI changes

When a plugin's own GUI changes a parameter, JUCE notifies via `AudioProcessorListener::audioProcessorParameterChanged()`. PluginNode implements this and forwards to Engine, which notifies listeners. This notification arrives on the message thread (JUCE's GUI thread), which in Squeeze is the control thread.

### Lua callback

```lua
sq.on_param_change(node_id, function(param_index, value)
    -- react to parameter changes
end)
```

Or for all nodes:

```lua
sq.on_param_change(function(node_id, param_index, value)
    -- react to any parameter change
end)
```

The callback fires on the control thread. It must not be called from the audio thread — if a parameter changes on the audio thread (e.g., from a ramp command), the notification is deferred to the control thread.

## Parameter Smoothing (Ramps)

When a parameter changes abruptly (from a scene crossfade, MIDI CC, or Lua script), it can cause audible artifacts — clicks, zipper noise. Smoothing interpolates the value over a short time.

### Approach

Smoothing lives in the Scheduler/audio-thread layer as a ramp command:

```cpp
struct RampCommand {
    int nodeId;
    int paramIndex;
    float targetValue;      // normalized 0.0–1.0
    int durationSamples;    // ramp length in samples
};
```

The audio thread maintains active ramps. Each process block, it advances active ramps by block size, calling `setParameter(index, interpolatedValue)` at the start of each block. When a ramp completes, it snaps to the target value.

### Resolution

Ramps update once per audio block (not per sample). At 512 samples / 44100 Hz, that's ~86 updates per second — smooth enough for parameter changes, and avoids per-sample overhead.

### Lua API

```lua
sq.ramp_param(node_id, name, target_value, duration_ms)
```

Duration of 0 means instant (no ramp, same as `set_param`). A new ramp on the same parameter cancels the previous one.

### Not every parameter needs smoothing

Discrete parameters (waveform select, boolean toggles) should snap, not ramp. The `numSteps` field in the descriptor tells you: if `numSteps > 0`, don't ramp. The `ramp_param` Lua function should respect this — if the parameter is discrete, it sets instantly regardless of the duration argument.

## Lua API

### Discovery

```lua
-- Get all parameter descriptors for a node
local descs, err = sq.param_info(node_id)
-- Returns: {
--   { name="Cutoff", index=0, default=0.5, steps=0,
--     automatable=true, boolean=false, label="Hz", group="Filter" },
--   { name="Resonance", index=1, ... },
--   ...
-- }

-- Get display text for a parameter's current value
local text, err = sq.param_text(node_id, "Cutoff")
-- Returns: "440 Hz"
```

### Value access

```lua
-- By name (existing, unchanged)
sq.set_param(node_id, "Cutoff", 0.75)
local val = sq.get_param(node_id, "Cutoff")

-- By index (new, for performance-critical paths)
sq.set_param_i(node_id, 0, 0.75)
local val = sq.get_param_i(node_id, 0)
```

### Batch operations

```lua
-- Set multiple parameters at once (single call, reduces overhead)
sq.set_params(node_id, { Cutoff = 0.75, Resonance = 0.3 })
```

### Snapshots

```lua
-- Capture all parameter state
local snap = sq.capture_snapshot()

-- Capture specific nodes
local snap = sq.capture_snapshot({node_id_1, node_id_2})

-- Restore
sq.restore_snapshot(snap)
```

### Ramps

```lua
-- Ramp a parameter over time
sq.ramp_param(node_id, "Cutoff", 1.0, 500)  -- to 1.0 over 500ms

-- Instant set (equivalent to set_param)
sq.ramp_param(node_id, "Cutoff", 1.0, 0)
```

### Change notifications

```lua
-- Per-node callback
sq.on_param_change(node_id, function(param_index, value)
    print("param " .. param_index .. " = " .. value)
end)

-- Global callback (all nodes)
sq.on_param_change(function(node_id, param_index, value)
    -- ...
end)

-- Remove callback
sq.off_param_change(node_id)
sq.off_param_change()  -- removes global
```

## Invariants

- Parameter descriptors are fixed after node construction — same contract as ports
- Parameter indices are stable for the lifetime of the node (0 to N-1, contiguous)
- All parameter values are normalized 0.0–1.0
- `setParameter()` clamps to [0.0, 1.0]
- `getParameter()` always returns a value in [0.0, 1.0]
- `getParameterText()` returns a human-readable string for the current value, never empty for valid indices (at minimum, the numeric value)
- Snapshot entries for nonexistent nodes or out-of-range indices are silently skipped on restore
- Ramps on discrete parameters (numSteps > 0) snap instantly
- Change notifications fire on the control thread, never the audio thread

## Error Conditions

- `getParameter(index)` with out-of-range index returns 0.0f
- `setParameter(index, value)` with out-of-range index is a no-op
- `setParameter(index, value)` with value outside [0.0, 1.0] clamps silently
- `getParameterText(index)` with out-of-range index returns ""
- Engine methods with unknown nodeId return false / 0.0f / empty vector
- `ramp_param` with negative duration treated as 0 (instant)

## Does NOT Handle

- Parameter mapping curves (node-internal concern)
- Per-sample parameter modulation (parameters change per-block at most)
- Parameter persistence / preset save-load (future milestone)
- MIDI learn / CC mapping (built in Lua on top of this API)
- Parameter undo/redo (future milestone)
- Audio-thread parameter reads from other nodes (parameters are node-local)

## Dependencies

- Node (base class)
- Engine (node lookup, snapshot orchestration)
- Scheduler (ramp commands on audio thread)
- JUCE AudioProcessorParameter (PluginNode implementation detail)

## Thread Safety

- `getParameterDescriptors()`: any thread (immutable after construction)
- `getParameter(index)`: control thread (reads current value from node)
- `setParameter(index, value)`: control thread (writes directly to node state)
- `getParameterText(index)`: control thread (may query plugin state)
- Audio-thread parameter access: only via Scheduler commands (ramps) or inside `process()` reading the node's own internal state
- Change notifications: always fired on control thread
- Snapshot capture/restore: control thread only

## Phasing

### Phase 1 — Descriptors and index-based access

- `ParameterDescriptor` struct
- `getParameterDescriptors()` on Node and PluginNode
- Index-based `getParameter(int)` / `setParameter(int, float)` as primary virtual interface
- Name-based convenience on Node base class
- `getParameterText()` on Node and PluginNode
- Engine API updated
- Lua `sq.param_info()`, `sq.param_text()`, `sq.set_param_i()`, `sq.get_param_i()`

### Phase 2 — Snapshots

- `ParameterSnapshot` struct
- Engine `captureSnapshot()` / `restoreSnapshot()`
- Lua `sq.capture_snapshot()` / `sq.restore_snapshot()`

### Phase 3 — Ramps

- `RampCommand` in Scheduler
- Audio-thread ramp interpolation
- Lua `sq.ramp_param()`

### Phase 4 — Change notifications

- `ParameterListener` interface
- PluginNode listens to JUCE parameter changes
- Engine notifies listeners
- Lua `sq.on_param_change()` / `sq.off_param_change()`
