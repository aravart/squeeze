❯ Let's talk candidly about the pros and cons of this audio server framework. Right now there
  is no way to "GLUE" things together. That's not just hard to manage, it might also lead to
  performance problems and limitations down the line. If I want a new primitive, I have to
  hard code it in C++. Composability seems weak.

⏺ You're right, and this is worth being honest about.

  The core problem

  Right now the architecture is essentially: a fixed set of C++ node types, wired together from
  Python. Python is a remote control, not a construction kit. The "programmable" part is limited to
   arranging primitives — you can't create new ones without dropping into C++, recompiling, and
  extending the FFI.

  That's three distinct problems:

  1. No composition (the "glue" problem)

  Without grouping, everything lives in one flat graph. Want a channel strip? That's 5 loose nodes.
   Want 16 channel strips? That's 80 loose nodes in one topology sort, with no way to:

  - Treat a strip as a unit (bypass, save, reuse, instantiate)
  - Nest (a mixer bus containing strips)
  - Reduce cognitive or computational complexity

  As the graph grows, this gets worse — connection count grows combinatorially, the topology sort
  processes everything at the same granularity, and there's no opportunity to skip an entire
  subgraph (e.g., a muted bus).

  2. No user-defined primitives

  This is the deeper issue that GroupNode doesn't solve. GroupNode lets you compose existing C++
  nodes into larger units. But if you need a node that does something none of the built-in types do
   — a custom envelope follower, a spectral processor, a granular engine — you're back to writing
  C++.

  For a Python-driven audio framework, that's a significant limitation. The whole pitch is "program
   your audio from Python," but the programming stops at the arrangement layer.

  3. The GroupNode solution is expensive for what it delivers

  The audit shows GroupNode touches almost every foundational component — dynamic ports, doubled
  API surface, cascading change detection, recursive PDC, hierarchy-spanning infrastructure. That's
   a lot of complexity for what is essentially "nested graphs." And it still doesn't give you
  user-defined DSP.

  What the alternatives look like

  There's a spectrum of solutions, roughly ordered by power vs. complexity:

  A. Graph templates (lightest)
  Python-side "stamp out these 5 nodes and wire them up" helpers. No engine changes. The engine
  still sees a flat graph. You get reusability and patterns but no encapsulation, no
  bypass-as-unit, no nested latency management.

  This is what you can already do today with Python helper functions. It's not nothing, but it
  doesn't solve the performance or encapsulation problems.

  B. Lightweight GroupNode (medium)
  A simplified version: GroupNode is opaque to the parent graph, has fixed ports declared at
  creation time (not dynamic export/unexport), and processes its internal graph as a black box.
  This gets you:

  - Encapsulation and bypass-as-unit
  - Subgraph skipping for muted buses
  - Nested PDC (but simpler — ports don't change)
  - No doubled API (build the internal graph at creation time, then seal it)

  You lose dynamic port manipulation at runtime, but you dodge most of the expensive concessions
  (no cascading changes, no dynamic ports, simpler API surface).

  C. ScriptNode / callback node (different axis)
  A node type whose process() calls into Python (or Lua, or WASM). This solves the "new primitives
  require C++" problem entirely. The user writes DSP in a high-level language and the engine hosts
  it.

  The cost: crossing the language boundary per audio block. For Python specifically, the GIL makes
  this painful. For Lua or WASM, it's more viable. This is what many successful systems do (Max/MSP
   externals, SuperCollider UGens, VCV Rack modules).

  D. Full GroupNode (current spec)
  Maximum flexibility, maximum cost. Dynamic ports, recursive everything, universal composition
  primitive.

  E. ScriptNode + lightweight GroupNode (the interesting combo)
  ScriptNode gives you user-defined primitives. Lightweight GroupNode gives you composition.
  Together they cover both gaps without the full weight of the current GroupNode spec. The engine's
   built-in C++ nodes become the optimized fast path for common operations, while ScriptNode
  handles the long tail of custom behavior.

  My honest take

  The current architecture's biggest weakness isn't the absence of GroupNode specifically — it's
  that Python can arrange but not create. GroupNode as currently spec'd is an expensive solution to
   one half of the composability problem (arrangement reuse) while ignoring the other half (custom
  DSP).

  If I were prioritizing, I'd consider:

  1. Skip to Transport/Scheduling (Phase 4) — get the engine actually usable first
  2. Add a lightweight sealed-GroupNode later (option B) — simpler, covers 80% of the composition
  need
  3. Explore ScriptNode as the real composability play — this is what makes the Python story
  actually compelling

  The risk of building full GroupNode now is that you pay a huge complexity tax on a feature whose
  value is "arrange existing things in groups" when the bigger gap is "I can't make new things at
  all."

