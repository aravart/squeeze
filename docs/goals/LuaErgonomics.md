# Lua API Ergonomics

## Where the friction is today

A typical session looks like this:

```lua
local kb = sq.add_midi_input("KeyStep Pro")
local synth = sq.add_plugin("Pigments")
local verb = sq.add_plugin("ValhallaRoom")

sq.connect(kb, "midi_out", synth, "midi_in")
sq.connect(synth, "out", verb, "in")
sq.update()
sq.start()

synth:set_param("Cutoff", 0.7)
```

That's already decent. But the friction points are:

### 1. Port names are guesswork

You have to call `synth:ports()` to discover that the port is called `"out"` and not `"audio_out"` or `"main"`. Every `sq.connect` call requires knowing exact port names for both sides. This is the single biggest source of friction.

**Fix: auto-connect by signal type.** If you say `sq.connect(synth, verb)` with no port names, try to find the unique matching pair -- one audio output on src, one audio input on dst. Same for MIDI. Only require explicit port names when there's ambiguity (sidechain, multiple outputs, etc.).

```lua
sq.connect(kb, synth)          -- MIDI: only one midi_out, only one midi_in
sq.connect(synth, verb)        -- audio: only one audio out, only one audio in
sq.connect(synth, "sc_out", verb, "sidechain")  -- explicit when ambiguous
```

This is pure Lua -- the bootstrap can inspect `ports()` before calling the raw connect.

### 2. The `>>` operator for signal chains

Lua 5.4 has `__shr` (right shift). This lets you write:

```lua
kb >> synth >> verb
```

Each `>>` call does the auto-connect from point 1. The return value is always the right-hand side so chains compose naturally. This is the "Lua DSL" moment -- it feels like drawing a signal flow.

For channel-filtered MIDI, you'd need a small helper:

```lua
kb:ch(3) >> synth    -- filter to MIDI channel 3
```

Where `ch()` returns a lightweight wrapper that carries the channel info into the `>>` resolution.

### 3. `sq.update()` is a footgun

Every topology change requires a manual `sq.update()` or the audio thread keeps using the old graph. People will forget this constantly. Two options:

- **Auto-update on connect/disconnect**: each `sq.connect()` and `sq.disconnect()` calls `engine.updateGraph()` automatically. Downside: if you're making 10 connections, that's 10 snapshot builds. But control-plane latency doesn't matter -- these are infrequent.
- **Deferred auto-update**: set a dirty flag, flush before `processBlock` or after a short timer. More complex, less obvious.

Go with auto-update. The cost is negligible and it eliminates a whole class of "why isn't my change taking effect?" bugs. Keep `sq.update()` as a manual override for the batch case.

### 4. Parameters as properties

Currently: `synth:set_param("Cutoff", 0.7)` and `synth:get_param("Cutoff")`

With `__newindex` / `__index` on the metatable:

```lua
synth.Cutoff = 0.7
print(synth.Cutoff)    -- 0.7
```

The catch: parameter names with spaces or special characters (`"LFO 1 Rate"`) won't work as Lua identifiers. So you'd use bracket syntax for those: `synth["LFO 1 Rate"] = 0.5`. The `__index`/`__newindex` metamethods would check if the key matches a parameter name and delegate accordingly, falling back to the normal metatable lookup for methods like `:ports()`.

This is genuinely nice for the common case (short parameter names) and gracefully degrades for weird names.

### 5. Bulk patching / declarative graphs

For setting up a complex routing:

```lua
sq.patch {
    kb >> synth,
    kb >> bass,
    synth >> verb >> master,
    bass >> master,
}
sq.start()
```

Where `sq.patch` takes a table of chains, builds all the connections, and does a single `updateGraph()`. The `>>` operator would need to defer its connect calls and return a "pending chain" object that `sq.patch` resolves.

This is more ambitious but extremely expressive. It reads like a patch diagram.

### 6. Quick helpers for common patterns

```lua
-- Load a plugin + open its editor in one call
local synth = sq.plugin("Pigments")   -- add_plugin + open_editor

-- Print the graph as text
sq.graph()
-- kb(1) --midi--> synth(2) --audio--> verb(3) --> [output]
```

---

## Priority

In order of bang-for-buck:

1. **Auto-connect (port inference)** -- pure Lua, eliminates the biggest friction
2. **Auto-update on connect** -- trivial C++ change, eliminates the footgun
3. **`>>` operator** -- pure Lua, makes the REPL feel like a music tool
4. **Parameter-as-property** -- pure Lua metatable work, nice but not critical
5. **`sq.patch{}`** -- builds on `>>`, good for scripts/scenes
6. **`sq.graph()` pretty printer** -- pure Lua, helpful for REPL exploration

Points 1-3 are the ones that transform the experience. The rest is polish. And notably, 1, 3, 4, and 5 are pure Lua (bootstrap changes) -- no C++ needed.
