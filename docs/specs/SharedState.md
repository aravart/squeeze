# SharedState Specification

## Overview

SharedState publishes engine state into a POSIX shared memory region so that external processes can read it without any IPC overhead. The audio thread writes scalars (meters, playheads, transport) and scope waveform data into fixed-size structures. Any number of clients mmap the same region and read at whatever frequency they wish — no registration, no protocol, no serialization.

## Responsibilities

- Allocate and manage a named POSIX shared memory region
- Publish per-node scalar state from the audio thread (peak levels, playhead position, voice count)
- Publish system-level scalars (callback timing, xrun count, transport position)
- Provide per-tap-point audio waveform ring buffers for scope visualization
- Allow clients to enable/disable scope taps from the control thread
- Ensure all audio-thread writes are RT-safe (no allocation, no blocking)
- Ensure clients never block or interfere with the audio thread

## Data Model

### Memory Layout

```
┌──────────────────────────────────────────────┐  offset 0
│  Header                                      │
├──────────────────────────────────────────────┤
│  ScalarBlock (SeqLock-protected)             │
├──────────────────────────────────────────────┤
│  ScopeRing[0]                                │
│  ScopeRing[1]                                │
│  ...                                         │
│  ScopeRing[kMaxScopeTaps - 1]                │
├──────────────────────────────────────────────┤
│  TapTable (atomic pointer-index swap)        │
└──────────────────────────────────────────────┘
```

All types are POD. No pointers, no STL containers, no heap-allocated members.

### Constants

```cpp
static constexpr int kMaxNodes = 256;
static constexpr int kMaxScopeTaps = 8;
static constexpr int kScopeRingSize = 8192;   // samples per channel per tap
static constexpr uint32_t kMagic = 0x53515A53; // "SQZS"
static constexpr uint32_t kVersion = 1;
```

### Header

```cpp
struct SharedHeader {
    uint32_t magic;         // kMagic — clients verify this before reading
    uint32_t version;       // kVersion — clients reject mismatched versions
    uint32_t shmSize;       // total size in bytes (for client validation)
    uint32_t pid;           // engine process ID (detect stale shm)
};
```

### ScalarBlock

Updated from the audio thread every callback via SeqLock. Contains all per-node and system-level scalars.

```cpp
struct NodeScalars {
    int32_t id;                 // node ID (-1 = unused slot)
    float peakL;                // peak level, left channel (linear, 0.0–1.0+)
    float peakR;                // peak level, right channel
    double playhead;            // sample read position (for SamplerNodes; -1.0 if N/A)
    int32_t activeVoices;       // active voice count (for SamplerNodes; 0 otherwise)
    uint8_t flags;              // bit 0: isLeaf, bit 1: isSampler, bit 2: hasBuffer
    uint8_t pad[3];
};

struct ScalarBlock {
    std::atomic<uint32_t> seq;   // SeqLock sequence counter

    // System
    uint32_t blockCount;         // monotonic, incremented each callback
    double sampleRate;
    int32_t blockSize;
    double callbackDurationUs;   // last callback wall time
    int32_t xrunCount;           // cumulative

    // Nodes
    int32_t numNodes;
    NodeScalars nodes[kMaxNodes];
};
```

**Peak metering**: `peakL`/`peakR` are the peak absolute sample values across the current audio block's output. They are *not* smoothed — clients apply their own ballistics (decay, hold). This keeps the audio-thread write trivial (one `max` per channel per sample) and gives clients full control over visual behavior.

**Playhead**: For SamplerNodes, `playhead` is `readPosition_` of voice 0 (the most recently triggered voice). For non-sampler nodes, it is -1.0. Phase 2 may add per-voice playhead arrays.

### ScopeRing

One per tap point. The audio thread appends samples; clients read behind the write head.

```cpp
struct ScopeRing {
    std::atomic<uint32_t> writeHead;   // sample index (wraps at kScopeRingSize)
    int32_t nodeId;                     // which node this tap reads (-1 = inactive)
    int32_t numChannels;                // 1 or 2 (set when tap is enabled)
    float samples[2][kScopeRingSize];   // [channel][sample], always stereo-sized
};
```