❯ I suppose this is why Faust / Supercollider do what they do *compiling* new C++ primitives
  from a different language, no?

⏺ Exactly. That's the key insight those systems arrived at: the language boundary can't exist at
  runtime for DSP. So they move it to compile time.

  The spectrum is roughly:

  ┌───────────────┬────────────────────┬─────────────────────┬──────────────┐
  │    System     │  "Nice" language   │     Compiles to     │ Boundary at  │
  ├───────────────┼────────────────────┼─────────────────────┼──────────────┤
  │ Faust         │ Functional DSP     │ C++, LLVM IR, WASM  │ Compile time │
  ├───────────────┼────────────────────┼─────────────────────┼──────────────┤
  │ SuperCollider │ sclang             │ C++ UGens (plugins) │ Compile time │
  ├───────────────┼────────────────────┼─────────────────────┼──────────────┤
  │ Max/MSP       │ Patcher            │ C externals         │ Compile time │
  ├───────────────┼────────────────────┼─────────────────────┼──────────────┤
  │ Csound        │ Orchestra language │ C internally        │ Compile time │
  ├───────────────┼────────────────────┼─────────────────────┼──────────────┤
  │ VCV Rack      │ C++ (but with SDK) │ Shared libraries    │ Compile time │
  └───────────────┴────────────────────┴─────────────────────┴──────────────┘

  Every one of these learned the same lesson: you can interpret/script the wiring (what connects to
   what, when things start/stop, parameter changes) but the DSP itself must be native code. The
  "high-level language" story is always about making it easier to produce that native code, not
  about calling into an interpreter at sample rate.

  For Squeeze, the Python layer already handles the wiring side well. The gap is: what happens when
   a user needs a DSP primitive that doesn't exist?

  The practical options, from least to most ambitious:

  1. Faust integration — Faust already compiles to JUCE-compatible C++. A user writes .dsp files,
  compiles them, and Squeeze loads them as plugin nodes. This is almost free — PluginNode already
  hosts VSTs, and Faust can emit VSTs. The only missing piece is a tighter workflow
  (compile-and-hot-reload rather than going through a full plugin build).
  2. Native shared-library nodes — A stable C ABI for user-written nodes (a .dylib/.so with
  create(), process(), destroy() exports). Simpler than the full VST spec, specific to Squeeze.
  Users write C/C++/Rust/Zig, compile to a shared lib, Squeeze loads it. This is essentially what
  SuperCollider UGens are.
  3. WASM nodes — Same idea but targeting WebAssembly. Users write in any language that compiles to
   WASM, Squeeze runs it in a WASM runtime (Wasmtime, etc.). Sandboxed, portable, near-native
  performance. This is the modern version of option 2.
  4. JIT from Python — Something like Numba or JAX-style tracing that takes a Python function with
  numpy-like operations and JIT-compiles it to native code. Keeps the "everything is Python" story
  intact. Much harder to build, but the most compelling user experience.

  The interesting thing is that none of these require GroupNode. They're orthogonal. And arguably
  more valuable — GroupNode gives you new arrangements of existing parts, while any of these gives
  you genuinely new parts.

  If you wanted to pick one low-hanging fruit: option 1 (Faust) already works today through the
  existing plugin hosting path. The user experience is just rough (write Faust → compile to VST →
  load in Squeeze). Smoothing that pipeline — or even embedding libfaust for JIT compilation —
  would give Squeeze a real "create new DSP from not-C++" story without touching the engine
  architecture at all.

❯ Well what do I really need? Let's consider two concrete cases. A MixNode that can
  dynamically accept new inputs / remove inputs. A linear effects chain that all operates on
  the same buffer in place but which can again grow/shrink.

