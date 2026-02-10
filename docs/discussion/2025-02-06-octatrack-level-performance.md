# What would it take to get Squeeze to Octatrack-level performance?

## What makes the Octatrack a performance instrument

It's not really about features — it's about **constraints that enable flow**. A few core things:

1. **Deterministic timing.** Everything is locked to a clock. Pattern changes, parameter changes, sample triggers — all quantized to musically meaningful boundaries. You never "miss the beat."

2. **Scenes + crossfader.** Two snapshots of every parameter, with a single physical fader to morph between them. This is the killer performance feature — it turns dozens of parameters into one gesture.

3. **Parameter locks.** Per-step automation. Every step in a sequence can override any parameter. This is what makes Elektron patterns feel alive.

4. **Instant recall.** Patterns, parts, banks — all switchable with no glitches, no loading screens. The audio never stops.

5. **Thru machines.** Live audio in, processed through the same effect chain as samples. This is what makes it a mixer/processor, not just a playback device.

## Where Squeeze is now

We have #5 essentially — audio flows through a graph of VST plugins with MIDI routing. That's actually the hardest part to get right. We also have Lua, which gives us something the Octatrack never will: scriptable, programmable behavior.

## What's missing for performance

Roughly in priority order:

**Transport + Clock** — A master clock (BPM, beat/bar position) that everything locks to. Pattern changes happen on bar boundaries. MIDI clock out so external gear syncs. This is the backbone.

**Sequencer** — Doesn't need to be Elektron-style at first. Even a simple Lua-driven step sequencer that fires MIDI events on the clock would get us there. Parameter locks are just "at step N, set parameter X to value Y" — very natural in Lua tables.

**Parameter access** — Right now we can host plugins, but can we read/write their parameters from Lua? This is critical. `node:set_param(name, value)` and `node:get_param(name)` open up scenes, crossfading, parameter locks, automation — everything.

**Scenes / state snapshots** — Once we have parameter access, a "scene" is just a Lua table of `{node, param, value}` tuples. A crossfader is linear interpolation between two scenes. This could be 50 lines of Lua.

**MIDI controller mapping** — Incoming CC messages mapped to actions. A Lua table: `{[cc74] = function(val) ... end}`. Lets you use any controller as a performance surface.

**Glitch-free preset switching** — This is the hard systems problem. Changing the graph topology (adding/removing nodes, reconnecting) without audio dropouts. The snapshot-swap architecture helps, but we'd need to think about crossfading between graph states.

**Sample playback** — Could punt on this by using a sampler VST (like a free one hosted in the graph). Or build a native SamplerNode eventually. The Octatrack's sample manipulation is deep, but a basic one-shot/loop player is achievable.

**Recording / live looping** — A LooperNode that records from its input and plays it back. Even a basic one transforms the system from playback to performance.

## The Lua advantage

The Octatrack's workflow is fixed. Brilliant, but fixed. With Lua + VSTs + a clock, we could build:

- Algorithmic sequencers that the Octatrack can't do
- Conditional triggers, probability, generative patterns
- Custom scene logic (not just linear crossfade — curves, random, stepped)
- Live-codeable performance scripts

We're essentially building a *programmable* Octatrack. The ceiling is much higher, but the floor needs work.

## Realistic next steps

Prioritized for getting to "I can perform with this":

1. **Parameter access on PluginNode** — expose VST parameters to Lua
2. **Transport / master clock** — BPM, beat position, bar boundaries, callbacks on beats
3. **MIDI CC input mapping** — route controller data to Lua callbacks
4. **A simple pattern sequencer in Lua** — prove the concept with a script

Those four things would get us to performable. Everything else is refinement.
