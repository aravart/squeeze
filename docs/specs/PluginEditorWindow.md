# PluginEditorWindow Specification

## Responsibilities
- Host a plugin's native GUI editor in a desktop window
- Provide Lua API to show/hide editor windows via node objects
- Manage window lifecycle (creation, close button, cleanup on shutdown)
- Bridge between REPL thread (Lua calls) and message thread (GUI operations)

## Interface

### PluginEditorWindow (C++ — app layer, not core)

A juce::DocumentWindow subclass that hosts an AudioProcessorEditor.

    class PluginEditorWindow : public juce::DocumentWindow {
    public:
        PluginEditorWindow(const juce::String& name,
                           juce::AudioProcessorEditor* editor,
                           int nodeId,
                           std::function<void(int)> onClose);
        void closeButtonPressed() override;
    private:
        int nodeId_;
        std::function<void(int)> onClose_;
    };

- Constructor: takes ownership of the editor component, sizes window to editor bounds, makes visible
- closeButtonPressed: defers removal from the window map via MessageManager::callAsync

### Lua API — Node objects

sq.add_plugin returns a PluginNode object. sq.add_midi_input returns a MidiInputNode object. Both carry their node ID and provide convenience methods.

    -- Plugin node methods
    local synth = sq.add_plugin("Pigments")
    synth:open_editor()                  --> true | nil, "error"
    synth:close_editor()                 --> true | nil, "error"
    synth:set_param("Cutoff", 0.5)       --> true | nil, "error"
    synth:get_param("Cutoff")            --> 0.5 | nil, "error"
    synth:params()                       --> {"Cutoff", "Resonance", ...} | nil, "error"
    synth:ports()                        --> {inputs={...}, outputs={...}} | nil, "error"
    synth:remove()                       --> true | nil, "error"
    synth.id                             --> integer node ID
    synth.name                           --> "Pigments"

    -- MIDI input node methods
    local kb = sq.add_midi_input("KeyStep Pro")
    kb:remove()
    kb.id                                --> integer node ID
    kb.name                              --> "KeyStep Pro"

    -- connect() accepts node objects or bare IDs
    sq.connect(kb, "midi_out", synth, "midi_in")
    sq.connect(kb.id, "midi_out", synth.id, "midi_in")  -- also works

    -- Raw ID-based functions still work for power users
    sq.open_editor(synth.id)
    sq.close_editor(synth.id)

### Main loop

The main thread runs a GUI message pump instead of a sleep loop. The REPL runs on a background juce::Thread. Both interactive and non-interactive modes use the same pump.

## Invariants
- At most one editor window per node (calling open_editor twice returns an error)
- Closing a window via the X button removes it from the map (no dangling pointers)
- All windows are destroyed before engine.stop() during shutdown
- Core library (squeeze_core, squeeze_lua) has zero GUI dependencies
- The message pump runs on the main thread (required by macOS for GUI)
- Editor windows receive keyboard focus (macOS: Process::setDockIconVisible)
- Node objects are lightweight Lua tables with metatables — no C++ usertype needed
- sq.connect accepts either node objects (extracts .id) or bare integers

## Error Conditions
- open_editor on a non-plugin node: returns nil, "Node X is not a plugin"
- open_editor on a plugin with no editor: returns nil, "Plugin has no editor"
- open_editor when already open: returns nil, "Editor already open for node X"
- close_editor when not open: returns nil, "No editor open for node X"
- MessageManagerLock fails: returns nil, "GUI unavailable"

## Does NOT Handle
- Plugin state save/restore via GUI (future milestone)
- Window position persistence across sessions
- Resizable editor negotiation (uses plugin's default size)

## Dependencies
- PluginNode (getProcessor() -> createEditorIfNeeded(), hasEditor())
- Engine (getNode() for nodeId lookup, dynamic_cast to PluginNode)
- juce_gui_basics (DocumentWindow, Component, MessageManager)
- juce_graphics (transitive dependency of juce_gui_basics)

## Thread Safety
- open_editor / close_editor: called from REPL thread; use MessageManagerLock
- closeButtonPressed: message thread; uses callAsync to defer map removal
- Editor window map: mutated under MessageManagerLock or on message thread only
- Main thread: dispatchNextMessageOnSystemQueue loop

## Example Usage

    local synth = sq.add_plugin("Pigments")
    local reverb = sq.add_plugin("ValhallaRoom")
    local kb = sq.add_midi_input("KeyStep Pro")

    sq.connect(kb, "midi_out", synth, "midi_in")
    sq.connect(synth, "out", reverb, "in")
    sq.start()

    synth:open_editor()
    reverb:open_editor()
    synth:set_param("Cutoff", 0.7)
