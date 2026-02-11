# Buffer Specification

## Responsibilities

- Hold audio sample data in memory (interleaved channels, float samples)
- Provide factory construction from audio files or as empty/zeroed allocations
- Expose read/write pointers for audio-thread access without locking
- Track recording progress via an atomic write position
- Support clearing and resizing from the control thread

## Interface

### Factory Methods (control thread only)

```cpp
// Load an audio file into a new Buffer. Returns nullptr on failure.
static std::unique_ptr<Buffer> loadFromFile(
    const std::string& filePath,
    juce::AudioFormatManager& formatManager,
    std::string& errorMessage);

// Create a zeroed buffer. Returns nullptr for invalid parameters.
static std::unique_ptr<Buffer> createEmpty(
    int numChannels,
    int lengthInSamples,
    double sampleRate,
    const std::string& name = "");
```

### Audio Data Access (audio-thread safe)

```cpp
const juce::AudioBuffer<float>& getAudioData() const;
juce::AudioBuffer<float>& getAudioData();
const float* getReadPointer(int channel) const;
float* getWritePointer(int channel);
```

### Metadata (immutable after construction)

```cpp
int getNumChannels() const;
int getLengthInSamples() const;
double getSampleRate() const;
double getLengthInSeconds() const;      // lengthInSamples / sampleRate
const std::string& getName() const;     // filename for loaded files, user-supplied for empty
const std::string& getFilePath() const; // empty string for createEmpty buffers
```

### Recording Support

```cpp
std::atomic<int> writePosition{0};  // public, samples from buffer start

void clear();                                              // zero all samples, reset writePosition to 0
void resize(int numChannels, int newLengthInSamples);      // preserves existing data up to min(old, new) length
```

### Construction

The default constructor is private. Buffers can only be created through the two factory methods.

## Invariants

1. Metadata (sampleRate, name, filePath) is immutable after construction.
2. A file-loaded buffer has `writePosition == lengthInSamples` (fully written).
3. An empty buffer has `writePosition == 0` and all samples zeroed.
4. Read/write pointers for a given channel are stable between calls (same address) unless `resize()` is called.
5. `clear()` resets both audio data and writePosition atomically (with respect to the writePosition field; audio zeroing is not atomic).
6. `resize()` preserves existing sample data up to `min(oldLength, newLength)`. New space is zeroed.
7. `createEmpty()` requires `numChannels >= 1`, `lengthInSamples >= 1`, `sampleRate > 0.0`.

## Error Conditions

| Condition | Behavior |
|-----------|----------|
| `loadFromFile` — file not found | Returns nullptr, sets errorMessage |
| `loadFromFile` — unsupported/corrupted format | Returns nullptr, sets errorMessage |
| `loadFromFile` — read failure | Returns nullptr, sets errorMessage |
| `createEmpty` — numChannels < 1 | Returns nullptr |
| `createEmpty` — lengthInSamples < 1 | Returns nullptr |
| `createEmpty` — sampleRate <= 0 | Returns nullptr |

## Does NOT Handle

- Disk streaming — entire file is loaded into RAM
- Sample rate conversion — data is stored at the file's native rate
- Ownership or lifetime management — that is the Engine's responsibility
- Thread-safe resize — `resize()` and `clear()` are control-thread only
- Interleaved/planar conversion — uses JUCE's channel-per-pointer layout

## Dependencies

- `juce_audio_basics` — `juce::AudioBuffer<float>`
- `juce_audio_formats` — `juce::AudioFormatManager`, `juce::AudioFormatReader`
- `core/Logger.h` — `SQ_LOG` macro for load/create logging

## Thread Safety

| Operation | Thread | Notes |
|-----------|--------|-------|
| `loadFromFile()` | Control | Allocates; called under Engine controlMutex_ |
| `createEmpty()` | Control | Allocates; called under Engine controlMutex_ |
| `clear()` | Control | Mutates data and writePosition |
| `resize()` | Control | May reallocate; NOT audio-thread safe |
| `getReadPointer()` | Audio or Control | No allocation, no locking |
| `getWritePointer()` | Audio or Control | No allocation, no locking |
| `getAudioData()` | Audio or Control | Returns reference, no locking |
| Metadata getters | Any | Immutable after construction |
| `writePosition` load | Any | Atomic; use acquire ordering for reads |
| `writePosition` store | Audio | Atomic; use release ordering for writes |

The Buffer itself has no internal mutex. Concurrency safety comes from:
- Engine's `controlMutex_` serializing all control-plane operations
- Atomic `writePosition` for audio→control thread signaling
- Deferred deletion in Engine ensuring audio thread never sees a dangling pointer

## Engine Integration

Engine manages all Buffer instances through an `ownedBuffers_` map with monotonically increasing integer IDs (never reused).

```cpp
int loadBuffer(const std::string& filePath, std::string& errorMessage);
int createBuffer(int numChannels, int lengthInSamples, double sampleRate,
                 const std::string& name, std::string& errorMessage);
bool removeBuffer(int id);
Buffer* getBuffer(int id) const;
std::string getBufferName(int id) const;
std::vector<std::pair<int, std::string>> getBuffers() const;
bool setSamplerBuffer(int nodeId, int bufferId);
```

**Deferred deletion**: `removeBuffer()` nulls all SamplerNode references to the buffer, then moves the `unique_ptr` to `pendingBufferDeletions_` rather than destroying immediately. This ensures the audio thread never dereferences a freed buffer.

## Lua API

```lua
sq.load_buffer(filePath)                                    --> buffer_id | nil, error
sq.create_buffer(channels, length, sampleRate [, name])     --> buffer_id | nil, error
sq.remove_buffer(id)                                        --> true | nil, error
sq.buffers()                                                --> {{id=N, name="..."}, ...}
sq.buffer_info(id)                                          --> {name, channels, length, sample_rate, file_path, length_seconds} | nil, error
sq.set_sampler_buffer(nodeId, bufferId)                     --> true | nil, error
```

## Example Usage

```cpp
// Load from file
juce::AudioFormatManager afm;
afm.registerBasicFormats();
std::string err;
auto buf = Buffer::loadFromFile("/path/to/kick.wav", afm, err);
if (!buf) { /* handle err */ }

assert(buf->getNumChannels() == 2);
assert(buf->writePosition.load() == buf->getLengthInSamples());

// Create empty for recording
auto rec = Buffer::createEmpty(2, 44100 * 10, 44100.0, "loop");
assert(rec->writePosition.load() == 0);

// Audio thread reads
const float* L = buf->getReadPointer(0);
const float* R = buf->getReadPointer(1);

// Recording (audio thread writes, control thread reads position)
float* dst = rec->getWritePointer(0);
dst[pos] = sample;
rec->writePosition.store(pos + 1, std::memory_order_release);

// Via Engine
int id = engine.loadBuffer("/path/to/snare.wav", err);
engine.setSamplerBuffer(samplerNodeId, id);
engine.removeBuffer(id);  // deferred deletion, nulls sampler references
```
