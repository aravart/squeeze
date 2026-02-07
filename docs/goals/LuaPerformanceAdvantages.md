# Lua Performance Advantages — Beyond the Octatrack

The Octatrack's workflow is fixed. Brilliant, but fixed. With Lua + VSTs + a clock, Squeeze can do things hardware can't. This document expands on what that means concretely.

---

## Algorithmic Sequencers

The Octatrack's sequencer is a 64-step grid. Powerful, but it's a grid — you place notes on steps, and they play back. Everything you hear was explicitly programmed step by step.

With Lua generating MIDI events on a clock, the sequencer *is code*. That opens up entire families of sequencing approaches:

### Euclidean rhythms

Distribute N hits across M steps as evenly as possible. Euclidean(3,8) gives you a tresillo rhythm. Euclidean(5,16) gives you a clave variant. Two lines of Lua generate rhythmic patterns that would take careful manual programming on a grid. Stack several Euclidean generators at different densities and you get interlocking polyrhythms that feel alive because they *are* mathematically alive — they're not loops, they're processes.

### Polymetric / polyrhythmic patterns

The Octatrack runs everything in the same time signature with the same pattern length. In Lua, nothing stops you from running a 7-step bass pattern against a 16-step hihat against a 12-step chord sequence. They phase against each other, align every LCM(7,16,12) = 336 steps, and the result is a slowly evolving texture that never repeats within human attention spans.

### Cellular automata

Take a row of cells, apply a rule (like Rule 110 or Game of Life), map the result to notes or triggers. Each generation produces the next pattern. The music *grows* according to simple rules that produce complex emergent behavior. You can seed it with a kick pattern and watch it evolve into something you'd never program by hand.

### Markov chains

Record yourself playing, analyze the note transitions, build a probability table: "after C, there's a 40% chance of E, 30% chance of G, 20% chance of A, 10% chance of rest." Then let the chain generate new melodies that *sound like you* but never repeat. Feed it a Bach chorale and it writes pseudo-Bach. Feed it your own patterns and it writes pseudo-you.

### L-systems and fractal structures

Start with an axiom like "A", apply rewrite rules (A → AB, B → A), iterate, and map the resulting string to musical events. This produces self-similar structures at multiple time scales — the phrase structure mirrors the note structure, which mirrors the rhythmic structure. Music with fractal geometry.

### Turing machine / shift register

A loop of bits that gets slightly mutated each cycle — flip one random bit. The pattern is *almost* the same each time through, but it drifts. Lock it and it loops perfectly. Unlock it and it wanders. This is the core idea behind the Music Thing Modular Turing Machine, and it's trivial in Lua.

### Mathematical mappings

Map the digits of pi to a scale. Map prime numbers to rhythmic positions. Map the Fibonacci sequence to intervals. These sound like gimmicks, but they produce patterns with an uncanny sense of structure that our ears detect without being able to name. They're useful as *seeds* — starting points that you curate and constrain into actual music.

### The key insight

All of these are **10-50 lines of Lua each**. They're not complex to implement. They're just impossible on hardware that only gives you a step grid.

---

## Conditional Triggers, Probability, Generative Patterns

The Octatrack (and other Elektron boxes) introduced conditional triggers — a step can be set to play only on certain conditions like "every other time" (1:2), "the first 3 of every 4 plays" (3:4), or only during a fill. This was revolutionary for hardware. But the conditions are a fixed menu. You get maybe 15 options.

In Lua, the condition is arbitrary code.

### Per-step probability

Each step has a probability from 0 to 100. At 70%, a hihat step plays 7 out of 10 times. Simple, but when you apply different probabilities to different parts of the kit, the groove breathes — it's never quite the same twice, but it stays recognizably the same pattern. The Octatrack can't do per-step probability at all.

### Velocity humanization

Not just random ±N. You can model actual drumming: accents on beats 1 and 3, ghost notes drift quieter over time, velocity increases slightly during fills. A function that takes (step, bar_number, is_fill) and returns a velocity.

### Conditional chains

"If the kick played on step 5, skip the snare on step 6." "If we've looped this pattern more than 8 times, introduce a variation." "If the last 4 bars had no fills, force one." These are stateful conditions — the sequencer has memory. The Octatrack's conditionals are stateless (they don't know what happened before).

### Accumulator patterns

Every N repetitions, shift a parameter. The filter opens a little more each pass. The pattern transposes up a semitone every 4 bars. After 16 bars, reset. This creates long-form structure from short patterns without you having to program 16 different variations.

### Weighted random from pools

Instead of one pattern, define three variations of a fill. Each time the fill triggers, pick one at random (weighted — variation A 50%, B 30%, C 20%). The music stays in the same territory but never plays the exact same fill twice. You can pre-compose the variations so they all work, but the selection is generative.

### Mutation

Start with a programmed pattern. Each cycle, there's a small chance that one step changes — a note shifts pitch, a ghost note appears, a hihat drops out. Over minutes, the pattern evolves away from its origin. You can control the mutation rate: 0% is a loop, 100% is chaos, somewhere around 5-15% is *alive*.

### State machines

