# Logger Specification

## Responsibilities
- Provide timestamped debug logging to stderr
- Gate logging behind an atomic level flag (default: `warn`)
- Support five levels: `off`, `warn`, `info`, `debug`, `trace`
- Support two logging paths: control thread (direct write) and audio thread (lock-free queue)
- Drain queued RT log entries from the control thread
- Optionally forward log messages to a user-supplied callback (for host language integration)

## Interface

```cpp
namespace squeeze {

struct LogEntry { char message[512]; int level; };

enum class LogLevel : int { off = 0, warn = 1, info = 2, debug = 3, trace = 4 };

class Logger {
public:
    // Level control — safe from any thread (atomic relaxed)
    static void setLevel(LogLevel level);
    static LogLevel getLevel();

    // Control-thread logging — direct fprintf to stderr
    static void log(LogLevel level, const char* file, int line, const char* fmt, ...);

    // Audio-thread logging — lock-free push to internal ring buffer
    static void logRT(LogLevel level, const char* file, int line, const char* fmt, ...);

    // Drain RT queue to stderr (control thread only)
    static void drain();

    // Optional callback for host language log capture
    using LogCallback = void(*)(int level, const char* message, void* userData);
    static void setCallback(LogCallback callback, void* userData);
};

} // namespace squeeze
```

### Macros

```cpp
// Warn — fires at warn, info, debug, trace
SQ_WARN(fmt, ...)        // CT
SQ_WARN_RT(fmt, ...)     // RT

// Info — fires at info, debug, trace
SQ_INFO(fmt, ...)        // CT
SQ_INFO_RT(fmt, ...)     // RT

// Debug — fires at debug and trace
SQ_DEBUG(fmt, ...)       // CT
SQ_DEBUG_RT(fmt, ...)    // RT

// Trace — fires only at trace
SQ_TRACE(fmt, ...)       // CT
SQ_TRACE_RT(fmt, ...)    // RT
```

All macros short-circuit: if the current level is below the macro's threshold, no formatting occurs, no function is called.

## Log Levels

| Level | Int | Default | Purpose |
|-------|-----|---------|---------|
| `off`   | 0 | | Silence all output |
| `warn`  | 1 | **yes** | Problems requiring attention: xruns, queue overflows, dropped events, failed operations |
| `info`  | 2 | | High-level operational milestones: engine start/stop, audio device open/close, plugin loaded, graph rebuilt |
| `debug` | 3 | | Detailed internal operations: every connect/disconnect, node add/remove, buffer load, parameter changes, snapshot swaps |
| `trace` | 4 | | Per-message granularity: individual MIDI messages, per-event scheduling, per-block details |

### Level Selection Guidelines

- **warn**: Something went wrong or was dropped. Would want to see in production.
- **info**: "What happened" — useful for understanding engine behavior without drowning in detail. A human reading info-level output should be able to follow the high-level narrative of a session.
- **debug**: "How it happened" — every API call, every topology mutation, every state transition.
- **trace**: "Everything" — per-MIDI-message, per-event, per-block. Only for targeted debugging.

## Message Format

```
[000042][CT][info] Engine.cpp:25 start: sr=44100 bs=512
[000042][RT][warn] PerfMonitor.cpp:109 xrun: 850us (budget 725us), total 3
```

- `[NNNNNN]` — milliseconds since process start, zero-padded to 6 digits
- `[CT]` or `[RT]` — control thread vs. realtime thread
- `[level]` — log level tag
- Basename of source file `:` line number
- Space + user message

## Invariants
- Default level is `warn`
- `setLevel()` / `getLevel()` use relaxed atomic load/store (safe from any thread)
- `log()` writes one complete line to stderr per call
- `logRT()` never allocates, never blocks, never does I/O
- `logRT()` silently drops the entry if the internal ring buffer is full
- `drain()` is safe to call when the queue is empty (no-op)
- Messages exceeding 512 bytes are truncated, never overflow
- When a callback is set, messages are forwarded to the callback instead of (not in addition to) stderr
- The callback is invoked from the control thread only (via `log()` and `drain()`), never from the audio thread

## Error Conditions
- Ring buffer full on `logRT()`: silently drop, no crash, no block
- `drain()` on empty queue: returns immediately
- `setCallback(nullptr, nullptr)`: reverts to stderr output

## Does NOT Handle
- Log file output (stderr or callback only)
- Log rotation or size limits
- Thread identification beyond CT/RT tags
- Filtering by source file or component
- Colorized output (host language can colorize via callback)

## Dependencies
- None. Logger uses its own internal fixed-size lock-free SPSC ring buffer (simple `std::atomic` head/tail over a `std::array<LogEntry, 1024>`). It does not depend on the SPSCQueue component.
- `<chrono>`, `<cstdio>`, `<cstdarg>`, `<atomic>`, `<array>`

## Thread Safety

| Method | Thread | Mechanism |
|--------|--------|-----------|
| `setLevel()` / `getLevel()` | Any | `std::atomic<int>`, relaxed ordering |
| `log()` | Control thread only | Direct `fprintf` or callback invocation |
| `logRT()` | Audio thread only | Lock-free push: `vsnprintf` into stack buffer, copy to ring slot |
| `drain()` | Control thread only | Single-consumer pop from ring buffer, then `fprintf` or callback |
| `setCallback()` | Control thread only | Must not be called while audio is running |

### RT Safety of `logRT()`
- No mutex or blocking
- No `fprintf` or I/O (deferred to `drain()`)
- `vsnprintf` into a fixed stack buffer (`char[400]`)
- `snprintf` into the `LogEntry` struct (fixed 512 bytes)
- Atomic head/tail for lock-free ring buffer push

**Caveat:** `vsnprintf` is not guaranteed allocation-free by POSIX. On mainstream platforms (glibc, macOS libc, MSVC) it does not allocate for simple format specifiers (`%d`, `%s`, `%x`, `%p`), but locale-dependent or exotic specifiers (e.g. `%f` with extreme values, `%ls`) may allocate on some implementations. Callers on the audio thread should stick to integer and string format specifiers. This is a pragmatic tradeoff — fully safe deferred formatting (pushing raw arguments and formatting in `drain()`) would add substantial complexity for minimal real-world benefit.

## C ABI

```c
// Set log level globally. 0=off, 1=warn, 2=info, 3=debug, 4=trace.
// Does not require an SqEngine handle.
void sq_set_log_level(int level);

// Set a callback to receive log messages. Pass NULL to revert to stderr.
// The callback receives: level (int), message (const char*), userData.
// Callback is invoked from the control thread only.
void sq_set_log_callback(void (*callback)(int level, const char* message, void* user_data), void* user_data);
```

## Python API

```python
import squeeze

squeeze.set_log_level(3)  # debug

# Optional: capture logs in Python
def my_handler(level: int, message: str) -> None:
    print(f"[{level}] {message}")

squeeze.set_log_callback(my_handler)
```

## Example Usage

```cpp
#include "core/Logger.h"

// In Engine::start()
SQ_INFO("start: sr=%.0f bs=%d", sampleRate, blockSize);

// In Engine::processBlock() — RT path
SQ_DEBUG_RT("snapshot swap");

// In MidiRouter RT callback — very verbose
SQ_TRACE_RT("MIDI note-on ch=%d note=%d vel=%d", ch, note, vel);

// In PerfMonitor — xrun detected on audio thread
SQ_WARN_RT("xrun: %.0fus (budget %.0fus), total %d", durationUs, budgetUs, count);

// Main loop
while (running) {
    messageManager->runDispatchLoopUntil(10);
    Logger::drain();
}
```
