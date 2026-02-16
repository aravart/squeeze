# PluginManager Specification

## Responsibilities

- Own `juce::AudioPluginFormatManager` (with `addDefaultFormats()`)
- Load plugin descriptions from a JUCE `KnownPluginList` XML file (plugin-cache.xml)
- Look up plugins by name (case-sensitive, exact match)
- Instantiate plugins as `PluginNode` (returns `std::unique_ptr<Node>`)
- List available plugin names

## Overview

PluginManager handles plugin cache loading and plugin instantiation. It replaces v1's `PluginCache` class with a single component that both reads the cache and creates plugin instances. It has no Engine dependency — it returns `std::unique_ptr<Node>` instances that the FFI layer passes to Engine. Plugin scanning is out of scope; only pre-built XML caches are supported.

## Interface

### C++ (`squeeze::PluginManager`)

```cpp
namespace squeeze {

class PluginManager {
public:
    PluginManager();
    ~PluginManager();

    // Non-copyable, non-movable
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    // --- Cache loading ---
    bool loadCache(const std::string& xmlPath, std::string& error);
    bool loadCacheFromString(const std::string& xmlString, std::string& error);

    // --- Lookup ---
    const juce::PluginDescription* findByName(const std::string& name) const;
    std::vector<std::string> getAvailablePlugins() const;
    int getNumPlugins() const;

    // --- Instantiation ---
    std::unique_ptr<Node> createNode(const std::string& name,
                                     double sampleRate, int blockSize,
                                     std::string& error);

private:
    juce::AudioPluginFormatManager formatManager_;
    std::vector<juce::PluginDescription> descriptions_;
};

} // namespace squeeze
```

### C ABI (`squeeze_ffi.h`)

```c
// Plugin cache
bool sq_load_plugin_cache(SqEngine engine, const char* path, char** error);

// Plugin instantiation (FFI orchestration: PluginManager creates, Engine owns)
int sq_add_plugin(SqEngine engine, const char* name, char** error);

// Plugin listing
SqStringList sq_available_plugins(SqEngine engine);
int sq_num_plugins(SqEngine engine);
```

### Python API

```python
engine.load_plugin_cache("/path/to/plugin-cache.xml")
synth_id = engine.add_plugin("Diva")
plugins = engine.available_plugins    # property, list of strings
```

### FFI Orchestration

`sq_add_plugin` is not a method on Engine or PluginManager alone — it is orchestrated by the FFI layer:

```cpp
int sq_add_plugin(SqEngine handle, const char* name, char** error) {
    auto node = handle->pluginManager.createNode(
        name, handle->audioDevice.getSampleRate(),
        handle->audioDevice.getBlockSize(), errorStr);
    if (!node) { /* set error, return -1 */ }
    return handle->engine.addNode(std::move(node), name);
}
```

## Invariants

- `getNumPlugins()` returns 0 before any cache is loaded
- After a successful `loadCache()`, `getNumPlugins() > 0`
- `loadCache()` / `loadCacheFromString()` can be called multiple times — each call replaces the previous list entirely
- `findByName()` returns a stable pointer valid until the next `loadCache()` call
- `getAvailablePlugins()` returns names sorted alphabetically
- `createNode()` for a name not in the cache returns nullptr and sets error
- `createNode()` blocks (loads .vst3/.component from disk) — control thread only

## Error Conditions

- `loadCache()` with nonexistent file: returns false, sets error, list is empty
- `loadCache()` with malformed XML: returns false, sets error, list is empty
- `loadCacheFromString()` with invalid XML: returns false, sets error, list is empty
- `findByName()` with unknown name: returns nullptr
- `createNode()` with unknown name: returns nullptr, sets error
- `createNode()` with plugin that fails to load (corrupt binary, missing dependency): returns nullptr, sets error
- `createNode()` with sampleRate 0 or blockSize 0: returns nullptr, sets error

## Does NOT Handle

- **Plugin scanning / discovery** — future (only loads pre-built XML caches)
- **Plugin state / preset management** — PluginNode concern
- **Node ownership** — Engine owns nodes after `addNode()`
- **Plugin GUI** — PluginNode concern
- **Plugin parameter access** — Engine's generic parameter system via Node interface
- **Sample rate / block size changes after creation** — future (Engine would need to re-prepare nodes)

## Dependencies

- Node (the interface returned by `createNode()`)
- PluginNode (concrete type created by `createNode()`)
- JUCE (`juce_audio_processors`: AudioPluginFormatManager, KnownPluginList, PluginDescription)
- JUCE (`juce_audio_formats`: for plugin loading)

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| `loadCache()` / `loadCacheFromString()` | Control | Replaces internal list — not concurrent with other calls |
| `findByName()` | Control | Read-only, safe concurrent with other reads |
| `getAvailablePlugins()` / `getNumPlugins()` | Control | Read-only |
| `createNode()` | Control | Blocks (disk I/O) — never call from audio thread |

All PluginManager methods are called from the control thread. The FFI layer serializes access through `controlMutex_`.

## Example Usage

### C ABI

```c
char* error = NULL;
SqEngine engine = sq_engine_create(&error);

// Load plugin cache
if (!sq_load_plugin_cache(engine, "/path/to/plugin-cache.xml", &error)) {
    fprintf(stderr, "Cache load failed: %s\n", error);
    sq_free_string(error);
}

// List available plugins
SqStringList plugins = sq_available_plugins(engine);
for (int i = 0; i < plugins.count; i++) {
    printf("Plugin: %s\n", plugins.items[i]);
}
sq_free_string_list(plugins);

// Create a plugin node (FFI orchestrates PluginManager + Engine)
int synth = sq_add_plugin(engine, "Diva", &error);
if (synth < 0) {
    fprintf(stderr, "Plugin load failed: %s\n", error);
    sq_free_string(error);
}

// Connect to output
int output = sq_output_node(engine);
sq_connect(engine, synth, "out", output, "in", &error);

sq_engine_destroy(engine);
```

### Python

```python
from squeeze import Engine

engine = Engine()
engine.load_plugin_cache("/path/to/plugin-cache.xml")

print(f"Available: {engine.available_plugins}")

synth = engine.add_plugin("Diva")
engine.connect(synth, "out", engine.output, "in")

engine.close()
```