- Audio thread writes `numSamples` floats per channel per callback, advances `writeHead` with a single relaxed atomic store
- Clients read `writeHead`, then copy out whatever range they need
- If a client is too slow and gets lapped (writeHead moves past its read position by more than `kScopeRingSize`), it sees a discontinuity — acceptable for visualization
- `nodeId == -1` means this tap is inactive; audio thread skips it

### TapTable

Maps tap slots to node IDs. Written by the control thread, read by the audio thread via atomic index swap.

```cpp
struct TapEntry {
    int32_t nodeId;     // -1 = inactive
};

struct TapTable {
    TapEntry entries[kMaxScopeTaps];
};
```

The control thread maintains two `TapTable` copies. After modifying one, it atomically swaps the active index. The audio thread reads the active table to decide which nodes to tap.

## Interface

```cpp
namespace squeeze {

class SharedState {
public:
    SharedState();
    ~SharedState();

    // --- Control thread ---

    // Create/open the shared memory region. Returns false on failure.
    // name: POSIX shm name (e.g., "/squeeze-12345" where 12345 is the PID).
    bool open(const std::string& name);

    // Close and unlink the shared memory region.
    void close();

    bool isOpen() const;

    // Return the shm name (for clients to connect).
    std::string getName() const;

    // Configure system parameters (called from Engine::prepare or start).
    void prepare(double sampleRate, int blockSize);

    // Enable a scope tap on a node. Returns tap index (0..kMaxScopeTaps-1),
    // or -1 if all taps are in use or nodeId is invalid.
    int enableScopeTap(int nodeId);

    // Disable a scope tap. Clears the ring buffer.
    void disableScopeTap(int tapIndex);

    // Disable all scope taps for a given node (called on node removal).
    void disableScopeTapsForNode(int nodeId);

    // --- Audio thread (all methods are RT-safe) ---

    // Called once per callback, before node processing.
    // Increments blockCount, records system state.
    void beginCallback();

    // Record a node's output levels and state after its process() call.
    // slotIndex: position in execution order.
    // nodeId: graph node ID.
    // output: the node's output audio buffer.
    // numSamples: samples in this block.
    void recordNode(int slotIndex, int nodeId,
                    const juce::AudioBuffer<float>& output,
                    int numSamples);

    // Record sampler-specific state.
    void recordSamplerState(int slotIndex, double playhead,
                            int activeVoices, uint8_t flags);

    // Write scope data for tapped nodes. Called after all nodes have processed.
    // Takes the snapshot's audioOutputs and slots to find tapped nodes.
    void writeScopeTaps(const GraphSnapshot& snapshot, int numSamples);

    // Publish the ScalarBlock (SeqLock write). Called at end of callback.
    void endCallback(double callbackDurationUs, int xrunCount);
};

} // namespace squeeze
```

## Engine Integration

SharedState is a member of Engine, called from `processBlock`:

```cpp
void Engine::processBlock(AudioBuffer<float>& out, MidiBuffer& outMidi, int numSamples)
{
    perfMonitor_.beginCallback();
    sharedState_.beginCallback();

    // ... drain scheduler ...

    // Process each node
    for (int i = 0; i < slotCount; ++i) {
        auto& slot = snap.slots[i];

        perfMonitor_.beginNode(i, slot.nodeId);
        slot.node->process(ctx);
        perfMonitor_.endNode(i);

        // Record levels + state for shared memory
        sharedState_.recordNode(i, slot.nodeId, snap.audioOutputs[i], numSamples);
    }

    // Write scope ring buffers for active taps
    sharedState_.writeScopeTaps(snap, numSamples);

    // ... sum leaf nodes ...

    perfMonitor_.endCallback();
    sharedState_.endCallback(callbackDurationUs, xrunCount_.load());
}
```

### Sampler Playhead Recording

SamplerNode needs a lightweight query method for the audio thread to read playhead data without going through VoiceAllocator's full interface:

```cpp
// Added to SamplerNode (public, audio-thread-safe):
double getPlayhead() const;          // readPosition_ of voice 0
int getActiveVoiceCount() const;     // delegates to VoiceAllocator
```

Engine calls `recordSamplerState()` if the node is a SamplerNode (detected via `dynamic_cast` once at snapshot build time, cached as a flag in `NodeSlot`).

## SeqLock Protocol

Identical to PerfMonitor. The audio thread is the sole writer; any number of readers (in-process or cross-process via shm) spin-read.

