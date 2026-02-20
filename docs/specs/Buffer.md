# Buffer Specification

## Responsibilities

- Hold audio sample data in memory (channel-per-pointer layout via `juce::AudioBuffer<float>`)
- Provide factory construction for empty buffers and from raw audio data
- Expose per-channel read/write pointers for audio-thread access without locking
- Track recording progress via an atomic write position
- Support clearing from the control thread

Buffer is a **pure data container** with no file I/O and no dependency on other squeeze components. File loading is BufferLibrary's responsibility (tier 21). BufferLibrary constructs Buffers from loaded audio data via `createFromData`.

## Interface

### C++ (`squeeze::Buffer`)

```cpp
namespace squeeze {

class Buffer {
public:
    /// Create a zeroed buffer for recording or programmatic use.
    /// Returns nullptr for invalid parameters.
    static std::unique_ptr<Buffer> createEmpty(
        int numChannels, int lengthInSamples, double sampleRate,
        const std::string& name = "");

    /// Create a buffer from existing audio data (used by BufferLibrary after file load).
    /// Takes ownership of `data` via move. Returns nullptr for invalid parameters.
    static std::unique_ptr<Buffer> createFromData(
        juce::AudioBuffer<float>&& data, double sampleRate,
        const std::string& name, const std::string& filePath = "");

    ~Buffer();

    // Non-copyable, non-moveable (managed via unique_ptr; contains atomic)
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) = delete;
    Buffer& operator=(Buffer&&) = delete;

    // --- Audio data access (RT-safe, lock-free) ---

    /// Returns read pointer for channel, or nullptr if channel is out of range.
    const float* getReadPointer(int channel) const;

    /// Returns write pointer for channel, or nullptr if channel is out of range.
    float* getWritePointer(int channel);

    // --- Metadata (immutable after construction) ---

    int getNumChannels() const;
    int getLengthInSamples() const;
    double getSampleRate() const;
    double getLengthInSeconds() const;     // lengthInSamples / sampleRate
    const std::string& getName() const;
    const std::string& getFilePath() const; // empty for createEmpty buffers

    // --- Mutable metadata (control-thread only) ---
    double getTempo() const;     // BPM; 0.0 = not set
    void setTempo(double bpm);

    // --- Recording ---

    /// Current write position (samples from buffer start).
    /// Audio thread stores with release; control thread loads with acquire.
    std::atomic<int> writePosition{0};

    /// Zero all sample data and reset writePosition to 0. Control thread only.
    void clear();

private:
    Buffer(); // only accessible to factories

    juce::AudioBuffer<float> data_;
    double sampleRate_ = 0.0;
    std::string name_;
    std::string filePath_;
    double tempo_ = 0.0;
};

} // namespace squeeze
```

### C ABI (`squeeze_ffi.h`)

At tier 16, the FFI layer maintains a simple buffer registry (monotonically increasing IDs starting at 1, never reused). At tier 21 (BufferLibrary), this registry is replaced by `BufferLibrary` which adds file loading.

```c
/* ── Buffer management ────────────────────────────────────────── */

/// Create an empty buffer. Returns buffer ID (>= 1), or -1 on failure (sets *error).
int sq_create_buffer(SqEngine engine, int num_channels, int length_in_samples,
                     double sample_rate, const char* name, char** error);

/// Remove a buffer by ID. Returns false if not found.
bool sq_remove_buffer(SqEngine engine, int buffer_id);

/// Returns the number of buffers.
int sq_buffer_count(SqEngine engine);

/* ── Buffer queries ───────────────────────────────────────────── */

/// Returns number of channels, or 0 if buffer not found.
int sq_buffer_num_channels(SqEngine engine, int buffer_id);

/// Returns length in samples, or 0 if buffer not found.
int sq_buffer_length(SqEngine engine, int buffer_id);

/// Returns sample rate, or 0.0 if buffer not found.
double sq_buffer_sample_rate(SqEngine engine, int buffer_id);

/// Returns buffer name. Caller must sq_free_string(). NULL if not found.
char* sq_buffer_name(SqEngine engine, int buffer_id);

/// Returns length in seconds, or 0.0 if buffer not found.
double sq_buffer_length_seconds(SqEngine engine, int buffer_id);

/// Returns the current write position, or -1 if buffer not found.
int sq_buffer_write_position(SqEngine engine, int buffer_id);

/// Set the write position. No-op if buffer not found.
void sq_buffer_set_write_position(SqEngine engine, int buffer_id, int position);

/// Returns buffer tempo (BPM), or 0.0 if buffer not found.
double sq_buffer_tempo(SqEngine engine, int buffer_id);

/// Set buffer tempo (BPM). No-op if buffer not found.
void sq_buffer_set_tempo(SqEngine engine, int buffer_id, double bpm);

/* ── Buffer sample data ───────────────────────────────────────── */

/// Read samples from a buffer into dest.
/// Returns number of samples actually read (0 on error or out-of-range).
/// Reads min(num_samples, length - offset) samples.
int sq_buffer_read(SqEngine engine, int buffer_id, int channel,
                   int offset, float* dest, int num_samples);

/// Write samples from src into a buffer.
/// Returns number of samples actually written (0 on error or out-of-range).
/// Writes min(num_samples, length - offset) samples.
int sq_buffer_write(SqEngine engine, int buffer_id, int channel,
                    int offset, const float* src, int num_samples);

/// Zero all samples and reset write position to 0. No-op if buffer not found.
void sq_buffer_clear(SqEngine engine, int buffer_id);
```