⏺ These are the two most common composition patterns in audio, and neither one actually needs
  GroupNode.

  MixNode

  A MixNode sums N inputs to one stereo output. The simplest version:

  - Fixed max inputs (say 64 stereo pairs): in_0_L, in_0_R, in_1_L, in_1_R, ...
  - Unconnected ports are silent — process() just skips them
  - Per-input gain via indexed parameters: gain_0, gain_1, ...
  - "Add an input" = connect to the next unused port pair
  - "Remove an input" = disconnect

  No dynamic ports. No new architecture. One C++ class. The "dynamic" part is just which ports have
   connections, which Graph already tracks. Python provides a nice wrapper:

  mixer = engine.add_mix(name="drum_bus")
  mixer.attach(kick_node)    # finds next free port pair, connects
  mixer.detach(kick_node)    # disconnects

  ChainNode

  A linear effects chain is where things get more interesting, because there's a real performance
  argument.

  In the flat graph today, a chain of 5 effects means:

  EQ.out → Comp.in  (buffer copy)
  Comp.out → Sat.in  (buffer copy)
  Sat.out → Lim.in   (buffer copy)
  ...


  Each node reads from one buffer and writes to another. Five nodes, five buffers, four copies.

  A ChainNode owns a sequence of processors and passes the same buffer through all of them
  in-place:

  [in] → EQ → Comp → Sat → Lim → [out]
           (one buffer, modified in-place)


  One buffer. Zero copies. For a 16-channel mix with 5-effect chains, that's the difference between
   80 buffer allocations and 16.

  But here's the design tension: what are the "effects" inside the chain? They can't be regular
  Nodes in the Graph — they don't have connections, they don't participate in the topology sort.
  They're internal to the ChainNode.

  Two options:

  A. Chain contains Node instances, privately owned. They have global IDs (for parameter access)
  but aren't in the Graph. ChainNode calls their process() directly, passing the same buffer as
  both input and output. Engine's getNode() can still find them for sq_set_param. This is a mini
  version of the GroupNode "nodes owned outside the graph" pattern — but much simpler because
  there's no internal routing, just a list.

  B. Chain contains a different abstraction — "Processors" — that are lighter than Nodes. No ports,
   no IDs, just a process(buffer) function. Simpler, but now you have two hierarchies (Node and
  Processor), and you can't address internal effects by ID for parameter automation.

  Option A is better. It reuses the existing Node interface, keeps parameter access uniform, and
  the only "concession" is that Engine needs a flat map of all nodes (not just graph-registered
  ones) — which is just a std::unordered_map<int, Node*> that ChainNode registers its children
  into.

  What these two cover

  MixNode  → fan-in / summing bus
  ChainNode → serial effects insert
  flat Graph → arbitrary parallel/complex routing


  Together with the existing flat graph for everything else, these handle:

  - Channel strip: ChainNode (EQ → Comp → Gate → Gain)
  - Mixer bus: MixNode with per-input gain
  - Drum kit: MidiSplitter → N samplers → MixNode
  - Parallel compression: split → dry + ChainNode(comp) → MixNode
  - Master chain: ChainNode (Limiter → Dither → Meter)

  That's roughly 95% of real-world composition patterns, without any of the 15 GroupNode
  concessions. No dynamic ports, no doubled API, no cascading changes, no recursive PDC, no port
  export/unexport.

  What you'd still give up

  - Arbitrary nested topologies — a subgraph with non-linear internal routing (e.g., a feedback
  loop inside a group). Rare in practice.
  - Recursive nesting — a ChainNode inside a ChainNode. You could support this, but do you need it?
  - General-purpose encapsulation — hiding arbitrary complexity behind a clean interface. MixNode
  and ChainNode are specific shapes, not a universal composition primitive.

  The question is whether that 5% of use cases justifies the complexity cost. For a practical audio
   engine, I'd argue it doesn't — at least not yet.

❯ Does Faust help with anything besides "processors" with fixed in and fixed out?

