# Logger Specification

## Responsibilities
- Provide debug logging with millisecond timestamps to stderr
- Gate all logging behind an atomic enable/disable flag (disabled by default)
- Support two logging paths: control thread (direct write) and audio thread (lock-free queue)
- Drain queued RT log entries from the control thread

## Interface

```cpp
namespace squeeze {

struct LogEntry {
    char message[256];
};

// Log levels: 0 = off, 1 = debug (-d), 2 = trace (-dd)
enum class LogLevel : int { off = 0, debug = 1, trace = 2 };

class Logger {
public:
    static void enable();                   // sets level to debug
    static void disable();                  // sets level to off
    static bool isEnabled();                // true if level >= debug
    static void setLevel(LogLevel level);
    static LogLevel getLevel();

    // Control thread: formats and writes directly to stderr
    static void log(const char* file, int line, const char* fmt, ...);

    // Audio thread: formats into LogEntry, pushes to lock-free queue
    static void logRT(const char* file, int line, const char* fmt, ...);

    // Control thread: pops all entries from RT queue and writes to stderr
    static void drain();
};

} // namespace squeeze

// Macros — debug level (short-circuit when disabled)
#define SQ_LOG(fmt, ...)    do { if (squeeze::Logger::isEnabled()) squeeze::Logger::log(__FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)
#define SQ_LOG_RT(fmt, ...) do { if (squeeze::Logger::isEnabled()) squeeze::Logger::logRT(__FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)

// Macros — trace level (short-circuit when level < trace)
#define SQ_LOG_TRACE(fmt, ...)    do { if (squeeze::Logger::getLevel() >= squeeze::LogLevel::trace) squeeze::Logger::log(__FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)
#define SQ_LOG_RT_TRACE(fmt, ...) do { if (squeeze::Logger::getLevel() >= squeeze::LogLevel::trace) squeeze::Logger::logRT(__FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)
```

## Invariants
- Default state is disabled; no output unless explicitly enabled
- `isEnabled()` uses relaxed atomic load (safe to call from any thread)
- `log()` writes one complete line to stderr per call
- `logRT()` never allocates, never blocks, never does I/O
- `logRT()` silently drops the entry if the queue is full
- `drain()` is safe to call when the queue is empty (no-op)
- Messages longer than 256 bytes (including header) are truncated, never overflow

## Message Format
```
[000042][CT] Engine.cpp:25 updateGraph: 3 nodes
[000042][RT] Engine.cpp:159 snapshot swap
```
- `[NNNNNN]` — milliseconds since process start, zero-padded to 6 digits
- `[CT]` or `[RT]` — thread tag (control or realtime)
- Basename of source file + `:` + line number
- Space + user message

## Error Conditions
- Queue full on `logRT()`: entry is silently dropped (no crash, no block)
- `drain()` on empty queue: returns immediately
- `log()`/`logRT()` when disabled: macros short-circuit, no formatting occurs

## Does NOT Handle
- Log file output — stderr only
- Log rotation or size limits
- Thread identification beyond CT/RT tags
- Filtering by source file or component

## Dependencies
- `SPSCQueue` (from core, already exists)
- `<chrono>`, `<cstdio>`, `<cstdarg>`, `<atomic>`

## Thread Safety
- `enable()`, `disable()`, `isEnabled()`: safe from any thread (atomic)
- `log()`: called from control thread only
- `logRT()`: called from audio thread only (producer side of SPSC queue)
- `drain()`: called from control thread only (consumer side of SPSC queue)

## Example Usage

```cpp
// In main.cpp
Logger::setLevel(LogLevel::debug);  // -d flag
Logger::setLevel(LogLevel::trace);  // -dd flag

// In Engine.cpp (control thread) — fires at debug level
SQ_LOG("updateGraph: %d nodes", graph.getNodeCount());

// In Engine.cpp (audio thread, inside processBlock) — fires at debug level
SQ_LOG_RT("snapshot swap");

// In MidiInputNode.cpp — fires only at trace level
SQ_LOG_RT_TRACE("MIDI [%s] note-on ch=%d note=%d vel=%d", ...);

// In main loop
Logger::drain();
```
