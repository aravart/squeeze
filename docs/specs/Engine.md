# Engine Specification (Tier 0 — Empty Shell)

This is the initial Engine spec covering only lifecycle and version. It will grow as components are added.

## Responsibilities

- Provide the top-level object that FFI callers create and destroy
- Report the engine version string
- Manage JUCE initialization (lazy, process-wide, never torn down)

## Interface

### C++ (`squeeze::Engine`)

```cpp
namespace squeeze {
class Engine {
public:
    Engine();
    ~Engine();
    std::string getVersion() const;  // Returns "0.2.0"
};
}
```

### C ABI (`squeeze_ffi.h`)

```c
typedef void* SqEngine;

SqEngine sq_engine_create(char** error);
void     sq_engine_destroy(SqEngine engine);
char*    sq_version(SqEngine engine);
void     sq_free_string(char* s);
```

### EngineHandle (internal)

```cpp
struct EngineHandle {
    squeeze::Engine engine;
};
```

`sq_engine_create()` allocates an `EngineHandle` on the heap. The returned opaque `SqEngine` pointer is the only handle the caller needs.

## Invariants

- `sq_engine_create` returns a non-NULL handle on success
- `sq_engine_create` returns NULL and sets `*error` on failure
- `sq_engine_destroy(NULL)` is a no-op
- `sq_free_string(NULL)` is a no-op
- `sq_version` returns a non-NULL string that the caller must free with `sq_free_string`
- Multiple engines can be created and destroyed independently
- JUCE MessageManager is initialized exactly once, on the first `sq_engine_create` call

## Error Conditions

- `sq_engine_create`: allocation failure or JUCE init failure → returns NULL, sets `*error`
- `sq_engine_create`: NULL `error` pointer is safe — error message is discarded

## Does NOT Handle

- Audio device management (future tier)
- Graph, nodes, connections (future tier)
- Transport, MIDI, scheduling (future tier)
- Plugin hosting (future tier)

## Dependencies

- JUCE (`juce_core`, `juce_events` for MessageManager)
- C standard library (`malloc`, `free`, `strdup`)

## Thread Safety

- `sq_engine_create` and `sq_engine_destroy` are control-thread only
- JUCE init is guarded by a static flag (single-threaded assumption for first create call)

## Example Usage

```c
#include "squeeze_ffi.h"
#include <stdio.h>

int main() {
    char* error = NULL;
    SqEngine engine = sq_engine_create(&error);
    if (!engine) {
        fprintf(stderr, "Failed: %s\n", error);
        sq_free_string(error);
        return 1;
    }

    char* version = sq_version(engine);
    printf("Squeeze %s\n", version);
    sq_free_string(version);

    sq_engine_destroy(engine);
    return 0;
}
```

```python
import ctypes
sq = ctypes.cdll.LoadLibrary("libsqueeze_ffi.dylib")
# ... see examples/hello.py
```