**Writer (audio thread, in `endCallback`):**
```
seq.store(seq + 1, release)   // odd = write in progress
memcpy scalars into block
seq.store(seq + 1, release)   // even = write complete
```

**Reader (any process):**
```
do {
    s1 = seq.load(acquire)
    if (s1 & 1) continue        // write in progress
    copy data
    s2 = seq.load(acquire)
} while (s1 != s2)
```

The SeqLock is **in the shared memory region itself**, so cross-process reads work with no additional synchronization.

## Scope Ring Protocol

The audio thread writes contiguously and advances `writeHead` atomically. No SeqLock needed — a single atomic index is sufficient because:

1. Samples are plain floats written sequentially
2. The ring is large enough (~186ms at 44.1kHz) that clients reading at 30–60fps are never lapped
3. A torn read of a few samples at the write boundary is visually imperceptible in a scope display

**Writer (audio thread, in `writeScopeTaps`):**
```
for each active tap:
    head = ring.writeHead.load(relaxed)
    for each sample in block:
        ring.samples[ch][(head + s) % kScopeRingSize] = value
    ring.writeHead.store((head + numSamples) % kScopeRingSize, release)
```

**Reader (client process):**
```
head = ring.writeHead.load(acquire)
// copy out last N samples behind head
for i in 0..N-1:
    idx = (head - N + i + kScopeRingSize) % kScopeRingSize
    sample = ring.samples[ch][idx]
```

## Client Library

A minimal header-only reader for clients to mmap and read the shared state:

```cpp
// squeeze_shm_reader.h (standalone, no JUCE dependency)
namespace squeeze {

class SharedStateReader {
public:
    bool open(const std::string& name);
    void close();
    bool isOpen() const;

    // Read the latest scalar block. Returns false if SeqLock was contended
    // (caller should retry or skip this frame).
    bool readScalars(ScalarBlock& out) const;

    // Read scope waveform data. Copies the last numSamples from the ring.
    // Returns actual number of samples copied.
    int readScope(int tapIndex, int channel, float* dest, int numSamples) const;

    // Check if engine is alive (compare PID, check blockCount is advancing).
    bool isEngineAlive() const;
};

} // namespace squeeze
```

This is a standalone header + small .cpp — no JUCE, no squeeze_core dependency. Any client (web UI via native module, Python via ctypes, monitoring tool) can use it.

## Invariants

- All audio-thread writes are RT-safe: no allocation, no blocking, no syscalls
- The audio thread never reads from the shm region (it only writes)
- `open()` / `close()` are control-thread operations; shm lifecycle matches engine lifecycle
- The shared memory region has a fixed size determined at compile time by the constants
- `magic` and `version` in the header allow clients to detect incompatible layouts
- `pid` in the header allows clients to detect a stale (dead engine) region
- ScalarBlock writes are atomic (SeqLock) — clients always see a consistent frame
- ScopeRing writes are ordered (release store on writeHead) — clients see complete blocks
- Scope taps are opt-in: untapped nodes incur zero overhead on the audio thread
- Disabling a scope tap clears the ring and sets `nodeId = -1`
- `recordNode` peak metering is always active (cost: one `max` per channel per sample — negligible)
- At most `kMaxScopeTaps` (8) nodes can be scope-tapped simultaneously
- At most `kMaxNodes` (256) nodes can report scalar state

## Error Conditions

| Condition | Behavior |
|---|---|
| `shm_open` fails (permissions, name collision) | `open()` returns false |
| Client opens before engine | `open()` succeeds but `magic` check fails |
| Engine crashes without `close()` | Stale shm persists; client detects via `pid` check |
| More than `kMaxNodes` nodes | Excess nodes not tracked, no crash |
| All scope taps in use | `enableScopeTap()` returns -1 |
| `enableScopeTap` for invalid nodeId | Returns -1 |
| `disableScopeTap` with invalid index | No-op |
| Node removed while scope-tapped | `disableScopeTapsForNode()` cleans up |
| `endCallback` before `open()` | No-op (null shm pointer check) |
| Client reads during write | SeqLock retry (scalars) or minor visual glitch (scope) |

## Does NOT Handle

