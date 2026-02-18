# BufferLibrary Specification

## Responsibilities

- Own `juce::AudioFormatManager` (with `registerBasicFormats()`)
- Manage loaded `Buffer` instances with monotonically increasing integer IDs (never reused)
- Load audio files into Buffers
- Create empty Buffers (for recording or programmatic use)
- Remove Buffers (with deferred deletion support)
- Provide buffer lookup by ID

## Overview

BufferLibrary is the central registry for audio buffers. It loads audio files using JUCE's format readers, creates empty buffers for recording, and manages their lifetimes with stable integer IDs. It has no Engine dependency — it is a pure buffer management component. The FFI layer orchestrates interactions between BufferLibrary and Engine (e.g., assigning a buffer to a sampler Source).

## Interface

### C++ (`squeeze::BufferLibrary`)

```cpp
namespace squeeze {

class BufferLibrary {
public:
    BufferLibrary();
    ~BufferLibrary();

    // Non-copyable, non-movable
    BufferLibrary(const BufferLibrary&) = delete;
    BufferLibrary& operator=(const BufferLibrary&) = delete;

    // --- Buffer creation ---
    int loadBuffer(const std::string& filePath, std::string& error);
    int createBuffer(int numChannels, int lengthInSamples, double sampleRate,
                     const std::string& name, std::string& error);

    // --- Buffer removal ---
    std::unique_ptr<Buffer> removeBuffer(int id);

    // --- Queries ---
    Buffer* getBuffer(int id) const;
    std::string getBufferName(int id) const;
    std::vector<std::pair<int, std::string>> getBuffers() const;
    int getNumBuffers() const;

private:
    juce::AudioFormatManager formatManager_;
    struct BufferEntry {
        std::unique_ptr<Buffer> buffer;
        std::string name;
    };
    std::unordered_map<int, BufferEntry> buffers_;
    int nextId_ = 1;
};

} // namespace squeeze
```

### C ABI (`squeeze_ffi.h`)

```c
// Buffer loading
int sq_load_buffer(SqEngine engine, const char* path, char** error);
int sq_create_buffer(SqEngine engine, int num_channels, int length_in_samples,
                     double sample_rate, const char* name, char** error);

// Buffer removal
bool sq_remove_buffer(SqEngine engine, int buffer_id);

// Buffer queries
SqBufferInfo sq_buffer_info(SqEngine engine, int buffer_id);
SqIdNameList sq_buffers(SqEngine engine);
int sq_num_buffers(SqEngine engine);
```

### Python API

```python
buf_id = engine.load_buffer("/path/to/sample.wav")
empty_id = engine.create_buffer(channels=2, length=44100, sample_rate=44100.0, name="recording")
engine.remove_buffer(buf_id)
buffers = engine.buffers          # property, list of (id, name) tuples
info = engine.buffer_info(buf_id) # BufferInfo namedtuple
```

### FFI Orchestration

Buffer assignment to sources is orchestrated by the FFI layer:

```cpp
bool sq_assign_buffer(SqEngine handle, SqSource src, int buffer_id, char** error) {
    Buffer* buf = handle->bufferLibrary.getBuffer(buffer_id);
    if (!buf) { /* set error, return false */ }
    Source* source = static_cast<Source*>(src);
    Processor* gen = source->getGenerator();
    if (!gen) { /* set error, return false */ }
    // gen->setBuffer(buf) or equivalent via parameter system
    return true;
}
```

## Invariants

- Buffer IDs are monotonically increasing, starting from 1, and never reused
- `getNumBuffers()` returns 0 before any buffers are loaded/created
- `loadBuffer()` reads the entire file into RAM — no disk streaming
- `loadBuffer()` stores at native file sample rate — no sample rate conversion
- Buffer names: file basename (without path) for file-loaded, user-supplied for `createBuffer()`
- `removeBuffer()` returns the `unique_ptr<Buffer>` — the caller controls when it is actually destroyed (deferred deletion)
- `getBuffer()` returns nullptr for unknown IDs
- After `removeBuffer(id)`, `getBuffer(id)` returns nullptr
- `getBuffers()` returns entries sorted by ID (ascending)

## Error Conditions

- `loadBuffer()` with nonexistent file: returns -1, sets error
- `loadBuffer()` with unsupported format: returns -1, sets error
- `loadBuffer()` with corrupted file: returns -1, sets error
- `createBuffer()` with numChannels <= 0: returns -1, sets error
- `createBuffer()` with lengthInSamples <= 0: returns -1, sets error
- `createBuffer()` with sampleRate <= 0: returns -1, sets error
- `removeBuffer()` with unknown ID: returns nullptr
- `getBufferName()` with unknown ID: returns empty string

## Does NOT Handle

- **Assigning buffers to sources** — FFI layer orchestrates (get Buffer*, pass to generator)
- **Deferred deletion timing** — caller decides when to destroy the returned `unique_ptr`
- **Buffer playback or DSP** — SamplerNode / SamplerVoice
- **Sample rate conversion** — stores at native rate; sampler processors handle rate matching
- **Disk streaming** — entire file loaded to RAM
- **Buffer editing** — buffers are immutable after creation (recording uses atomic write position in Buffer)

## Dependencies

- Buffer (the data type managed by BufferLibrary)
- JUCE (`juce_audio_formats`: AudioFormatManager, AudioFormatReader)

## Thread Safety

| Method | Thread | Notes |
|--------|--------|-------|
| `loadBuffer()` | Control | Blocks (disk I/O) — never call from audio thread |
| `createBuffer()` | Control | Allocates memory |
| `removeBuffer()` | Control | Returns ownership — caller handles deferred deletion |
| `getBuffer()` | Control | Read-only lookup; returned pointer valid until `removeBuffer()` |
| `getBufferName()` / `getBuffers()` / `getNumBuffers()` | Control | Read-only |

All BufferLibrary methods are called from the control thread. The FFI layer serializes access through `controlMutex_`. Audio-thread access to Buffer data is through the `Buffer*` pointer obtained before processing begins — the audio thread never calls BufferLibrary directly.

## Example Usage

### C ABI

```c
char* error = NULL;
SqEngine engine = sq_create(44100.0, 512, &error);

// Load a sample
int kick = sq_load_buffer(engine, "/samples/kick.wav", &error);
if (kick < 0) {
    fprintf(stderr, "Load failed: %s\n", error);
    sq_free_string(error);
}

// Create an empty buffer for recording
int rec = sq_create_buffer(engine, 2, 44100 * 60, 44100.0, "recording", &error);

// List all buffers
SqIdNameList bufs = sq_buffers(engine);
for (int i = 0; i < bufs.count; i++) {
    printf("Buffer %d: %s\n", bufs.ids[i], bufs.names[i]);
}
sq_free_id_name_list(bufs);

// Remove a buffer
sq_remove_buffer(engine, kick);

sq_destroy(engine);
```

### Python

```python
from squeeze import Squeeze

s = Squeeze()

kick = s.load_buffer("/samples/kick.wav")
rec = s.create_buffer(channels=2, length=44100 * 60,
                      sample_rate=44100.0, name="recording")

for buf_id, name in s.buffers:
    print(f"Buffer {buf_id}: {name}")

s.remove_buffer(kick)
s.close()
```
