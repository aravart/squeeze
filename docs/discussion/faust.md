❯ You once said this: Faust has a built-in parameter model using "UI primitives":
  - **Inside Faust's signal flow:** audio rate. The parameter variable is just a float that the signal graph reads every sample. If the host happened to write to the float pointer mid-block, the DSP would pick
   it up immediately. But no standard architecture does that.
  - **Audio-rate modulation** is done *within* Faust by wiring signals to signals. You don't modulate a parameter from outside at audio rate — you'd write the modulation into the Faust program itself:
  So the mapping to Squeeze is actually clean: Faust parameters are named, have ranges/defaults, and are control-rate from the host — exactly what `sq_set_param(engine, node_id, "frequency", 0.5)` already
  does. No special accommodation needed. A Faust-compiled node would expose its parameters through the same `getParameterDescriptors()` / `setParameter()` interface as any other Node.
  The thing Faust *can't* do is receive sample-accurate automation from the host (Squeeze's EventScheduler sub-block splitting). The host would set the value, it'd take effect for that block. Good enough for
  most use cases, but you wouldn't get glitch-free parameter sweeps without zipper artifacts — unless the Faust code includes its own internal smoothing (which is common practice: `si.smoo(hslider(...))`). I'm
   wondering about whether Faust can output C++ source code rather than compiled binaries.

⏺ Yes — that's actually Faust's primary mode of operation. Faust is a source-to-source compiler (transpiler), not a traditional compiler that produces binaries.

  faust -lang cpp -a minimal.cpp mydsp.dsp -o mydsp.cpp

  This generates a self-contained C++ class (inheriting from dsp) with:
  - buildUserInterface() — declares all parameters (sliders, buttons, etc.)
  - compute(int count, float** inputs, float** outputs) — the sample-processing loop
  - getNumInputs() / getNumOutputs()
  - init(int sampleRate)

  The generated C++ is pure computation — no runtime dependency on Faust itself. You get a .cpp file you can compile with any C++ compiler.

  Available backends:
  - C++ (-lang cpp) — the default and most mature
  - C (-lang c) — plain C output
  - LLVM IR (-lang llvm) — for JIT compilation
  - Rust, WebAssembly, Soul, VHDL, etc.

  What this means for Squeeze: A Faust-based processor wouldn't need Faust installed at runtime. The workflow would be:

  1. Author DSP in Faust (.dsp file)
  2. Compile to C++ at build time (or as a dev tool)
  3. The generated class gets wrapped in a Processor subclass that maps buildUserInterface() metadata to getParameterDescriptors() and routes compute() through processBlock()
  4. Ships as native C++ — no Faust runtime, no interpreter, no external dependency

  The generated compute() is typically tight, vectorizable code. Faust's compiler does aggressive optimization at the signal-graph level (constant folding, common subexpression elimination, delay line optimization)
   before emitting C++, so the output often outperforms hand-written equivalents.

  The one nuance: Faust also supports LLVM JIT via libfaust, which lets you compile .dsp strings to machine code at runtime (like a plugin that accepts Faust source text). That's heavier — it embeds the Faust
  compiler as a library. But the static C++ generation path has zero runtime cost.

❯ So above, you said that there's no audio rate modulation unless it is internal to Fause. But if something is outputting C++ code, couldn't we find a way to make it work with an external (to that .dsp) LFO?

