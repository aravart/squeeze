# ScopeProcessor Specification

## Responsibilities

- Inline read-only audio tap that passes audio through unchanged
- Captures audio data into a lock-free ring buffer for control/UI thread consumption
- Provides time-domain waveform data (oscilloscope)
- Provides frequency-domain spectrum data (FFT magnitudes)

## Interface

### C++ (`ScopeProcessor : Processor`)

```cpp
class ScopeProcessor : public Processor {
public:
    explicit ScopeProcessor(int bufferSize = 2048);

    // Processor interface
    void prepare(double sampleRate, int blockSize) override;
    void process(AudioBuffer& buffer) override;
    int getParameterCount() const override;           // 0
    ParamDescriptor getParameterDescriptor(int index) const override;
    float getParameter(const std::string& name) const override;
    void setParameter(const std::string& name, float value) override;
    int getLatencySamples() const override;            // always 0

    // Scope-specific (control thread only)
    int getWaveform(float* out, int maxSamples) const;
    int getSpectrum(float* out, int maxBins) const;
    int getBufferSize() const;
};
```

- `bufferSize` — number of samples stored in the ring buffer. Must be a power of two. Determines both the waveform history length and the FFT resolution.
- `getWaveform()` — copies the most recent `min(bufferSize, maxSamples)` samples (mono: channel 0, or summed L+R) into `out`. Returns the number of samples written.
- `getSpectrum()` — performs an FFT on the current ring buffer contents and writes `min(bufferSize/2, maxBins)` magnitude values (linear scale, 0.0–1.0 normalized to peak) into `out`. Returns the number of bins written.

### C ABI

```c
SqScope  sq_add_scope(SqEngine engine, int buffer_size);
void     sq_remove_scope(SqEngine engine, SqScope scope);
int      sq_scope_get_waveform(SqEngine engine, SqScope scope, float* out, int max_samples);
int      sq_scope_get_spectrum(SqEngine engine, SqScope scope, float* out, int max_bins);
int      sq_scope_get_buffer_size(SqEngine engine, SqScope scope);
```

`sq_add_scope` creates the ScopeProcessor but does not insert it into any chain. The caller uses `sq_source_append` / `sq_bus_append` (or insert variants) to place it. `sq_remove_scope` removes and destroys it.

### Python (low-level)

```python
class Squeeze:
    def add_scope(self, buffer_size: int = 2048) -> int: ...
    def remove_scope(self, scope: int) -> None: ...
    def scope_get_waveform(self, scope: int, max_samples: int) -> list[float]: ...
    def scope_get_spectrum(self, scope: int, max_bins: int) -> list[float]: ...
    def scope_get_buffer_size(self, scope: int) -> int: ...
```

### Python (high-level)

```python
class Scope:
    """Read-only audio analysis tap. Insert into any chain."""
    buffer_size: int          # read-only, set at construction

    def waveform(self, max_samples: int | None = None) -> list[float]: ...
    def spectrum(self, max_bins: int | None = None) -> list[float]: ...
```

Created via `engine.add_scope(buffer_size=2048)`, inserted via `source.chain.append(scope)` or `bus.chain.append(scope)`.

## Invariants

- Audio passes through unmodified — output buffer is identical to input buffer
- `getLatencySamples()` always returns 0
- `getParameterCount()` returns 0 (no user-facing parameters)
- `bufferSize` is always a power of two (enforced at construction; rounds up if not)
- Ring buffer is lock-free: audio thread writes, control thread reads, no contention
- `getWaveform()` and `getSpectrum()` never block the audio thread
- `getSpectrum()` performs the FFT on the calling (control) thread, not on the audio thread
- Waveform data is mono (left channel, or summed L+R / channel count for stereo+ buffers)
- Spectrum magnitudes are linear scale, normalized to peak (0.0–1.0)

## Error Conditions

- `bufferSize <= 0` — clamp to minimum (64)
- `bufferSize` not power of two — round up to next power of two
- `getWaveform()` / `getSpectrum()` called before `prepare()` — returns 0 (no data)
- `out` is null — returns 0
- `maxSamples` / `maxBins` <= 0 — returns 0
- Invalid `SqScope` handle — returns 0 / no-op

## Does NOT Handle

- Spectrogram (FFT over time) — future extension, not in this spec
- Phase scope / Lissajous display — future extension
- Windowing function selection — uses Hann window, not configurable
- Per-channel waveform (multi-channel) — always reduces to mono
- dB-scale spectrum output — caller converts if needed
- Display rendering — this is a data provider, not a UI component

## Dependencies

- `Processor` base class
- Lock-free ring buffer (JUCE `AbstractFifo` or equivalent single-producer single-consumer ring)
- FFT implementation (JUCE `dsp::FFT`)

## Thread Safety

- **Audio thread** calls `process()` — writes samples into the ring buffer. No allocation, no blocking.
- **Control thread** calls `getWaveform()` and `getSpectrum()` — reads from the ring buffer. The FFT for `getSpectrum()` runs on the control thread.
- The ring buffer is the synchronization boundary. Single-producer (audio), single-consumer (control). No mutexes.

## Example Usage

```python
from squeeze import Engine

engine = Engine(sample_rate=44100, block_size=128)

synth = engine.add_source("Lead", plugin="Diva.vst3")
synth.route_to(engine.master)

# Add scope to the source's insert chain (post-effects)
scope = engine.add_scope(buffer_size=2048)
synth.chain.append(scope)

engine.start()

# Poll from control thread / UI loop
waveform = scope.waveform()          # 2048 floats, time-domain
spectrum = scope.spectrum()          # 1024 floats, frequency-domain

# Or with limits
waveform = scope.waveform(max_samples=512)   # most recent 512 samples
spectrum = scope.spectrum(max_bins=256)       # lowest 256 frequency bins
```
