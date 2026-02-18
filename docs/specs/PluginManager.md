# PluginManager Specification

## Responsibilities

- Own `juce::AudioPluginFormatManager` (with `addDefaultFormats()`)
- Load plugin descriptions from a JUCE `KnownPluginList` XML file (plugin-cache.xml)
- Look up plugins by name (case-sensitive, exact match)
- Instantiate plugins as `PluginProcessor` (returns `std::unique_ptr<Processor>`)
- List available plugin names

## Overview

PluginManager handles plugin cache loading and plugin instantiation. It has no Engine dependency — it returns `std::unique_ptr<Processor>` instances that the FFI layer passes to Engine (as Source generators or Chain inserts). Plugin scanning is out of scope; only pre-built XML caches are supported.

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
    std::unique_ptr<Processor> createProcessor(const std::string& name,
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

// Plugin listing
SqStringList sq_available_plugins(SqEngine engine);
int sq_num_plugins(SqEngine engine);
```

Plugin instantiation is embedded in source/bus chain operations:
- `sq_add_source_plugin(engine, name, plugin_path, &error)` — creates a PluginProcessor as the source generator
- `sq_source_append(engine, src, plugin_path)` — creates a PluginProcessor and appends to the source chain
- `sq_bus_append(engine, bus, plugin_path)` — creates a PluginProcessor and appends to the bus chain

### Python API

```python
engine.load_plugin_cache("/path/to/plugin-cache.xml")
plugins = engine.available_plugins    # list of strings

synth = engine.add_source("Lead", plugin="Diva.vst3")
eq = vocal.chain.append("EQ.vst3")
```

### FFI Orchestration

Plugin creation is orchestrated at the FFI level:

```cpp
SqSource sq_add_source_plugin(SqEngine handle, const char* name,
                               const char* plugin_path, char** error) {
    auto proc = handle->pluginManager.createProcessor(
        plugin_path, handle->engine.getSampleRate(),
        handle->engine.getBlockSize(), errorStr);
    if (!proc) { /* set error, return NULL */ }
    return handle->engine.addSource(name, std::move(proc));
}

SqProc sq_source_append(SqEngine handle, SqSource src,
                         const char* plugin_path) {
    auto proc = handle->pluginManager.createProcessor(
        plugin_path, handle->engine.getSampleRate(),
        handle->engine.getBlockSize(), errorStr);
    if (!proc) { /* return NULL */ }
    return handle->engine.sourceAppend(src, std::move(proc));
}
```

## Invariants

- `getNumPlugins()` returns 0 before any cache is loaded
- After a successful `loadCache()`, `getNumPlugins() > 0`
- `loadCache()` / `loadCacheFromString()` can be called multiple times — each call replaces the previous list entirely
- `findByName()` returns a stable pointer valid until the next `loadCache()` call
- `getAvailablePlugins()` returns names sorted alphabetically
- `createProcessor()` for a name not in the cache returns nullptr and sets error
- `createProcessor()` blocks (loads .vst3/.component from disk) — control thread only

## Error Conditions

- `loadCache()` with nonexistent file: returns false, sets error, list is empty
- `loadCache()` with malformed XML: returns false, sets error, list is empty
- `loadCacheFromString()` with invalid XML: returns false, sets error, list is empty
- `findByName()` with unknown name: returns nullptr
- `createProcessor()` with unknown name: returns nullptr, sets error
- `createProcessor()` with plugin that fails to load: returns nullptr, sets error
- `createProcessor()` with sampleRate 0 or blockSize 0: returns nullptr, sets error

## Does NOT Handle

- **Plugin scanning / discovery** — future (only loads pre-built XML caches)
- **Plugin state / preset management** — PluginProcessor concern
- **Processor ownership** — Engine owns processors after they are added
- **Plugin GUI** — PluginEditorWindow concern
- **Plugin parameter access** — Engine's generic parameter system via Processor interface
- **Sample rate / block size changes after creation** — future

## Dependencies

- Processor (the interface returned by `createProcessor()`)
- PluginProcessor (concrete type created by `createProcessor()`)
- JUCE (`juce_audio_processors`: AudioPluginFormatManager, KnownPluginList, PluginDescription)

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| `loadCache()` / `loadCacheFromString()` | Control | Replaces internal list — not concurrent with other calls |
| `findByName()` | Control | Read-only, safe concurrent with other reads |
| `getAvailablePlugins()` / `getNumPlugins()` | Control | Read-only |
| `createProcessor()` | Control | Blocks (disk I/O) — never call from audio thread |

All PluginManager methods are called from the control thread. The FFI layer serializes access through `controlMutex_`.

## Example Usage

### C ABI

```c
char* error = NULL;
SqEngine engine = sq_create(44100.0, 512, &error);

if (!sq_load_plugin_cache(engine, "/path/to/plugin-cache.xml", &error)) {
    fprintf(stderr, "Cache load failed: %s\n", error);
    sq_free_string(error);
}

SqStringList plugins = sq_available_plugins(engine);
for (int i = 0; i < plugins.count; i++)
    printf("Plugin: %s\n", plugins.items[i]);
sq_free_string_list(plugins);

// Create a source with a plugin generator
SqSource synth = sq_add_source_plugin(engine, "Lead", "Diva.vst3", &error);

// Add a plugin to a bus chain
SqBus master = sq_master(engine);
SqProc limiter = sq_bus_append(engine, master, "Limiter.vst3");

sq_destroy(engine);
```

### Python

```python
from squeeze import Squeeze

s = Squeeze()
s.load_plugin_cache("/path/to/plugin-cache.xml")

print(f"Available: {s.available_plugins}")

synth = s.add_source("Lead", plugin="Diva.vst3")
s.master.chain.append("Limiter.vst3")

s.close()
```
