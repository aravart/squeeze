# Concurrency Model Specification

## Architectural Decision

**Option B: Engine-level mutex.** Each gateway (Lua REPL, OSC, WebSocket) calls Engine directly from its own thread. Engine serializes all control-plane operations with a single `std::mutex controlMutex_`.

### Why not a shared command queue?

- The REPL is synchronous; blocking on a mutex is fine.
- OSC and WebSocket have different async semantics that a shared queue would constrain.
- Control-plane calls are infrequent; mutex contention is negligible.
- Cross-gateway ordering is meaningless — there is no scenario where "OSC command A must happen before WebSocket command B."

### Why not `std::recursive_mutex`?

Using `*Locked()` private helpers is cleaner and catches accidental recursive calls at development time (a deadlock is easier to debug than silent re-entry bugs).

## Thread Roles

| Thread | Role | Locks `controlMutex_`? | Blocking Allowed? |
|--------|------|------------------------|-------------------|
| Audio | `processBlock`, node `process()` | **Never** | **No** |
| Control / REPL | Lua VM, Engine control-plane calls | Yes | Yes |
| Control / OSC | OSC message handling, Engine calls | Yes | Yes |
| Control / WebSocket | WebSocket request handling, Engine calls | Yes | Yes |
| MIDI callback | `MidiInputNode::handleIncomingMidiMessage` — writes to SPSC queue only | **Never** | No |
| GUI / Message thread | Plugin editor windows, `MessageManager` dispatch | **Never** (see Lock Ordering) | Yes |
| Audio setup | `audioDeviceAboutToStart` — called by JUCE during device init | Yes | Yes |

## What the Mutex Protects

The mutex serializes access to Engine's mutable control-plane state:

- `graph_` — the Graph topology
- `ownedNodes_` — node ownership map
- `nodeNames_` — node name map
- `midiDeviceNodes_` — MIDI device tracking map
- `pendingDeletions_` — deferred node destruction list
- `ownedBuffers_` — buffer ownership map
- `bufferNames_` — buffer name map
- `nextBufferId_` — buffer ID counter
- `cache_` — plugin cache
- The act of calling `scheduler_.sendCommand()` — ensures only one thread writes to the SPSC command queue at a time, maintaining the single-producer invariant

### Not protected by the mutex

- `sampleRate_`, `blockSize_`, `running_` — these are `std::atomic` and can be read from any thread
- `activeSnapshot_` — only accessed on the audio thread
- `processBlock()` — audio thread, never locks
- `deviceManager_` — only used by `start()`/`stop()` which have their own semantics

## Lock Ordering

**Rule: Engine `controlMutex_` must never be held when acquiring `MessageManagerLock`.**

The EditorManager (app layer in main.cpp) follows this pattern:
1. Call `engine.getNode(id)` — acquires and releases `controlMutex_`
2. Then call `runOnMessageThread()` / acquire `MessageManagerLock` — no Engine lock held

This prevents the A-B / B-A deadlock between `controlMutex_` and `MessageManagerLock`.

## Gateway Patterns

### Lua REPL (synchronous)

The REPL thread calls Engine methods directly. The mutex blocks the REPL while another thread holds it — this is fine because the REPL is inherently synchronous.

```
REPL thread                    Engine
    │                            │
    │  engine.addPlugin("X")     │
    │───────────────────────────>│  lock(controlMutex_)
    │                            │  ... do work ...
    │                            │  unlock(controlMutex_)
    │<───────────────────────────│  return result
    │                            │
```

### OSC (fire-and-forget)

Most OSC messages are fire-and-forget. The OSC handler calls Engine, which locks, does the work, unlocks, and the handler returns. For messages that need a response, compose the response after the mutex is released.

### WebSocket (request/response)

WebSocket messages carry correlation IDs. The handler calls Engine (which locks/unlocks), then composes the response JSON with the correlation ID. The response is sent after the mutex is released.

## Audio Thread Guarantee

**The audio thread NEVER acquires `controlMutex_`.** This is the fundamental invariant that preserves realtime safety. The audio thread only:

- Reads `activeSnapshot_` (swapped atomically via Scheduler)
- Calls `scheduler_.processPending()` (lock-free SPSC read)
- Calls `node->process()` on nodes in the snapshot
- Calls `scheduler_.sendGarbage()` (lock-free SPSC write)
- Reads atomics (`sampleRate_`, `blockSize_`)

## Methods That Lock vs. Don't Lock

### Lock `controlMutex_`

All control-plane methods that read or mutate Engine's owned state:

- Plugin cache: `loadPluginCache`, `getAvailablePluginNames`, `findPluginByName`
- Node management: `addNode`, `removeNode`, `getNode`, `getNodeName`, `getNodes`
- Plugin instantiation: `addPlugin`
- MIDI: `getAvailableMidiInputs`, `addMidiInput`, `autoLoadMidiInputs`, `refreshMidiInputs`
- Topology: `connect`, `disconnect`, `getConnections`
- Graph push: `updateGraph` (both overloads)
- Parameters: `setParameter`, `getParameter`, `setParameterByName`, `getParameterByName`, `getParameterDescriptors`, `getParameterText`
- Buffers: `loadBuffer`, `createBuffer`, `removeBuffer`, `getBuffer`, `getBufferName`, `getBuffers`
- Testing: `prepareForTesting`
- Audio setup: `audioDeviceAboutToStart`

### Do NOT lock

- `start()` — calls `deviceManager_` methods and writes atomics; `audioDeviceAboutToStart` acquires the lock when JUCE calls it synchronously
- `stop()` — calls `deviceManager_` methods and writes `running_` atomic
- `isRunning()`, `getSampleRate()`, `getBlockSize()` — atomic reads
- `processBlock()`, `audioDeviceIOCallbackWithContext()` — audio thread
- `audioDeviceStopped()` — writes atomic only
- `getGraph()` — test-only, returns reference
