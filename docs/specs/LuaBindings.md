# LuaBindings Specification

## Overview

LuaBindings bridges all C++ engine components to Lua via a flat `sq` table API. It is the last component before milestone 1. All Lua API functions live under a single `sq` table registered into the Lua state.

## Responsibilities

- Own a Graph, PluginCache, and FormatManager instance
- Own all dynamically created nodes (via `std::unique_ptr<Node>`)
- Expose a flat `sq.*` Lua API for plugin discovery, graph manipulation, engine control, and parameter access
- Translate between Lua types and C++ types
- Report errors via Lua return values (nil + error string pattern)

## Interface

```cpp
class LuaBindings {
public:
    LuaBindings(Engine& engine, Scheduler& scheduler);

    bool loadPluginCache(const std::string& xmlPath);
    void bind(sol::state& lua);

    // For testing: add a pre-built node without going through PluginCache
    int addTestNode(std::unique_ptr<Node> node, const std::string& name = "test");

    Graph& getGraph();

private:
    Engine& engine_;
    Scheduler& scheduler_;
    Graph graph_;
    PluginCache cache_;
    juce::AudioPluginFormatManager formatManager_;
    std::unordered_map<int, std::unique_ptr<Node>> ownedNodes_;
    std::unordered_map<int, std::string> nodeNames_;
    int nextNodeId_ = 0;
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

## Error Pattern

All functions that can fail return a tuple:
- On success: `value` (single return)
- On failure: `nil, "error message"` (two returns)

Implemented as `std::tuple<sol::object, sol::object>` where the second element is `sol::lua_nil` on success.

## Invariants

- `bind()` creates the `sq` table with all API functions
- After `bind()`, the `sq` table is accessible from Lua
- Node IDs are stable — removing a node does not change other IDs
- Connection IDs are stable — disconnecting does not change other IDs
- `addTestNode()` returns the graph node ID and stores ownership
- `update()` calls `engine_.updateGraph(graph_)` to push the current graph state
- `start()` calls `engine_.start()` with given or default sample rate and block size
- `stop()` calls `engine_.stop()`
- All owned nodes are destroyed when LuaBindings is destroyed

## Error Conditions

- `add_plugin(name)`: returns nil + error if plugin name not found in cache
- `remove_node(id)`: returns nil + error if node ID not found
- `connect(...)`: returns nil + error if source/dest node not found or Graph rejects connection
- `disconnect(id)`: returns nil + error if connection ID not found
- `set_param(id, name, value)`: returns nil + error if node not found
- `get_param(id, name)`: returns nil + error if node not found
- `params(id)`: returns nil + error if node not found
- `ports(id)`: returns nil + error if node not found
- `plugin_info(name)`: returns nil + error if plugin not found in cache

## Does NOT Handle

- Audio device selection (Engine handles this)
- Plugin scanning (uses pre-existing cache)
- Plugin GUI / editor
- State save/restore
- Undo/redo
- Real-time safety (all Lua API calls are on the control thread)

## Dependencies

- Engine (graph push, start/stop)
- Scheduler (owned by main, passed to Engine)
- Graph (node/connection management)
- PluginCache (plugin lookup)
- PluginNode (plugin instantiation)
- Node (base class for all nodes)
- Port (PortDescriptor, PortAddress, PortDirection, SignalType)
- sol2 (Lua bindings)
- JUCE (AudioPluginFormatManager)

## Thread Safety

- All Lua API methods are called from the control thread (the REPL thread)
- `update()` sends a graph snapshot to the audio thread via the Scheduler's SPSC queue
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

sq.connect(synth, "midi_in", effect, "in")  -- error: wrong ports
sq.connect(synth, "out", effect, "in")      -- correct

sq.set_param(synth, "Gain", 0.8)
sq.update()
sq.start()
```
