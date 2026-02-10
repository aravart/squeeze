# Thread Inventory

## Current threads (4)

| # | Thread | Created by | Role |
|---|--------|-----------|------|
| 1 | **Main** | `main()` | JUCE message loop (`runDispatchLoopUntil(50)`), logger drain, GUI window events, control-plane operations |
| 2 | **Audio device** | JUCE `AudioDeviceManager` (OS callback) | `Engine::processBlock()` — drains scheduler queue, processes all nodes, sums output |
| 3 | **MIDI input** | JUCE `MidiInput` (OS callback, one per device) | `MidiInputNode::handleIncomingMidiMessage()` — pushes into SPSC queue |
| 4 | **REPL** | `ReplThread` (`juce::Thread` subclass, `-i` flag) | Linenoise loop, executes Lua, calls Engine via `controlMutex_` |

## Synchronization model

- **Control plane** (main + REPL): `controlMutex_` serializes all Engine mutations
- **Audio thread**: never blocks — atomic pointer swap for graph snapshots, SPSC queue drain for commands
- **MIDI thread**: never blocks — lock-free SPSC push, atomic increment for drop count
- **Main <-> Audio**: graph snapshots via SPSC queue, old snapshots garbage-collected back on main thread

## Projected at feature-complete (6)

Two additional threads:

| # | Thread | Role |
|---|--------|------|
| 5 | **Network I/O** | WebSocket + OSC server on one async event loop. Receives commands, sends state updates. Talks to control plane via `controlMutex_`, same path as the REPL. |
| 6 | **Disk I/O** | Phase 3 disk streaming and recording. Audio thread posts read/write requests to a lock-free queue, this thread fulfills them. Classic double-buffer: audio reads buffer A while disk fills buffer B, then swap. |

## Things that do NOT need threads

- **Transport/clock** — a counter advancing in `processBlock()`. No thread.
- **Sequencer** — state machine ticked by the transport on the audio thread. "At beat N, emit MIDI event" is a comparison per block.
- **MIDI output** — called from the audio callback; JUCE handles the device write.
- **Parameter automation** — applied in `processBlock()` from pre-scheduled command queue.
- **Meters/UI state** — SeqLock publish from audio thread, polled by network thread for pushing to clients.

## Notes

- The REPL thread may go away once the WebSocket UI exists, dropping to 5 in production. Worth keeping for development.
- The only new synchronization surface is the disk I/O queue (another SPSC).
- The architecture stays the same shape throughout: one RT thread that never blocks, everything else talks to it through lock-free queues and reads its state through SeqLock/atomics.
