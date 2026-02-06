# LuaBindings Specification

## Overview

LuaBindings is a thin Lua-to-C++ translation layer. It registers a flat `sq` table API into a Lua state and delegates all operations to Engine. It owns no graph, node, or plugin state — Engine is the single source of truth.

## Responsibilities

- Register the `sq.*` Lua API table into a Lua state
- Translate between Lua types and C++ types
- Delegate all operations to Engine
- Report errors via Lua return values (nil + error string pattern)
- Provide `addTestNode()` for injecting test nodes without real plugins

## Interface

```cpp
class LuaBindings {
public:
    explicit LuaBindings(Engine& engine);

    void bind(sol::state& lua);

    // For testing: add a pre-built node without going through PluginCache
    int addTestNode(std::unique_ptr<Node> node, const std::string& name = "test");

private:
    Engine& engine_;  // only member state
};
```

## Lua API (the "sq" table)

### Plugin Discovery

```lua
sq.list_plugins()             -- returns {"Pigments", "B-3 V2", ...}
sq.plugin_info(name)          -- returns {name, format, inputs, outputs, instrument} or nil, error
```

### Node Management

```lua
sq.add_plugin(name)           -- returns node_id or nil, error
sq.remove_node(id)            -- returns true or nil, error
sq.nodes()                    -- returns {{id=1, name="Pigments"}, ...}
sq.ports(node_id)             -- returns {inputs={...}, outputs={...}} or nil, error
```

### Connections

```lua
sq.connect(src_id, src_port, dst_id, dst_port)  -- returns conn_id or nil, error
sq.disconnect(conn_id)                           -- returns true or nil, error
sq.connections()                                 -- returns {{id, src, src_port, dst, dst_port}, ...}
```

### Engine Control

```lua
sq.update()                   -- push current graph to engine
sq.start(sr?, bs?)            -- start engine (defaults: 44100, 512)
sq.stop()                     -- stop engine
```

### Parameters

```lua
sq.set_param(node_id, name, value)  -- returns true or nil, error (value normalized 0-1)
sq.get_param(node_id, name)         -- returns float or nil, error
sq.params(node_id)                  -- returns {"Gain", "Mix", ...} or nil, error
```

### MIDI Input

```lua
sq.list_midi_inputs()                -- returns {"Device 1", "Device 2", ...}
sq.add_midi_input(device_name)       -- returns node_id or nil, error
sq.refresh_midi_inputs()             -- returns {added={"New Device"}, removed={"Gone Device"}}
```

## Delegation Pattern

Every Lua API method follows the same pattern:

1. Extract arguments from Lua
2. Call the corresponding Engine method
3. Convert the C++ result back to Lua types
4. Return using the error pattern (value or nil + error string)

No business logic lives in LuaBindings. If a Lua method needs to check a condition, query state, or make a decision, that logic belongs in Engine.

## Error Pattern

All functions that can fail return a tuple:
- On success: `value` (single return)
- On failure: `nil, "error message"` (two returns)

Implemented as `std::tuple<sol::object, sol::object>` where the second element is `sol::lua_nil` on success.

## Invariants

- `bind()` creates the `sq` table with all API functions
- After `bind()`, the `sq` table is accessible from Lua
- `addTestNode()` delegates to `engine_.addNode()` and returns the graph node ID
- LuaBindings holds no mutable state beyond the `Engine&` reference
- All node, graph, and plugin state lives in Engine

## Error Conditions

- `add_plugin(name)`: returns nil + error if Engine reports failure
- `remove_node(id)`: returns nil + error if Engine reports failure
- `connect(...)`: returns nil + error if Engine reports failure
- `disconnect(id)`: returns nil + error if Engine reports failure
- `set_param(id, name, value)`: returns nil + error if Engine reports failure
- `get_param(id, name)`: returns nil + error if node not found in Engine
- `params(id)`: returns nil + error if node not found in Engine
- `ports(id)`: returns nil + error if node not found in Engine
- `plugin_info(name)`: returns nil + error if plugin not found in Engine's cache
- `add_midi_input(name)`: returns nil + error if Engine reports failure

## Does NOT Handle

- Audio device selection (Engine handles this)
- Plugin scanning (uses pre-existing cache)
- Plugin GUI / editor
- State save/restore
- Undo/redo
- Real-time safety (all Lua API calls are on the control thread)
- Graph, node, or plugin ownership (Engine handles all of this)

## Dependencies

- Engine (all operations delegate here)
- Node (type used in `addTestNode`)
- Port (PortDescriptor, SignalType for `ports()` formatting)
- sol2 (Lua bindings)

## Thread Safety

- All Lua API methods are called from the control thread (the REPL thread)
- `update()` triggers Engine to send a graph snapshot to the audio thread via the Scheduler's SPSC queue
- No Lua API method is safe to call from the audio thread

## Example Usage

```lua
-- List available plugins
for _, name in ipairs(sq.list_plugins()) do
    print(name)
end

-- Create a synth and connect it
local synth = sq.add_plugin("Pigments")
local effect = sq.add_plugin("Chorus DIMENSION-D")

sq.connect(synth, "out", effect, "in")

sq.set_param(synth, "Gain", 0.8)
sq.update()
sq.start()

-- Check for new/removed MIDI devices
local changes = sq.refresh_midi_inputs()
for _, name in ipairs(changes.added) do
    print("New MIDI device: " .. name)
end
```
