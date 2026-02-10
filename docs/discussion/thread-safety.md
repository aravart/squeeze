# Thread Safety: SamplerNode Parameter and Buffer Writes

## Context

`SamplerNode::setParameter()` and `SamplerNode::setBuffer()` are called from the control thread (under `Engine::controlMutex_`). `SamplerNode::process()` reads these fields from the audio thread. This document explains why we don't use `std::atomic` or other synchronization for these writes.

## 1. Aligned Scalar Writes Are Naturally Atomic on Modern Hardware

On x86 and ARM (the only platforms we target), aligned stores of pointer-sized and smaller types are naturally atomic at the hardware level. A `float` (4 bytes, 4-byte aligned) and a `const Buffer*` (8 bytes, 8-byte aligned) are both written in a single instruction. The audio thread will always read either the old value or the new value, never a torn partial write.

This applies to:
- `SamplerParams` fields (`float`, `int`, `enum`) written by `setParameter()`
- `SamplerNode::buffer_` (`const Buffer*`) written by `setBuffer()`

## 2. Why We Don't Use `std::atomic` for SamplerParams

Strictly speaking, the C++ memory model classifies concurrent read/write of a non-atomic variable as a "data race" (undefined behavior). In practice, on x86/ARM with aligned scalar types, this is safe and is the industry-standard convention for audio engine parameters:

- **JUCE** uses plain `float` for `AudioParameterFloat::value`
- **Most DAW engines** (and audio plugin SDKs) rely on this for parameter reads
- Using `std::atomic` for 32+ parameter fields would be impractical and would add unnecessary acquire/release fences in the audio path
- The worst case is a one-callback delay in seeing a parameter change, which is inaudible

We accept this pragmatic trade-off: technically UB per the C++ standard, but guaranteed safe on our target hardware and consistent with audio industry practice.

## 3. Buffer Lifetime Is the Real Concern

The pointer store itself (`buffer_ = newBuffer`) is safe for the reasons above. The critical safety concern is **use-after-free**: if the underlying `Buffer` object is destroyed while the audio thread still references it.

### The problem scenario

1. User calls `sq.remove_buffer(id)` while a SamplerNode references that buffer
2. Engine erases the `unique_ptr<Buffer>`, freeing the memory
3. Audio thread reads from the freed buffer via `SamplerNode::buffer_` or `SamplerVoice::buffer_`

### The solution: null + defer

`Engine::removeBuffer()` does three things in order:

1. **Null out references**: Iterates all owned nodes, `dynamic_cast<SamplerNode*>` each, calls `setBuffer(nullptr)` on any that reference the buffer being removed
2. **Defer destruction**: Moves the `unique_ptr<Buffer>` to `pendingBufferDeletions_` instead of erasing immediately
3. **Push graph update**: The next `updateGraph()` or `processBlock()` will garbage-collect the deferred buffer after the audio thread has moved on

This ensures the audio thread never dereferences a freed buffer.

## 4. Voices Hold Their Own Buffer Pointer

`SamplerVoice::noteOn()` copies the buffer pointer at trigger time into the voice's internal state. Changing `SamplerNode::buffer_` only affects **future** noteOn events, not currently playing voices.

This means:
- **Assigning a new buffer**: Old voices keep playing from the old buffer until they release naturally. New notes use the new buffer.
- **Nulling the buffer**: Old voices keep playing. New noteOn events see `nullptr` and are silently ignored (VoiceAllocator's null-buffer guard).
- **Deferred destruction**: The old `Buffer` must outlive all voices that copied its pointer. The deferred deletion in `pendingBufferDeletions_` (cleaned up on the next graph push) provides this guarantee, since voices holding the old pointer will have released by the time the next graph snapshot is garbage-collected.

## Summary

| Concern | Mechanism | Safe? |
|---------|-----------|-------|
| Parameter float writes | Aligned scalar, single instruction | Yes (hardware atomic) |
| Buffer pointer write | Aligned pointer, single instruction | Yes (hardware atomic) |
| Buffer lifetime | Null references + defer destruction | Yes (explicit management) |
| Voice buffer pointer | Copied at noteOn, not updated | Yes (immutable per voice) |