### Python API

```python
class Buffer:
    """A handle to an audio buffer in the engine."""

    @property
    def buffer_id(self) -> int: ...

    @property
    def num_channels(self) -> int: ...

    @property
    def length(self) -> int: ...

    @property
    def sample_rate(self) -> float: ...

    @property
    def name(self) -> str: ...

    @property
    def length_seconds(self) -> float: ...

    @property
    def write_position(self) -> int: ...

    @write_position.setter
    def write_position(self, pos: int) -> None: ...

    @property
    def tempo(self) -> float: ...   # BPM; 0.0 = not set

    @tempo.setter
    def tempo(self, bpm: float) -> None: ...

    def read(self, channel: int, offset: int = 0,
             num_samples: int = -1) -> list[float]:
        """Read samples from the buffer.
        num_samples=-1 reads from offset to end."""

    def write(self, channel: int, data: list[float],
              offset: int = 0) -> int:
        """Write samples into the buffer. Returns number written."""

    def clear(self) -> None:
        """Zero all samples and reset write_position to 0."""

    def remove(self) -> bool:
        """Remove this buffer from the engine."""

# On Squeeze:
def create_buffer(self, channels: int, length: int,
                  sample_rate: float, name: str = "") -> Buffer: ...

@property
def buffer_count(self) -> int: ...
```

## Invariants

1. Metadata (`sampleRate`, `name`, `filePath`) is immutable after construction. Tempo defaults to 0.0 (not set) and is mutable via `setTempo()`, control-thread only.
2. `createEmpty` returns a buffer with `writePosition == 0` and all samples zeroed.
3. `createFromData` returns a buffer with `writePosition == lengthInSamples` (fully written).
4. Read/write pointers for a given channel are stable (same address) for the lifetime of the Buffer.
5. `clear()` zeroes all sample data and resets `writePosition` to 0.
6. `createEmpty()` requires `numChannels >= 1`, `lengthInSamples >= 1`, `sampleRate > 0.0`.
7. `getReadPointer(ch)` / `getWritePointer(ch)` return nullptr for out-of-range channels.
8. `getLengthInSeconds()` equals `getLengthInSamples() / getSampleRate()`.
9. Buffer IDs at the FFI level are monotonically increasing, starting from 1, never reused.
10. `createFromData` with a zero-length or zero-channel `AudioBuffer` returns nullptr.

## Error Conditions

| Condition | Behavior |
|-----------|----------|
| `createEmpty` — `numChannels < 1` | Returns nullptr |
| `createEmpty` — `lengthInSamples < 1` | Returns nullptr |
| `createEmpty` — `sampleRate <= 0.0` | Returns nullptr |
| `createFromData` — zero-length AudioBuffer | Returns nullptr |
| `createFromData` — zero-channel AudioBuffer | Returns nullptr |
| `createFromData` — `sampleRate <= 0.0` | Returns nullptr |
| `getReadPointer` — channel out of range | Returns nullptr |
| `getWritePointer` — channel out of range | Returns nullptr |
| `sq_create_buffer` — invalid params | Returns -1, sets `*error` |
| `sq_remove_buffer` — unknown ID | Returns false |
| `sq_buffer_read` — invalid ID, channel, or offset | Returns 0 |
| `sq_buffer_write` — invalid ID, channel, or offset | Returns 0 |
| `sq_buffer_*` query — unknown ID | Returns 0 / 0.0 / NULL as appropriate |

## Does NOT Handle

- **File loading** — BufferLibrary (tier 21) handles file I/O and format detection
- **Sample rate conversion** — data stored at native rate; consumers handle rate matching
- **Disk streaming** — entire buffer in RAM
- **Lifetime management policy** — the FFI / BufferLibrary decides when to destroy
- **Deferred deletion** — caller manages timing (critical when audio thread holds a `const Buffer*`)
- **Thread-safe clear** — `clear()` is control-thread-only; caller must ensure audio thread is not reading
- **Resize** — buffers are fixed-size after creation; pre-allocate with `createEmpty` for recording