The pattern has modes: intro, verse, build, drop, breakdown. Each mode has different patterns, different probability settings, different parameter ranges. Transitions between modes can be triggered manually (MIDI button) or automatically (after N bars, or when a condition is met). This is the beginning of a system that plays *sets*, not just loops.

---

## Custom Scene Logic

The Octatrack's crossfader is linear interpolation between two parameter snapshots. Scene A: filter open, reverb dry, tempo 120. Scene B: filter closed, reverb wet, tempo 130. Fader at 50%: everything halfway between. It's intuitive, it's physical, and it works. But linear is all you get, and two scenes is the limit.

### Curves

A linear crossfade treats all parameters equally. But perception isn't linear — volume is logarithmic, filter cutoff is exponential, tempo is... complicated. With Lua, each parameter gets its own curve. Filter follows an exponential curve (slow at the top, fast at the bottom, matching how we hear frequency). Volume follows logarithmic (matching loudness perception). Reverb mix stays dry until 70% then ramps quickly (because a little reverb is subtle but a lot is dramatic). One fader gesture, but each parameter moves at its own rate in its own shape.

### Multi-scene morphing

Why stop at two? Define four scenes on a 2D XY pad. Top-left is "ambient," top-right is "rhythmic," bottom-left is "sparse," bottom-right is "dense." Bilinear interpolation between all four based on an XY controller. Or define 8 scenes and use two faders — one selects between groups, the other morphs within the group.

### Stepped / quantized

Divide the fader into 8 discrete positions, each a distinct preset. No interpolation — it snaps between states. This turns the crossfader into a preset selector. Useful for dramatic changes: each position is a completely different texture. Combine with crossfade between *adjacent* steps for smooth but structured transitions.

### Hysteresis

The curve is different going forward vs. going back. Push the fader right: parameters change slowly at first, then dramatically. Pull it back left: they snap back quickly. This creates asymmetric transitions — a slow build and a sudden drop, from one physical gesture.

### Scripted transitions

"Over the next 4 bars, morph from scene A to scene B." You don't touch the fader — the script drives it. You can choreograph transitions: "Bars 1-4: morph filter. Bars 5-8: morph reverb. Bar 9: snap everything." This is basically per-parameter timeline automation, but expressed in code rather than drawn in a DAW.

### Reactive scenes

Scene position follows an audio analysis: when the input gets louder, morph toward scene B. When a certain frequency range spikes, push the crossfader. The system responds to the music. Feed it a live drum input and the effects respond to the dynamics of the playing. This is generative processing — the effects become part of the instrument.

### Random / stochastic

Each parameter jitters around its scene value by a random amount, controlled by a "chaos" knob. At 0%, it's the static scene. At 100%, parameters wander freely within their full range. In between, the scene is a suggestion that the system orbits around. Combine with slow LFOs and you get drifting textures that evolve without explicit programming.

---

## Live-Codeable Performance Scripts

This is maybe the most radical departure from hardware thinking. The Octatrack's behavior is fixed — its firmware defines what it can do, forever. Squeeze's behavior is *what the current Lua script says it is*.

### Hot-reload without stopping audio

Change a script file, reload it, and the new behavior takes effect on the next bar boundary. The audio engine never stops, the clock never glitches. You wrote a sequencer that plays 16th notes? Edit it to play triplets, save, reload — the change happens on the downbeat. This is how TidalCycles and Sonic Pi work, and it's the basis of livecoding as a performance practice.

### REPL as instrument

Squeeze already has a Lua REPL. During performance, type `bpm = 140` and the tempo changes. Type `pattern.kick[5] = 0` and step 5 goes silent. Type `scene_b.reverb = 0.9` and the target scene shifts. The command line becomes a performance surface. Some performers project the REPL so the audience sees the code changing — the process *is* the performance.

### Compositional macros

Build up a library of functions over the course of a performance. Start with a simple loop. Define a function `stutter(steps)` that repeats the current beat N times. Define `drop()` that kills everything except the kick. Define `build(bars)` that opens the filter over N bars. Now your performance vocabulary is: `stutter(4)`, `drop()`, `build(8)` — high-level musical gestures, not low-level parameter tweaks. You're performing at the level of arrangement, not mixing.

### Script switching as arrangement

Different scripts define different "songs" or "sections." Load `ambient_intro.lua` — the system plays a pad texture with generative melodies. Crossfade to `beat_drop.lua` — now it's a drum pattern with sidechain compression. Each script is a complete instrument configuration. The performance is navigating between scripts, with transition logic handling the morph.

### Audience interaction

Expose parameters to a web interface (which is where the WebSocket server comes in). Audience members vote on the next pattern, or a slider on their phones controls the chaos parameter. The Lua script reads these inputs and incorporates them. This is impossible on any hardware sampler.

### Self-modifying patterns

A script that rewrites itself: it analyzes what it played last bar, makes a judgment call (too repetitive? too chaotic?), and adjusts its own parameters. This is a feedback loop between the music and the algorithm. The performer's role shifts from controlling every detail to *steering* a system that has its own momentum.

---

## Summary

The common thread: the Octatrack gives you a fixed, brilliant instrument. Squeeze with Lua gives you **a toolkit for building instruments**. The cost is that you have to build them. The payoff is that you can build ones that don't exist yet.
