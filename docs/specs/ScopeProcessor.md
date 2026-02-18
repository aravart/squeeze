# ScopeProcessor Specification

## Responsibilities

- Inline read-only audio tap that passes audio through unchanged
- Captures audio data into a lock-free ring buffer for control/UI thread consumption
- Provides time-domain waveform data (oscilloscope)
- Provides frequency-domain spectrum data (FFT magnitudes)

## Overview

ScopeProcessor is a Processor subclass that acts as a transparent audio tap. Inserted into any Source or Bus chain, it passes audio through unmodified while capturing samples into a lock-free ring buffer. The control/UI thread can then read waveform (time-domain) and spectrum (frequency-domain) data without blocking the audio thread. This is the data provider for oscilloscopes, spectrum analyzers, and other visualization widgets.

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

`sq_add_scope` creates the ScopeProcessor but does not insert it into any chain. The caller uses `sq_source_append_proc` / `sq_bus_append_proc` to place a pre-created processor into a chain (see note below). `sq_remove_scope` removes and destroys it.

**Chain insertion for built-in processors:** The standard `sq_source_append` takes a `plugin_path` string and creates a PluginProcessor. Built-in processors like ScopeProcessor and RecordingProcessor are created via their own `sq_add_*` functions and inserted via `sq_source_append_proc` / `sq_bus_append_proc`, which accept an existing `SqProc` handle:

```c
SqProc sq_source_append_proc(SqEngine engine, SqSource src, SqProc proc);
SqProc sq_bus_append_proc(SqEngine engine, SqBus bus, SqProc proc);
```

### Python

```python
class Scope:
    """Read-only audio analysis tap. Insert into any chain."""

    @property
    def buffer_size(self) -> int: ...

    def waveform(self, max_samples: int | None = None) -> list[float]: ...
    def spectrum(self, max_bins: int | None = None) -> list[float]: ...
```

Created via `s.add_scope(buffer_size=2048)`, inserted via `source.chain.append(scope)` or `bus.chain.append(scope)`. The Chain `append`/`insert` methods accept either a plugin path string or a pre-created Processor/Scope object.

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
from squeeze import Squeeze

s = Squeeze(sample_rate=44100, block_size=128)

synth = s.add_source("Lead", plugin="Diva.vst3")
synth.route_to(s.master)

# Add scope to the source's insert chain (post-effects)
scope = s.add_scope(buffer_size=2048)
synth.chain.append(scope)

s.start()

# Poll from control thread / UI loop
waveform = scope.waveform()          # 2048 floats, time-domain
spectrum = scope.spectrum()          # 1024 floats, frequency-domain

# Or with limits
waveform = scope.waveform(max_samples=512)   # most recent 512 samples
spectrum = scope.spectrum(max_bins=256)       # lowest 256 frequency bins
```