## Dependencies

- `juce_audio_basics` — `juce::AudioBuffer<float>`
- `core/Logger.h` — `SQ_INFO`, `SQ_DEBUG`, `SQ_WARN` macros

No dependencies on other squeeze components.

## Thread Safety

| Operation | Thread | Notes |
|-----------|--------|-------|
| `createEmpty()` | Control | Allocates memory |
| `createFromData()` | Control | Moves audio data, allocates |
| `clear()` | Control | Mutates data and writePosition; caller must ensure no concurrent readers |
| `getReadPointer()` | Any | No allocation, no locking; RT-safe |
| `getWritePointer()` | Any | No allocation, no locking; RT-safe |
| Metadata getters | Any | Immutable after construction; RT-safe |
| `getTempo()` / `setTempo()` | Control | Mutable metadata; no atomics needed |
| `writePosition` load | Any | Atomic; use `std::memory_order_acquire` |
| `writePosition` store | Audio | Atomic; use `std::memory_order_release` |

Buffer has **no internal mutex**. Thread safety comes from:

- External serialization of control operations (FFI controlMutex)
- Atomic `writePosition` for audio-to-control thread signaling
- Pointer stability: channel data pointers never change after construction
- Deferred deletion: callers must ensure the audio thread is not accessing a Buffer before destroying it

### Recording data flow

The audio thread writes samples and advances `writePosition`; the control thread reads `writePosition` to track progress:

```
Audio thread:                         Control thread:
  float* dst = getWritePointer(0);      int pos = writePosition.load(acquire);
  dst[pos] = sample;                    // pos reflects audio thread progress
  writePosition.store(pos+1, release);
```

Samples at indices `< writePosition` are safe to read from the control thread. Samples at indices `>= writePosition` may be in-flight or zeroed.

## Example Usage

### C++

```cpp
// Create empty buffer for recording (10 seconds, stereo, 44.1 kHz)
auto buf = Buffer::createEmpty(2, 441000, 44100.0, "loop");
assert(buf->getNumChannels() == 2);
assert(buf->getLengthInSamples() == 441000);
assert(buf->writePosition.load() == 0);

// Audio thread reads
const float* L = buf->getReadPointer(0);
const float* R = buf->getReadPointer(1);

// Recording (audio thread writes)
float* dst = buf->getWritePointer(0);
dst[pos] = sample;
buf->writePosition.store(pos + 1, std::memory_order_release);

// Control thread reads recording progress
int written = buf->writePosition.load(std::memory_order_acquire);

// Clear for re-recording
buf->clear();
assert(buf->writePosition.load() == 0);

// Create from loaded audio data (BufferLibrary would call this)
juce::AudioBuffer<float> loaded(2, 44100);
// ... file reader fills `loaded` ...
auto kick = Buffer::createFromData(std::move(loaded), 44100.0, "kick.wav", "/samples/kick.wav");
assert(kick->writePosition.load() == kick->getLengthInSamples());
```

### C ABI

```c
char* error = NULL;
SqEngine e = sq_engine_create(44100.0, 512, &error);

// Create an empty buffer
int buf = sq_create_buffer(e, 2, 44100, 44100.0, "test", &error);
assert(buf >= 1);

// Query metadata
assert(sq_buffer_num_channels(e, buf) == 2);
assert(sq_buffer_length(e, buf) == 44100);

// Write a ramp into channel 0
float ramp[44100];
for (int i = 0; i < 44100; i++) ramp[i] = (float)i / 44100.0f;
int written = sq_buffer_write(e, buf, 0, 0, ramp, 44100);
assert(written == 44100);

// Read back
float dest[100];
int nread = sq_buffer_read(e, buf, 0, 0, dest, 100);
assert(nread == 100);

// Clear and remove
sq_buffer_clear(e, buf);
assert(sq_buffer_write_position(e, buf) == 0);
sq_remove_buffer(e, buf);

sq_engine_destroy(e);
```

### Python

```python
from squeeze import Squeeze
import math

with Squeeze() as s:
    buf = s.create_buffer(channels=2, length=44100, sample_rate=44100.0, name="sine")
    print(f"{buf.name}: {buf.num_channels}ch, {buf.length} samples, {buf.length_seconds:.2f}s")

    # Write a sine wave
    sine = [math.sin(2 * math.pi * 440 * i / 44100) for i in range(44100)]
    buf.write(channel=0, data=sine)
    buf.write(channel=1, data=sine)

    # Read back
    samples = buf.read(channel=0, num_samples=5)
    print(f"First 5: {[f'{x:.4f}' for x in samples]}")

    # Clear
    buf.clear()
    assert buf.write_position == 0

    buf.remove()
    print(f"Buffers remaining: {s.buffer_count}")
```