⏺ No. Faust is strictly fixed-topology, compile-time-determined I/O. A Faust program is a pure
  function from N inputs to M outputs where N and M are known at compile time. No dynamic routing,
  no adding/removing channels, no runtime structural changes.

  It solves exactly one problem well: writing new leaf-node DSP processors (a filter, a compressor,
   a reverb) without hand-writing C++. That's valuable, but it's orthogonal to everything you're
  wrestling with here.

  Your two cases — a mixer that gains/loses inputs, a chain that gains/loses effects — are
  fundamentally about runtime-dynamic structure. Faust has nothing to say about that. Neither does
  SuperCollider's UGen system, nor Max's externals. Those all produce fixed-shape blocks.

  The "glue" problem is always solved by the host, not the DSP language:

  - In Max/MSP: the patcher (host) handles dynamic wiring
  - In SuperCollider: the SynthDef graph builder (host) handles topology
  - In VCV Rack: the rack runtime (host) handles cable connections
  - In Squeeze: the Engine (host) handles it

  Which brings it right back to where you are: MixNode and ChainNode as engine-level C++ primitives
   are the right answer for these patterns. They're host-level concerns — dynamic structure
  management — not DSP authoring concerns. Faust could still be useful inside a ChainNode (compile
  a custom effect, load it as one of the chain's processors), but it can't be the chain.

❯ Does Faust expose parameters even? Are they modulated at audio rate, control rate?

⏺ Yes. Faust has a built-in parameter model using "UI primitives":

  freq = hslider("frequency", 440, 20, 20000, 1);
  gain = hslider("gain", 0.5, 0, 1, 0.01);
  gate = button("trigger");
  process = os.osc(freq) * gain * gate;

  These compile down to float pointers in the generated C++ code. The host reads/writes them
  directly. The generated class exposes a buildUserInterface() method that enumerates all
  parameters with their names, ranges, and defaults.

  As for rate — it's nuanced:

  - From the host side: control rate. The standard architecture files set the float value once
  before calling compute() (i.e., once per block). So from Squeeze's perspective, they're
  block-rate parameters — exactly like sq_set_param.
  - Inside Faust's signal flow: audio rate. The parameter variable is just a float that the signal
  graph reads every sample. If the host happened to write to the float pointer mid-block, the DSP
  would pick it up immediately. But no standard architecture does that.
  - Audio-rate modulation is done within Faust by wiring signals to signals. You don't modulate a
  parameter from outside at audio rate — you'd write the modulation into the Faust program itself:

  // LFO modulating filter cutoff — all internal, audio rate
  cutoff = hslider("cutoff", 1000, 100, 10000, 1) + os.osc(5) * 200;
  process = fi.lowpass(2, cutoff);

  So the mapping to Squeeze is actually clean: Faust parameters are named, have ranges/defaults,
  and are control-rate from the host — exactly what sq_set_param(engine, node_id, "frequency", 0.5)
   already does. No special accommodation needed. A Faust-compiled node would expose its parameters
   through the same getParameterDescriptors() / setParameter() interface as any other Node.

  The thing Faust can't do is receive sample-accurate automation from the host (Squeeze's
  EventScheduler sub-block splitting). The host would set the value, it'd take effect for that
  block. Good enough for most use cases, but you wouldn't get glitch-free parameter sweeps without
  zipper artifacts — unless the Faust code includes its own internal smoothing (which is common
  practice: si.smoo(hslider(...))).

✻ Cogitated for 38s

❯ Awkward. Say you were writing a synth and you wanted to wire an LFO to a parameter. You'd
  have to have all possible LFOs *inside* each Faust node? Nothing like Elektron-style routing
   of LFOs to parameters across the engine?

