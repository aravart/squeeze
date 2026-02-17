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
                       int nodeId,
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
    bool open(Engine& engine, int nodeId, std::string& error);
    bool close(int nodeId, std::string& error);
    void closeAll();
    bool hasEditor(int nodeId) const;

private:
    static bool runOnMessageThread(std::function<void()> fn);
    std::map<int, std::unique_ptr<PluginEditorWindow>> windows_;
};
```

### C ABI (`squeeze_ffi.h`)

```c
bool sq_open_editor(SqEngine engine, int node_id, char** error);
bool sq_close_editor(SqEngine engine, int node_id, char** error);
bool sq_has_editor(SqEngine engine, int node_id);
void sq_run_dispatch_loop(int timeout_ms);
```

### Low-level Python (`_low_level.py`)

```python
class Squeeze:
    def open_editor(self, node_id: int) -> None:        # raises SqueezeError
    def close_editor(self, node_id: int) -> None:       # raises SqueezeError
    def has_editor(self, node_id: int) -> bool:

    @staticmethod
    def run_dispatch_loop(timeout_ms: int = 50) -> None:
```

### High-level Python (`node.py`, `engine.py`)

```python
class Node:
    def open_editor(self) -> None:
    def close_editor(self) -> None:
    @property
    def editor_open(self) -> bool:

class Engine:
    @staticmethod
    def run_dispatch_loop(timeout_ms: int = 50) -> None:
```

## Invariants

- At most one editor window per node ID at any time
- Closing the window's close button removes the window from the map (deferred via `callAsync`)
- `closeAll()` is called during `sq_engine_destroy` before the engine is deleted
- The core library (`squeeze_core`) has zero GUI dependencies — `EditorManager` and `PluginEditorWindow` are compiled only into `squeeze_ffi`
- `runOnMessageThread` executes synchronously if already on the message thread, otherwise dispatches via `callAsync` + `WaitableEvent`

## Error Conditions

- `open` with non-existent node ID: returns false, sets error "Node N not found"
- `open` with a node that is not a PluginNode: returns false, sets error "Node N is not a plugin"
- `open` with a plugin that has no editor: returns false, sets error "Plugin has no editor"
- `open` when editor already open for that node: returns false, sets error "Editor already open for node N"
- `open` when GUI dispatch times out (5s): returns false, sets error "GUI unavailable (timeout)"
- `close` when no editor is open for that node: returns false, sets error "No editor open for node N"
- `close` when GUI dispatch times out: returns false, sets error "GUI unavailable (timeout)"

## Does NOT Handle

- Plugin state save/restore via the editor
- Window position persistence across sessions
- Resizable editor negotiation (editors are displayed at their preferred size)
- Multiple windows for the same plugin

## Dependencies

- `Engine` — for `getNode()` and `getNodeName()`
- `PluginNode` — for `getProcessor()` and `dynamic_cast` check
- `juce::AudioProcessor` — for `hasEditor()` and `createEditorIfNeeded()`
- `juce::DocumentWindow` — window hosting
- `juce::MessageManager` — thread dispatch

## Thread Safety

- `open()`, `close()`, `hasEditor()` are called from the FFI/control thread
- All GUI object creation/destruction is dispatched to the message thread via `runOnMessageThread()`
- `runOnMessageThread()` uses `callAsync` + `WaitableEvent` with a 5-second timeout
- Lock ordering: `open()` calls `engine.getNode()` first (acquires+releases `controlMutex_`), THEN dispatches to message thread. Never holds both locks simultaneously.
- `closeButtonPressed()` uses `callAsync` to defer map erasure, avoiding destruction during callback

## Example Usage

```python
from squeeze import Engine, set_log_level

with Engine() as engine:
    engine.load_plugin_cache("plugin-cache.xml")
    synth = engine.add_plugin("Dexed")
    synth >> engine.output

    engine.start()
    synth.open_editor()

    # Main loop — pump GUI events
    while synth.editor_open:
        Engine.run_dispatch_loop(50)

    engine.stop()
```