- Bidirectional communication (clients only read; commands go through Lua/OSC/WebSocket)
- Event streams (note triggers, parameter change notifications — use sockets for those)
- Historical data or recording (only latest state / last ~186ms of scope data)
- Scope data for the mixed output bus (taps are per-node; a "master out" tap would be a future addition)
- Per-voice playhead arrays (Phase 1 publishes voice 0 only)
- FFT / spectrum analysis (clients compute from raw scope samples)
- Smoothed or ballistic metering (clients apply their own decay/hold)
- Authentication or access control on the shm region (local trust model)
- Windows support (POSIX shm only; Windows would need a `CreateFileMapping` backend)

## Dependencies

- `<sys/mman.h>` — `shm_open`, `mmap`, `munmap`, `shm_unlink`
- `<unistd.h>` — `ftruncate`, `getpid`, `close`
- `<atomic>` — SeqLock sequence counter, ScopeRing writeHead
- Engine — integration into `processBlock`
- GraphSnapshot — for resolving tapped node outputs
- SamplerNode — for playhead/voice queries (via cached flag in NodeSlot)

## Thread Safety

| Method | Thread | Notes |
|---|---|---|
| `open()`, `close()` | Control | Called once at engine start/stop |
| `prepare()` | Control | Sets sampleRate/blockSize in ScalarBlock |
| `enableScopeTap()`, `disableScopeTap()` | Control | Swaps TapTable atomically |
| `disableScopeTapsForNode()` | Control | Called from `Engine::removeNode()` |
| `getName()`, `isOpen()` | Any | Immutable after `open()` |
| `beginCallback()` | Audio | No-op if not open |
| `recordNode()` | Audio | Writes into staging ScalarBlock |
| `recordSamplerState()` | Audio | Writes into staging ScalarBlock |
| `writeScopeTaps()` | Audio | Reads TapTable atomically, writes ScopeRings |
| `endCallback()` | Audio | SeqLock publish of ScalarBlock |
| `SharedStateReader::*` | Client (any process) | Lock-free reads from mmap'd region |

## Lua API

```lua
-- Shared memory is auto-opened at engine start.
-- Returns the shm name for clients to connect.
sq.shm_name()   -- e.g. "/squeeze-12345"

-- Scope taps
local tap = sq.scope_tap(nodeId)     -- returns tap index (0-7) or nil, err
sq.scope_untap(tap)                  -- disable tap
sq.scope_taps()                      -- returns table of {tap=idx, node=id}

-- Scalars are always published; no enable/disable needed.
-- Clients read shm directly — no Lua API for reading scalars.
```

## Example Usage

### Engine-side setup (C++)

```cpp
// In Engine::start()
sharedState_.open("/squeeze-" + std::to_string(getpid()));
sharedState_.prepare(sampleRate, blockSize);

// In Engine::stop()
sharedState_.close();
```

### Client-side reading (C++)

```cpp
squeeze::SharedStateReader reader;
if (!reader.open("/squeeze-12345"))
    return;  // engine not running

// Poll at 60fps
ScalarBlock scalars;
if (reader.readScalars(scalars)) {
    for (int i = 0; i < scalars.numNodes; ++i) {
        auto& n = scalars.nodes[i];
        printf("node %d: peak=%.3f playhead=%.0f voices=%d\n",
               n.id, std::max(n.peakL, n.peakR), n.playhead, n.activeVoices);
    }
}

// Read scope waveform (last 512 samples)
float buf[512];
int got = reader.readScope(0, 0, buf, 512);  // tap 0, channel 0
// ... draw waveform from buf[0..got-1] ...
```

### Client-side reading (Python via ctypes)

```python
import ctypes, mmap, struct

fd = os.open("/dev/shm/squeeze-12345", os.O_RDONLY)
shm = mmap.mmap(fd, 0, access=mmap.ACCESS_READ)

# Read header
magic, version, size, pid = struct.unpack_from("IIII", shm, 0)
assert magic == 0x53515A53
```

## Memory Budget

| Component | Size |
|---|---|
| Header | 16 bytes |
| ScalarBlock | ~16 + (256 * 32) = ~8.2 KB |
| ScopeRing * 8 | 8 * (8 + 8192 * 2 * 4) = ~512 KB |
| TapTable * 2 | 2 * (8 * 4) = 64 bytes |
| **Total** | **~520 KB** |
