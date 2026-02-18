# PluginEditorWindow Specification

## Responsibilities

- Host a plugin's native editor GUI in a standalone window
- Manage the lifecycle of editor windows (open, close, close-all)
- Bridge the FFI/control thread to the JUCE message thread for all GUI operations
- Provide a dispatch loop function so Python (which owns the main thread) can pump the JUCE message queue

## Interface

### PluginEditorWindow (C++, `src/gui/PluginEditorWindow.h`)

Header-only `juce::DocumentWindow` subclass that owns an `AudioProcessorEditor`.

```cpp
class PluginEditorWindow : public juce::DocumentWindow {
public:
    PluginEditorWindow(const juce::String& name,
                       juce::AudioProcessorEditor* editor,
                       int procHandle,
                       std::function<void(int)> onClose);

    void closeButtonPressed() override;
    // Defers erasure from EditorManager's map via callAsync to avoid
    // destroying the window from within its own callback.
};
```

### EditorManager (C++, `src/gui/EditorManager.h` / `.cpp`)

```cpp
class EditorManager {
public:
    bool open(Engine& engine, int procHandle, std::string& error);
    bool close(int procHandle, std::string& error);
    void closeAll();
    bool hasEditor(int procHandle) const;

private:
    static bool runOnMessageThread(std::function<void()> fn);
    std::map<int, std::unique_ptr<PluginEditorWindow>> windows_;
};
```

### C ABI (`squeeze_ffi.h`)

```c
bool sq_editor_open(SqEngine engine, SqProc proc, char** error);
bool sq_editor_close(SqEngine engine, SqProc proc, char** error);
bool sq_editor_has(SqEngine engine, SqProc proc);
void sq_process_events(int timeout_ms);
```

### Python (`processor.py`, `squeeze.py`)

```python
class Processor:
    def open_editor(self) -> None:
    def close_editor(self) -> None:
    @property
    def editor_open(self) -> bool:

class Squeeze:
    @staticmethod
    def process_events(timeout_ms: int = 0) -> None:
```

## Invariants

- At most one editor window per processor handle at any time
- Closing the window's close button removes the window from the map (deferred via `callAsync`)
- `closeAll()` is called during `sq_destroy` before the engine is deleted
- The core library (`squeeze_core`) has zero GUI dependencies — `EditorManager` and `PluginEditorWindow` are compiled only into `squeeze_ffi`
- `runOnMessageThread` executes synchronously if already on the message thread, otherwise dispatches via `callAsync` + `WaitableEvent`

## Error Conditions

- `open` with unknown processor handle: returns false, sets error "Processor not found"
- `open` with a processor that is not a PluginProcessor: returns false, sets error "Processor is not a plugin"
- `open` with a plugin that has no editor: returns false, sets error "Plugin has no editor"
- `open` when editor already open for that processor: returns false, sets error "Editor already open"
- `open` when GUI dispatch times out (5s): returns false, sets error "GUI unavailable (timeout)"
- `close` when no editor is open for that processor: returns false, sets error "No editor open"
- `close` when GUI dispatch times out: returns false, sets error "GUI unavailable (timeout)"

## Does NOT Handle

- Plugin state save/restore via the editor
- Window position persistence across sessions
- Resizable editor negotiation (editors are displayed at their preferred size)
- Multiple windows for the same plugin

## Dependencies

- `Engine` — for processor handle lookup via `processorRegistry_`
- `PluginProcessor` — for `getProcessor()` and `dynamic_cast` check
- `juce::AudioProcessor` — for `hasEditor()` and `createEditorIfNeeded()`
- `juce::DocumentWindow` — window hosting
- `juce::MessageManager` — thread dispatch

## Thread Safety

- `open()`, `close()`, `hasEditor()` are called from the FFI/control thread
- All GUI object creation/destruction is dispatched to the message thread via `runOnMessageThread()`
- `runOnMessageThread()` uses `callAsync` + `WaitableEvent` with a 5-second timeout
- Lock ordering: `open()` looks up the processor first (acquires+releases `controlMutex_`), THEN dispatches to message thread. Never holds both locks simultaneously.
- `closeButtonPressed()` uses `callAsync` to defer map erasure, avoiding destruction during callback

## Example Usage

```python
from squeeze import Squeeze, set_log_level

with Squeeze() as s:
    s.load_plugin_cache("plugin-cache.xml")
    synth = s.add_source("Dexed", plugin="Dexed")
    synth.route_to(s.master)

    s.start()
    synth.generator.open_editor()

    # Main loop — pump GUI events
    while True:
        Squeeze.process_events(timeout_ms=50)

    s.stop()
```