⏺ Right. That's the fundamental tension. Faust's model is a closed box — everything that happens at
   audio rate must be compiled inside. There's no "audio-rate input that modulates a parameter from
   outside."

  So in a Faust world, if you want an LFO on filter cutoff, you either:

  1. Bake the LFO into the Faust program (every node carries its own LFOs)
  2. Accept block-rate modulation from the host (set the parameter 300 times/sec, live with the
  zipper noise or hope the Faust code smooths it)
  3. Use an audio input as the modulation source — route the LFO node's audio output into a
  dedicated sidechain input on the Faust node, and have the Faust code multiply/add it internally

  Option 3 technically works but it's ugly — every Faust program would need explicit "mod_cutoff",
  "mod_resonance" sidechain inputs written into the source, and you'd recompile whenever you want
  to modulate a different parameter.

  This is exactly why modular systems (Eurorack, VCV Rack, Reaktor) don't use Faust as their core.
  Their whole paradigm is: CV is just audio, everything modulates everything, the routing is
  external. Faust's closed-box model is the opposite of that.

  The Elektron model you're describing — free-floating LFOs that can target any parameter on any
  node at audio rate — is an engine-level concern. It requires the engine to understand that a
  parameter isn't just a float you set, it's a destination that can receive a signal. That's a
  fundamentally different parameter model:

  final_value = base_value + (mod_signal * depth)
                ^^^^^^^^^^   ^^^^^^^^^^^^^^^^^^^^^^^^
                sq_set_param   engine routes this per-sample


  This is actually a stronger argument for building modulation as a first-class engine feature than
   for adopting Faust. Faust would actively fight you on this — you'd have to break open its
  encapsulation to inject external modulation signals, at which point you've lost most of what
  Faust gives you.

  The honest conclusion: Faust is good for writing self-contained effects processors (reverbs, EQs,
   compressors) where all modulation is internal. It's a poor fit for a modular synthesis engine
  where the whole point is that signals cross node boundaries to modulate parameters. For Squeeze,
  if you ever want Elektron-style modulation routing, that has to be built into the engine's
  parameter system — no external DSP language helps with that.


❯ OK, so let's consider this from the other perspective. What would be the right
  infrastructure if these were my set of requirements...   Hosting VST
    Opening editor winddows
    Routing MIDI notes
    Routing control rate MIDI
    Control rate automation
    Adding/removing processors during live play
    Mixing/summing
    Metering
    Disk recording
    Buffer recording
    Sample-accurate note-timing
    Buffer/sample playback
    Latency compensation
    Send channels
    Bus channels


