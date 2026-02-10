# DSP Libraries: Value Proposition

How much code do external DSP libraries save us from writing? This document maps every major DSP category an audio engine needs, estimates the from-scratch effort, and identifies which libraries cover it.

See [DSPLibraries.md](DSPLibraries.md) for detailed library descriptions, licenses, and integration notes.

---

## Functionality Coverage Map

| Category | From Scratch (est.) | Covered By | Coverage |
|----------|---------------------|------------|----------|
| **Filters** (LP/HP/BP/SVF/ladder/comb/EQ) | Weeks | FAUST, DaisySP, SoundPipe, chowdsp | Complete |
| **Dynamics** (compressor/limiter/gate) | 1-2 weeks | FAUST, DaisySP, SoundPipe | Complete |
| **Reverb** (algorithmic: plate/hall/room) | 2-4 weeks | FAUST, SoundPipe, STK | Complete |
| **Convolution reverb** | 1-2 weeks | FAUST, chowdsp | Complete |
| **Delay** (basic/ping-pong/tape/multi-tap) | 1 week | FAUST, signalsmith-dsp, SoundPipe | Complete |
| **Modulation** (chorus/flanger/phaser/trem) | 1-2 weeks | FAUST, DaisySP, SoundPipe | Complete |
| **Distortion** (OD/fuzz/bitcrush/waveshape) | 1 week | FAUST, DaisySP, SoundPipe | Complete |
| **Analog circuit modeling** (tube/pedal) | Months | chowdsp_wdf, RTNeural | Partial-Complete |
| **Pitch shift / time stretch** | 2-4 weeks | signalsmith-stretch, Rubber Band | Complete |
| **Oscillators** (BL saw/sq/tri/FM/wavetable) | 1-2 weeks | FAUST, DaisySP, SoundPipe, STK | Complete |
| **Envelopes** (ADSR/AR/follower/LFO) | Days | FAUST, DaisySP, SoundPipe | Complete |
| **Physical models** (strings/brass/flute/drums) | Months-years | STK, DaisySP, FAUST | Complete |
| **Drum synthesis** (analog kick/snare/hat) | 2-3 weeks | DaisySP | Complete |
| **Granular synthesis** | 2-4 weeks | DaisySP, FAUST | Partial-Complete |
| **Spectral** (FFT/vocoder/freeze) | 2-4 weeks | FAUST, signalsmith-dsp | Partial |
| **Pitch detection / analysis** | 1-2 weeks | Q (Cycfi) | Complete |
| **Neural audio** (amp sims/learned FX) | Months | RTNeural | Complete (inference) |
| **Utilities** (DC block, SRC, pan, crossfade) | Days | signalsmith-dsp, SoundPipe | Complete |

---

## Effort Summary

### From scratch: 12-24+ months of DSP development

Broken down:

- **Filters + dynamics + reverb + delay + modulation + distortion + oscillators + envelopes + utilities:** 3-4 months for production-quality implementations. FAUST alone covers all of this.
- **Physical modeling:** 6-12+ months of specialized research and implementation. STK gives 30 years of CCRMA research for free.
- **Pitch/time stretch:** A quality phase vocoder is 1-2 months minimum. signalsmith-stretch is a single header file.
- **Neural inference:** Building an RT-safe inference engine is a project unto itself. RTNeural exists.
- **Analog circuit modeling via WDF:** Deep academic domain. chowdsp_wdf encapsulates years of PhD-level work.

### With libraries: ~50-100 lines of wrapper code per node

The real work becomes choosing which algorithms to expose and designing the parameter interface — not implementing the DSP.

---

## What Libraries Don't Cover

The libraries provide the "math inside the boxes." Everything else is ours to build:

| Concern | Status |
|---------|--------|
| Node/graph/engine infrastructure | Done |
| Audio I/O and device management | Done (JUCE) |
| MIDI routing and channel filtering | Done |
| Lua scripting layer | Done |
| Performance monitoring | Done |
| Parameter mapping and UI | To build |
| Preset management (save/load parameter states) | To build |
| MIDI-to-parameter routing (CC mapping, learn mode) | To build |
| Metering / visualization (spectrum, waveforms) | To build |
| Session state (which nodes, how connected) | To build |

---

## Key Insight

The architecture we've already built — nodes, graph, scheduler, engine, Lua bindings, MIDI routing, parameters — is the hard infrastructure work. The DSP libraries fill the remaining gap: the signal processing that happens when audio flows through a node. That's the part that takes domain expertise and months of careful implementation to get right, and it's exactly what these libraries provide for free.