⏺ Yes, and there are a few angles of attack. The key insight is understanding what the generated C++ actually does with parameters.

  The problem: Faust's compiler categorizes signals as either audio-rate or control-rate. UI primitives (hslider, button, etc.) are control-rate, so the generated code hoists the read outside the loop:

  void compute(int count, FAUSTFLOAT** inputs, FAUSTFLOAT** outputs) {
      float fSlow0 = float(fHslider0);  // read ONCE per block
      for (int i = 0; i < count; i++) {
          outputs[0][i] = inputs[0][i] * fSlow0;
      }
  }

  Writing to fHslider0 mid-block does nothing — the value is already cached in fSlow0. The fSlow prefix is literal: Faust decided this is a slow signal and optimized accordingly.

  Three practical approaches:

  1. Audio inputs as modulation buses (cleanest)

  Design the Faust DSP to accept modulation as audio input channels rather than parameters:

  // inputs: [audio_in, lfo_cutoff, lfo_resonance]
  process(audio, mod_cutoff, mod_resonance) =
      fi.resonlp(base_cutoff + mod_cutoff * depth, q + mod_resonance, 1, audio)
  with {
      base_cutoff = hslider("cutoff", 1000, 20, 20000, 1);
      depth = hslider("mod_depth", 0, 0, 10000, 1);
      q = hslider("resonance", 0.7, 0.1, 10, 0.01);
  };

  From Squeeze's perspective, this is just a processor with 3 inputs. You route your LFO's audio output to input channels 2 and 3. Fully audio-rate, no hacking, no performance penalty. Faust treats those inputs as
  sample-rate signals and generates the tight per-sample code you'd want.

  This is also how modular synthesis actually works — modulation is signal routing.

  2. Sub-block splitting (works but costly)

  Call compute(1, ...) per sample, updating the parameter pointer between each call:

  for (int i = 0; i < blockSize; i++) {
      *fHslider0 = lfoBuffer[i];   // update parameter
      dsp.compute(1, &in + i, &out + i);  // render one sample
  }

  This works because the hoisted read happens every call. But it kills vectorization and adds function-call overhead per sample. Squeeze's EventScheduler already does sub-block splitting, so this could be a special
   case of "split at every sample when audio-rate modulation is active" — but that's expensive.

  3. Post-process the generated C++ (fragile but powerful)

  Since it's just source code, you could transform it:

  - Find float fSlow0 = float(fHslider0); outside the loop
  - Replace with float* fMod0 = modInputs[0]; and read fMod0[i] inside the loop

  This is a build-time codegen step. Fragile if Faust changes its output format, but the output is actually quite stable and predictable. You could write a small tool that parses the generated code and rewrites
  tagged parameters from control-rate to audio-rate.

  ---
  For Squeeze, I'd go with approach 1. It requires zero special infrastructure — a Faust processor just declares more audio inputs, and Squeeze's existing routing handles the rest. The Faust code is explicit about
  what's modulatable, and the generated C++ is optimal. The Processor wrapper would document which inputs are audio vs. modulation in its parameter descriptors.

✻ Worked for 47s

❯ OK, but this seems awkward for Elektron-levels of modulation where you can somehow modulate *any* parameter if you choose. That's a lot of input buses!

⏺ Right — you don't want to pre-wire 20 modulation inputs on the off chance someone points an LFO at "reverb decay." Elektron's model is "any LFO can target any parameter, assigned at runtime." That's fundamentally
   a control-flow decision, not a signal-routing decision.

  The answer is: this isn't a Faust problem, it's a Squeeze engine problem. And you've mostly already solved it with EventScheduler's sub-block splitting.

  Think about what Elektron actually does internally. The LFO runs, writes a new value to the target parameter every sample (or every tick), and the synth engine reads it. The synth engine isn't "receiving
  modulation on an audio bus" — it's just getting its parameter updated very fast.

  Squeeze can do the same thing:

  1. LFO is its own thing — a modulation source producing a stream of values
  2. User assigns it: "LFO 1 → filter cutoff on processor X"
  3. Each block, the engine checks: does processor X have any audio-rate modulators active?
    - No → setParameter() once, processBlock() for the full block. Faust runs at full efficiency, vectorized, hoisted reads, everything optimal.
    - Yes → sub-block loop. For each sample (or small chunk): update the parameter from the LFO buffer, call processBlock(1). The hoisted read re-fires each call.

  The Faust processor doesn't know or care. It just has parameters and processBlock(). The engine makes the scheduling decision. The cost is only paid on the specific processor that currently has active audio-rate
  modulation — everything else runs full-block.

  This is exactly what the EventScheduler already does for sample-accurate automation breakpoints. An LFO is just automation with a breakpoint every sample. Expensive for that one processor, but only while
  modulation is active, and the user explicitly opted into it.

  Faust's -os (one-sample) compilation mode is also worth knowing about — it generates a compute() that processes one sample per call, specifically designed for this use case. You could even compile both variants
  at build time and swap at runtime depending on whether modulation is active.

