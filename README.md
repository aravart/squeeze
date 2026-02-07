# Squeeze

A programmable audio engine for live performance. Route VST plugins as nodes in a graph, control everything via Lua scripting, and connect external tools over WebSocket.

## Architecture

Squeeze is built as a real-time audio graph engine:

- **Nodes** (VST plugins, MIDI inputs, samplers) are connected in a directed graph
- **Audio and MIDI** flow through connections between nodes with per-connection channel filtering
- **Lua scripting** provides full control: load plugins, route signals, tweak parameters, manage buffers
- The **audio thread** is lock-free; graph updates are swapped atomically via a scheduler

## Building

Requires CMake 3.24+ and a C++17 compiler.

```bash
cmake -B build
cmake --build build
```

### Run

```bash
./build/Squeeze_artefacts/Release/Squeeze
```

Options:
- `-c <path>` — load a VST plugin cache XML
- `-d` — enable debug logging

### Run tests

```bash
./build/squeeze_tests
```

## Tech stack

- **C++17** with **JUCE 7.0.12** for audio, MIDI, and plugin hosting
- **Lua 5.4.7** / **sol2** for scripting
- **Catch2** for testing

## Lua API

```lua
local kb    = sq.add_midi_input("Arturia KeyStep 37")
local synth = sq.add_plugin("Dexed")
local verb  = sq.add_plugin("Valhalla Room")

sq.patch {
    kb >> synth >> verb,
}

synth.Feedback = 0.6
verb.Mix = 0.4

sq.start()
```

## License

GPL-3.0. See [LICENSE.txt](LICENSE.txt).