⏺ Look at that requirements list again. It's not describing an arbitrary graph. It's describing a
  mixer.

  Source → [insert chain] → sends → bus assignment
                                          ↓
                                Bus → [insert chain] → master
                                                          ↓
                                                 Master → [insert chain] → output


  Every single requirement maps onto this structure directly. And this structure is far simpler
  than a general-purpose node graph.

  The primitives

  You need three things, not twenty:

  Chain — an ordered list of processors sharing one buffer, processed in-place. This is your insert
   rack. Effects go in, effects come out. One buffer, zero copies.

  Bus — sums N inputs into one buffer, then runs its own Chain. This is your mix bus, your
  send-return bus, your master. MixNode and ChainNode fused into one concept.

  Source — anything that generates audio into a buffer. A hosted VST synth, a sampler, an audio
  input.

  That's it. Sends are just routing with a gain: tap a channel's output, scale it, feed it to a
  bus. Metering is a tap that reads without modifying. Recording is a tap that writes to
  disk/buffer.

  How the requirements map

  ┌─────────────────────────────┬─────────────────────────────────────────────────────────────┐
  │         Requirement         │                      How it's handled                       │
  ├─────────────────────────────┼─────────────────────────────────────────────────────────────┤
  │ Hosting VST                 │ Source (synth) or Chain processor (effect)                  │
  ├─────────────────────────────┼─────────────────────────────────────────────────────────────┤
  │ Editor windows              │ On the processor, same as now                               │
  ├─────────────────────────────┼─────────────────────────────────────────────────────────────┤
  │ MIDI note routing           │ Source → MIDI input assignment                              │
  ├─────────────────────────────┼─────────────────────────────────────────────────────────────┤
  │ Control rate MIDI           │ CC routing table, separate from audio                       │
  ├─────────────────────────────┼─────────────────────────────────────────────────────────────┤
  │ Control rate automation     │ Scheduler fires parameter changes per block                 │
  ├─────────────────────────────┼─────────────────────────────────────────────────────────────┤
  │ Sample-accurate notes       │ Scheduler places note events at sample offsets within block │
  ├─────────────────────────────┼─────────────────────────────────────────────────────────────┤
  │ Add/remove during live play │ Swap chain array atomically at block boundary               │
  ├─────────────────────────────┼─────────────────────────────────────────────────────────────┤
  │ Mixing/summing              │ Bus                                                         │
  ├─────────────────────────────┼─────────────────────────────────────────────────────────────┤
  │ Metering                    │ Read-only tap on any bus/channel                            │
  ├─────────────────────────────┼─────────────────────────────────────────────────────────────┤
  │ Disk recording              │ Write tap on any bus/channel                                │
  ├─────────────────────────────┼─────────────────────────────────────────────────────────────┤
  │ Buffer recording            │ Write tap to memory                                         │
  ├─────────────────────────────┼─────────────────────────────────────────────────────────────┤
  │ Sample playback             │ Source                                                      │
  ├─────────────────────────────┼─────────────────────────────────────────────────────────────┤
  │ Latency compensation        │ PDC over the bus DAG (much smaller than a node graph)       │
  ├─────────────────────────────┼─────────────────────────────────────────────────────────────┤
  │ Send channels               │ Route channel output → bus input, with gain                 │
  ├─────────────────────────────┼─────────────────────────────────────────────────────────────┤
  │ Bus channels                │ Bus                                                         │
  └─────────────────────────────┴─────────────────────────────────────────────────────────────┘

  What the processing loop looks like

  for each source (in any order — they're independent):
      source.generate(buffer)
      source.chain.process(buffer)      // in-place inserts
      copy scaled taps to send buses    // sends

  for each bus (in dependency order — bus DAG topo sort):
      bus.sum(input_buffers)            // mix inputs
      bus.chain.process(buffer)         // in-place inserts
      copy scaled taps to send buses    // bus sends

  master.sum(input_buffers)
  master.chain.process(buffer)
  output(master.buffer)


  The topology sort operates on buses (maybe 4–8 of them), not individual processors (potentially
  hundreds). PDC operates on the bus DAG too — much simpler than computing latency through an
  arbitrary graph of nodes.

  What this means for the C++ layer

  The engine becomes much simpler:

  - Processor: base class, process(AudioBuffer&) — operates in-place
  - PluginProcessor: wraps a VST, implements Processor
  - Chain: owns an ordered vector<Processor*>, calls each in sequence on the same buffer
  - Bus: owns input routing + a Chain, sums inputs then processes chain
  - Engine: owns sources + buses + master, runs the processing loop above, handles scheduling

  No Port. No Connection. No Graph. No topology sort over individual nodes. No GroupNode. No
  dynamic ports. No port export/unexport. No cascading change detection. The bus dependency DAG is
  trivially small and rarely changes.

  What about weird routing?

  Parallel compression: source → dry bus + send to compress bus → sum both on master. Just bus
  routing.

  Sidechain: a compressor in Bus A's chain needs to read Bus B's signal. The processor has an
  auxiliary input — a read-only reference to another bus's buffer. One special-case, not a general
  graph feature.

  Effects loop / external processing: send to hardware output, return from hardware input. A source
   that's an audio input feeding a bus.

  These all fit. The only thing that doesn't fit is truly arbitrary signal-graph routing (feedback
  loops, per-sample wiring between arbitrary processors). But that's not in your requirements list.

  The Python API this enables

  engine = Engine(sample_rate=44100, block_size=128)

  # Sources
  synth = engine.add_plugin("Diva.vst3")
  drums = engine.add_sampler("kit.wav")

  # Buses
  drum_bus = engine.add_bus("drums")
  verb_bus = engine.add_bus("reverb")
  master = engine.master

  # Insert chains
  drum_bus.chain.append(engine.add_plugin("SSL_EQ.vst3"))
  drum_bus.chain.append(engine.add_plugin("Comp.vst3"))
  verb_bus.chain.append(engine.add_plugin("Valhalla.vst3"))
  master.chain.append(engine.add_plugin("Limiter.vst3"))

  # Routing
  synth.route_to(master)
  synth.send(verb_bus, level=-6.0)     # send to reverb bus
  drums.route_to(drum_bus)
  drum_bus.route_to(master)
  verb_bus.route_to(master)

  # Live: insert a new effect
  drum_bus.chain.insert(1, engine.add_plugin("Saturator.vst3"))

  # Live: remove it
  drum_bus.chain.remove(saturator)

  This is closer to how people actually think about audio routing. No node IDs, no port names, no
  manual wiring of individual connections.

  What you'd lose

  - General-purpose graph routing (you're giving up the "patch bay" model)
  - Recursive composition (no nesting buses inside buses... though bus-to-bus routing covers most
  cases)
  - The ability to build totally novel signal topologies that don't fit the mixer model

  The question is whether any of those are in your actual requirements. Looking at your list — they
   aren't.