✻ Brewed for 48s

❯ Won't processBlock(1) be a problem?

⏺ Yes. Several problems:

  Performance is the obvious one — you lose SIMD vectorization, you pay function call overhead 512x per block, branch prediction suffers. But that's just "slow," not "broken."

  The real problem is processors that need a minimum block size. FFT-based effects (spectral processing, convolution reverb), lookahead compressors, anything overlap-add — these fundamentally cannot operate on 1
  sample. They need N samples to produce meaningful output. A convolution reverb with a 1-sample block is nonsensical. And you're hosting VST/AU plugins too — many plugins have minimum block size expectations or
  will just misbehave.

  So processBlock(1) isn't a viable general strategy.

  What actually works: sub-blocks at a reasonable size. Don't go to 1 — go to 16 or 32. At 48kHz, a 16-sample sub-block gives you a 3kHz parameter update rate. That's faster than any LFO anyone would use and well
  beyond what Elektron does — their LFOs run at a control rate that's much slower than per-sample.

  So the engine logic becomes:

  - No modulation → processBlock(512), full efficiency
  - Audio-rate modulation active → split into processBlock(16) chunks, update parameter between each chunk from the LFO buffer (downsampled/interpolated to the sub-block rate)

  16 samples is small enough that even an aggressive audio-rate LFO sounds smooth, large enough that FFT-based processors and plugins still function, and you retain most of the vectorization benefit.

  This is also a much more natural fit for EventScheduler — it's already splitting at event boundaries, you'd just clamp the minimum sub-block size to something sane.

✻ Cogitated for 37s

❯ what sub-block size do Elektron boxes actually use?

⏺ I don't actually know the specific number — that's proprietary hardware implementation detail that Elektron doesn't publish. Let me see if anything is documented or reverse-engineered.

⏺ Web Search("Elektron Digitone Digitakt LFO update rate control rate samples")
  ⎿  Did 1 search in 25s

⏺ Web Search("Elektron synth internal control rate modulation resolution block size")
  ⎿  Did 1 search in 29s

⏺ Web Search("Elektronauts forum Elektron LFO control rate Hz internal sample rate modulation")
  ⎿  Did 1 search in 27s

⏺ Honest answer: I can't find the specific number. Elektron doesn't publish it and nobody seems to have definitively reverse-engineered it.

  What the searches do reveal:

  - Elektron LFO waveforms are divided into 128 phase steps per cycle, which tells us something about resolution but not the update rate directly
  - The Analog Four's audio-rate LFO mode (added in firmware updates) is explicitly called out as a special feature — implying the normal LFO mode runs at a significantly lower control rate
  - The Mod Wiggler thread on digital modulation notes that many digital synths run modulation at a lower sample rate than audio, and that recalculating filter coefficients every sample is expensive — so most do it
   once per block

  Given the 128 phase steps and typical embedded DSP practice, reasonable guesses would be somewhere in the range of a few hundred Hz to low kHz for the control rate — maybe 500-3000 Hz. That maps to something like
   16-96 sample sub-blocks at 48kHz.

  For Squeeze, the practical takeaway is the same: a sub-block size of 16-32 samples gives you a modulation update rate (1.5-3kHz at 48kHz) that's likely as good or better than what Elektron does in their normal
  LFO mode, and it avoids all the block-size-of-1 problems. You can always make it configurable if someone wants to go smaller.

  Sources:
  - Digital limitations / audio rate modulation / resolution - MOD Wiggler
  - Polyphonic synths with audio rate modulation sources - Elektronauts
  - Analog Four 1.22 update (FM and conditional sequencing) - MOD Wiggler
  - I finally solved and understand the LFOs - Elektronauts
