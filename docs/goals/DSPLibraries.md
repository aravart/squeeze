# DSP Libraries for Node Integration

Squeeze's Node abstraction (`processBlock(AudioBuffer, MidiBuffer)`) can wrap external DSP libraries with minimal glue code. This document surveys libraries that provide industry-standard functionality we don't need to build ourselves.

---

## Tier 1: High-Value, Easy Integration

### FAUST (AOT mode)

- **URL:** https://github.com/grame-cncm/faust
- **License:** GPL compiler, but generated code inherits the license of the input source (i.e., your license)
- **Stars:** ~2,960 | **Last commit:** Feb 2026 | **Actively maintained**

A functional DSP language with a standard library containing reverbs, filters (analog-modeled, biquads, waveguides), delays, oscillators, envelopes, dynamics processors, physical models, spectral processing, granular synthesis, and more — hundreds of ready-to-use algorithms.

**Integration:** Compile `.dsp` files to C++ ahead of time. The generated code exposes `compute(int nframes, float** inputs, float** outputs)` which maps 1:1 to `processBlock`. Generated code has zero dependencies, is fully RT-safe, and is a single C++ file per effect.

**JIT mode** (via libfaust) enables live-coding DSP but pulls in LLVM as a dependency (hundreds of MB). An interpreter backend exists (no LLVM) but is 3-10x slower. There is an existing JUCE integration: [juce_faustllvm](https://github.com/olilarkin/juce_faustllvm).

**Wrapper pattern:** A `FaustNode` subclass that takes a generated `dsp` class, calls `init(sampleRate)` in `prepareToPlay` and `compute()` in `processBlock`.

**Verdict:** The single most powerful option. AOT mode is essentially free to integrate and covers virtually every DSP category.

---

### signalsmith-stretch + signalsmith-dsp

- **URL:** https://github.com/Signalsmith-Audio/signalsmith-stretch / https://github.com/Signalsmith-Audio/dsp
- **License:** MIT
- **Stars:** 438 / — | **Last commit:** Jan 2026 | **Actively maintained**

**signalsmith-stretch:** Polyphonic pitch-shifting and time-stretching. Single header-only C++11 library. Block-based API: `process(inputBuffers, inputSamples, outputBuffers, outputSamples)`. Configure with `setTransposeFactor(ratio)`. Handles polyphonic material well.

**signalsmith-dsp:** Companion library providing delay lines (single/multi-channel), interpolators (Lagrange, polyphase, Kaiser-sinc), envelope followers, FFT, spectral processing (multi-channel STFT), filters, and windows.

**Integration:** Drop `.h` files into the project. Block-based APIs map directly to `processBlock`. Zero dependencies.

**Gotchas:** Best for modest time-stretching (0.75x-1.5x). Extreme stretching degrades more than Rubber Band. Latency depends on configuration.

**Verdict:** The best MIT-licensed time-stretching/pitch-shifting available. Trivial integration.

---

### RTNeural

- **URL:** https://github.com/jatinchowdhury18/RTNeural
- **License:** BSD-3-Clause
- **Stars:** 782 | **Last commit:** Nov 2025 | **Actively maintained**

Real-time neural network inference for audio. Supports Dense, GRU, LSTM, Conv1D layers. Designed for amp modeling, tape saturation, tube distortion, pedal emulation, and learned audio effects. Loads models trained in PyTorch/TensorFlow (exported as JSON).

**Integration:** Header-only. Three backends: Eigen, xsimd, or STL-only. Template-based compile-time model definition for maximum performance, or runtime model loading for flexibility. Per-sample `forward()` call in a loop inside `processBlock`.

**Gotchas:** Models must be trained externally (Python). Eigen backend may allocate during setup (not inference). Inference itself is fully RT-safe.

**Verdict:** The go-to library for neural audio processing. Nothing else fills this niche. Used in production JUCE plugins (AIDA-X, BYOD, Chow Centaur, Chow Tape Model).

---

### DaisySP

- **URL:** https://github.com/electro-smith/DaisySP
- **License:** MIT
- **Stars:** 1,095 | **Last commit:** May 2025

~60 DSP modules originally for the Electrosmith Daisy embedded platform, but fully platform-independent:

- **Synthesis:** Analog-modeled oscillators, FM, additive, subtractive, wavetable
- **Physical modeling:** Karplus-Strong (string, pluck), resonators, modal voice
- **Drums:** Analog bass drum, snare, hi-hat synthesis
- **Effects:** Phaser, wavefolder, decimator, overdrive, chorus, flanger
- **Filters:** SVF, one-pole, ladder, comb
- **Dynamics:** Limiter, compressor, crossfade
- **Envelopes:** AD, ADSR, decay
- **Noise:** White, dust, fractal, particle, grainlet
- **Sampling:** Granular player, looper

**Integration:** Per-sample API: `Init(sampleRate)` / `Process()`. Wrap in a sample loop inside `processBlock`. No dependencies. CMake build support.

**Gotchas:** Designed for embedded (Cortex-M7), so algorithms favor efficiency over maximum quality. No SIMD. Per-sample overhead is minor for simple DSP.

**Verdict:** Excellent breadth for a zero-dependency MIT library. The drum synthesis and physical modeling modules are particularly unique and not found in most other libraries. Fork the modules you need.

---

## Tier 2: Valuable but With Caveats

### chowdsp_utils

- **URL:** https://github.com/Chowdhury-DSP/chowdsp_utils
- **License:** BSD for common modules, **GPLv3 for DSP modules**
- **Stars:** 332 | **Last commit:** Dec 2025 | **Actively maintained**

Native JUCE modules providing filters (SVF, Butterworth, Chebyshev, Elliptic), reverb, compressor, parametric EQ, oscillators, waveshapers, modal DSP, delay lines (multiple interpolation types), convolution, pitch shifting, and SIMD utilities.

**Integration:** The most natural fit — these are literally JUCE modules using `juce::dsp::ProcessSpec` and `juce::dsp::ProcessContextReplacing`. Per-block processing matches our conventions exactly.

**Gotchas:** GPLv3 on the interesting DSP modules (filters, reverb, compressor, EQ, waveshapers). Common/utility modules are BSD. Requires JUCE.

**Related:** [chowdsp_wdf](https://github.com/Chowdhury-DSP/chowdsp_wdf) (BSD-3) — Wave Digital Filter library for analog circuit modeling. Header-only, SIMD support via xsimd. This one is freely usable.

**Verdict:** Architecturally the easiest integration. Production-quality DSP from the same author as RTNeural. GPL is the only concern.

---

### SoundPipe

- **URL:** https://github.com/PaulBatchelor/Soundpipe (archived) / https://git.sr.ht/~pbatch/soundpipe
- **License:** MIT
- **Stars:** 56 | **Status:** Archived Jan 2024

~115 C modules extracted/inspired from Csound: Moog diode ladder, Korg35, Butterworth filters, zita-rev, JC reverb, big reverb, smooth/variable delays, ADSR, compressor, peak limiter, bitcrusher, saturator, vocal tract, Karplus-Strong, band-limited oscillators, FM, and more.

**Integration:** Per-sample C API: `sp_module_create()` / `sp_module_init()` / `sp_module_compute(sp, module, &in, &out)` / `sp_module_destroy()`. Consistent pattern across all 115 modules. Pure C, no dependencies.

**Gotchas:** GitHub repo is archived (read-only). The code works fine but expect no further updates. [AudioKit/SoundpipeAudioKit](https://github.com/AudioKit/SoundpipeAudioKit) is an actively maintained fork but wrapped for Swift. No SIMD. Per-sample processing only.

**Verdict:** A curated collection of classic DSP algorithms with the simplest possible C API. Fork it and own the code. MIT license and zero dependencies make this trivially embeddable.

---

### STK (Synthesis Toolkit)

- **URL:** https://github.com/thestk/stk
- **License:** Custom permissive (similar to MIT, free for commercial use with attribution)
- **Stars:** 1,177 | **Last commit:** Mar 2025

Perry Cook's Synthesis Toolkit from CCRMA. Unmatched physical modeling: bowed string, brass, clarinet, flute, plucked string, sitar, blown bottle, shakers, drums. Also: BLIT-based oscillators, FM synthesis, filters (one-pole, two-pole, biquad, formant), effects (chorus, echo, NRev/JCRev/FreeVerb, pitch shift), envelopes (ADSR, asymptotic).

**Integration:** Per-sample `tick()` API. `StkFrames` buffer class maps to JUCE's `AudioBuffer`. All instruments support MIDI semantics via `noteOn(frequency, amplitude)` / `noteOff(amplitude)` — integrates well with our MIDI routing.

**Gotchas:** Uses its own I/O (RtAudio, RtMidi) — disable at build time, use only DSP classes. Some classes read raw files for wave tables — must pre-load or embed. Patent notice on some Stanford synthesis algorithms. Not SIMD-optimized.

**Verdict:** The gold standard for physical modeling synthesis. 30 years of academic validation. The MIDI instrument interface is a natural fit for our MIDI routing system.

---

### Rubber Band

- **URL:** https://github.com/breakfastquay/rubberband
- **License:** GPL-2 / Commercial
- **Stars:** 700 | **Last commit:** Feb 2025 | **Actively maintained**

Industry-standard time-stretching and pitch-shifting. Two engines: R2 (faster) and R3 (finer quality). Real-time streaming mode with dynamically adjustable ratios.

**Integration:** Block-based `process()` / `retrieve()` API. Inherent latency (~2048 samples in real-time mode). Output block size doesn't match input — needs elastic buffer.

**Gotchas:** GPL-2 or commercial license. Latency. Not purely RT-safe in all modes (offline mode allocates). Optional deps on libsamplerate, FFTW, sleef.

**Verdict:** Higher quality than signalsmith-stretch for extreme time ratios. Use if GPL is acceptable and you need maximum stretch quality.

---

## Tier 3: Situational

| Library | URL | License | Use Case | Notes |
|---------|-----|---------|----------|-------|
| **KFR** | https://github.com/kfrlib/kfr | GPL-2/Commercial | Fastest FFT, SIMD filters, resampling | Requires C++20. Primitives, not effects. |
| **Maximilian** | https://github.com/micknoise/Maximilian | MIT | Quick prototyping | Educational quality. Global state. |
| **Q (Cycfi)** | https://github.com/cycfi/q | MIT | Pitch detection, frequency analysis | Modern C++, actively maintained. |
| **Essentia** | https://github.com/MTG/essentia | AGPL | Music analysis (beat/key/onset/MFCCs) | Analysis not synthesis. AGPL is restrictive. |

---

## Comparison Matrix

| Library | License | API Style | Dependencies | RT-Safe | Integration Effort |
|---------|---------|-----------|-------------|---------|-------------------|
| **FAUST (AOT)** | Free (output) | Per-block `compute()` | None (generated) | Yes | Low |
| **signalsmith-stretch** | MIT | Per-block | None | Yes | Very low |
| **signalsmith-dsp** | MIT | Per-block | None | Yes | Very low |
| **RTNeural** | BSD-3 | Per-sample/block | Optional (Eigen/xsimd) | Yes (inference) | Low |
| **DaisySP** | MIT | Per-sample `Process()` | None | Yes | Low |
| **chowdsp_utils** | BSD/GPLv3 | Per-block (JUCE) | JUCE | Yes | Very low |
| **chowdsp_wdf** | BSD-3 | Per-sample | Optional (xsimd) | Yes | Low |
| **SoundPipe** | MIT | Per-sample C | None | Yes | Low |
| **STK** | Permissive | Per-sample `tick()` | None (disable I/O) | Yes | Medium |
| **Rubber Band** | GPL-2/Commercial | Per-block | Optional | Mostly | Medium |
| **KFR** | GPL-2/Commercial | Per-block | None (C++20) | Yes | Medium |

---

## Recommended Integration Priority

For maximum coverage with minimal effort and no license encumbrance:

1. **FAUST AOT** — broadest DSP coverage from a single integration point. Write a generic `FaustNode` wrapper.
2. **signalsmith-stretch** — drop a header, get pitch-shifting and time-stretching.
3. **DaisySP** — fork desired modules (drum synth, oscillators, granular). Wrap each as a Node.
4. **RTNeural** — when ML-based effects are needed (amp sims, learned effects).

All four are MIT/BSD, RT-safe, and map trivially to `processBlock`. The wrapper pattern is nearly identical for each: a thin Node subclass that delegates to the library's compute function.
