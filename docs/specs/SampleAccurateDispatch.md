# Sample-Accurate Dispatch Specification

## Problem

Events from EventQueue arrive with sample offsets within a block. MIDI events are naturally sample-accurate (MidiBuffer carries offsets), but parameter changes via `setParameter()` take effect instantly for the rest of the block. Without intervention, a parameter change at sample 57 of a 512-sample block affects all 512 samples — not just the final 455.

## Technique: Sub-Block Splitting

Split the block at each parameter change point. Process audio in segments, applying parameter changes between segments.

Given a block of N samples and parameter changes at sample offsets S1, S2, ... (sorted):

```
process [0, S1)        with original parameters
setParameter(...)      at S1
process [S1, S2)       with updated parameters
setParameter(...)      at S2
process [S2, N)        with updated parameters
```

Each call to `process()` sees a shorter block. The node produces audio in segments that are concatenated in the output buffer. MIDI events in each segment are filtered to only include those within the segment's sample range, with offsets adjusted to be relative to the segment start.

## Where Splitting Happens

**Engine** performs the splitting in `processBlock`, per node. Nodes are unaware that they're being called with sub-blocks — they see a normal `ProcessContext` with a smaller `numSamples`.

```
for each node in execution order:
    paramChanges = resolved events for this node where type == paramChange
    midiEvents   = resolved events for this node where type is MIDI

    1. Write MIDI events into the node's input MidiBuffer at their sample offsets
       (merged with any MIDI from graph connections)

    2. If no paramChanges:
         call node->process() once with full block — done

    3. If paramChanges exist:
         sort by sampleOffset
         currentSample = 0

         for each paramChange at offset S:
             if S > currentSample:
                 call node->process() for [currentSample, S)
             node->setParameter(paramChange.paramIndex, paramChange.value)
             currentSample = S

         if currentSample < numSamples:
             call node->process() for [currentSample, numSamples)
```

## Sub-Block ProcessContext

Each sub-block call needs a `ProcessContext` that views a slice of the full-block buffers:

- **Audio buffers**: offset into the full buffer. `juce::AudioBuffer` supports this via the constructor that references existing memory at an offset: `AudioBuffer(channelData + offset, numChannels, subBlockSize)`.
- **MIDI buffer**: filtered to events in `[currentSample, nextSample)` with offsets adjusted to `[0, subBlockSize)`. Uses a scratch `MidiBuffer` in the snapshot.
- **numSamples**: the sub-block size.

## How Each Event Type Achieves Sample Accuracy

### MIDI (noteOn, noteOff, CC)

MIDI events are written into `juce::MidiBuffer` at their resolved sample offsets. This is natively sample-accurate — MidiBuffer was designed for this.

- **PluginNode**: passes MidiBuffer to `processor_->processBlock()`. The VST plugin reads sample offsets from the buffer and handles timing internally. No splitting needed for MIDI alone.
- **SamplerNode**: iterates MidiBuffer in `process()`, renders sub-blocks between events, applies note-on/off at the exact sample. Already implemented.
- **Any future node**: receives MidiBuffer with offsets, handles as appropriate.

When Engine splits blocks for parameter changes, MIDI events are partitioned into the corresponding sub-block's MidiBuffer with adjusted offsets. A MIDI event at sample 100 in a sub-block starting at sample 80 appears at offset 20 in that sub-block's MidiBuffer.

### Parameter changes (paramChange)

Parameter changes are the reason splitting exists. `setParameter()` has no sample offset concept — it takes effect immediately. Splitting ensures `setParameter()` is called at the right moment between sub-blocks:

```
process [0, 57)            // old parameter value active
setParameter(2, 0.3)       // change takes effect
process [57, 512)          // new parameter value active
```

For nodes with internal smoothing (many VST plugins smooth parameter changes over a few ms), the smoothing starts from the sub-block boundary rather than the full block boundary. This is more accurate than applying at sample 0.

### Multiple events at the same sample offset

When MIDI and parameter changes share the same sample offset, parameter changes are applied first (before the sub-block that starts at that offset). This ensures the parameter value is set before any MIDI event triggers sound at that sample.

When multiple parameter changes target the same parameter at the same offset, the last one wins (sorted by schedule order).

## Impact on Nodes

Nodes **do not need to know** about sub-block splitting. The contract:

- `process()` may be called with any `numSamples` between 1 and the full block size
- `process()` may be called multiple times per audio block
- `setParameter()` may be called between `process()` calls within a block
- Audio buffer pointers may differ between calls (they point to different offsets in the full buffer)
- MIDI buffer contents change between calls

These are **not new requirements**. JUCE hosts routinely call `processBlock()` with varying sizes, and `setParameter()` is always callable between process calls. SamplerNode already handles arbitrary `numSamples`.

Nodes that maintain internal state across process calls (e.g., filter history, envelope position) naturally carry that state across sub-blocks — each sub-block picks up where the last left off.

## When Splitting is Skipped

Splitting only happens when a node has parameter change events from EventQueue in the current block. The common cases require no splitting:

- Node with no events: one `process()` call, full block. Most nodes, most blocks.
- Node with only MIDI events: one `process()` call, full block. MidiBuffer carries the offsets.
- Node with only param changes from Scheduler (knob turns): applied at block boundary via existing Scheduler path. No splitting.

Splitting is only triggered by **beat-synced parameter changes from EventQueue** — parameter locks, automation lanes, etc.

## Invariants

- The sum of all sub-block sizes equals the full block size (no samples lost or doubled)
- MIDI events appear in exactly one sub-block, at the correct adjusted offset
- Parameter changes are applied between sub-blocks, never during
- Sub-blocks are processed in order (ascending sample offset)
- A node's internal state is continuous across sub-blocks within a block
- If no parameter changes exist for a node, it is called exactly once with the full block (zero overhead)

## Error Conditions

- Parameter change at offset >= numSamples: clamped to numSamples - 1
- Parameter change at offset 0: setParameter is called before the first process() call (equivalent to block-boundary application)
- Sub-block size of 0 (two events at same offset): the zero-length process() call is skipped; both parameter changes are applied in sequence

## Does NOT Handle

- Tempo-synced parameter smoothing (that's a node-internal concern)
- Sample-accurate graph topology changes (graph swaps are always block-boundary)
- Audio-rate modulation (parameter changes at every sample — use audio-rate connections for that)

## MIDI Merging

A node may receive MIDI from two sources simultaneously:

1. **Graph connections**: MIDI routed from MidiInputNode (or other nodes) via the graph's connection list. This is the existing mechanism — Engine resolves MIDI sources per the connection topology and writes into the node's input MidiBuffer.

2. **EventQueue**: MIDI events (noteOn, noteOff, CC) resolved from beat-timestamped ScheduledEvents. These arrive as ResolvedEvents with sample offsets.

Engine merges both sources into a single MidiBuffer before calling `node->process()`. EventQueue MIDI events are added to the same MidiBuffer at their resolved sample offsets, interleaved with graph-connected MIDI. The MidiBuffer handles ordering by sample position natively.

When sub-block splitting is active (due to parameter changes), the merged MidiBuffer is partitioned into sub-blocks as described above — both graph-connected and EventQueue MIDI events follow the same partitioning logic.

## AudioPlayHead During Sub-Blocks

Transport's `AudioPlayHead::getPosition()` reflects the position at the start of the full block, not the sub-block. This is standard DAW behavior — hosts don't update AudioPlayHead mid-block. VST plugins that need sub-sample timing use MidiBuffer offsets, not AudioPlayHead position.

## Dependencies

- ProcessContext (from Node.h)
- juce::AudioBuffer (supports offset construction)
- juce::MidiBuffer (supports addEvents with offset filtering)

## Example

At 120 BPM, 44100 Hz, block size 512. A Lua sequencer schedules:
- noteOn(C4) at beat 4.0 → resolves to sample offset 57
- paramChange(filter_cutoff, 0.3) at beat 4.0 → resolves to sample offset 57
- paramChange(filter_cutoff, 0.8) at beat 4.25 → resolves to sample offset 340

Engine processes the sampler node:

```
1. Write noteOn(C4) into MidiBuffer at offset 57

2. Parameter changes at offsets [57, 340] → split needed

3. process([0, 57))
     MidiBuffer: empty (noteOn is at 57, not in [0, 57))
     filter_cutoff still at previous value
     Sampler renders 57 samples of whatever was already playing

4. setParameter(filter_cutoff, 0.3)

5. process([57, 340))
     MidiBuffer: noteOn(C4) at adjusted offset 0
     filter_cutoff = 0.3
     Sampler triggers C4 at sample 0 of this sub-block, renders 283 samples

6. setParameter(filter_cutoff, 0.8)

7. process([340, 512))
     MidiBuffer: empty
     filter_cutoff = 0.8
     Sampler continues rendering C4 for 172 samples with new cutoff
```

Result: note triggers at exactly sample 57, filter sweeps at the musically intended moments.

## processBlock Integration Flow

This shows how Transport, EventQueue, and SampleAccurateDispatch fit together in Engine's `processBlock`:

```
processBlock(outputBuffer, numSamples):

    1. Execute Scheduler commands (graph swaps, knob turns, transport control)
       — instant, block-boundary, existing mechanism

    2. Transport: advance(numSamples)
       — updates position, detects loop wrap

    3. EventQueue: retrieve(blockStartBeats, blockEndBeats,
                            transport.didLoopWrap(),
                            loopStartBeats, loopEndBeats,
                            numSamples, tempo, sampleRate,
                            resolvedEvents, maxEvents)
       — drains SPSC queue, matches events to block, resolves sample offsets

    4. For each node in execution order:
       a. Resolve MIDI input from graph connections (existing mechanism)
       b. Merge EventQueue MIDI events (noteOn/noteOff/CC) into MidiBuffer
       c. Collect EventQueue paramChange events for this node

       d. If no paramChanges:
            call node->process() once with full block
          Else:
            sub-block split per SampleAccurateDispatch algorithm
            (setParameter between sub-blocks, MIDI partitioned per sub-block)

    5. Sum leaf node outputs to device buffer (existing mechanism)
```

Steps 1-3 happen once per block. Step 4 is per-node. The existing processBlock logic for graph traversal, MIDI routing, and leaf-node summing is unchanged — EventQueue and sub-block splitting are additions to step 4.
