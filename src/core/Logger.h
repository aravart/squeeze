#pragma once

#include <atomic>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdio>

namespace squeeze {

struct LogEntry {
    char message[512];
    int level;
};

enum class LogLevel : int { off = 0, warn = 1, info = 2, debug = 3, trace = 4 };

class Logger {
public:
    static void setLevel(LogLevel level);
    static LogLevel getLevel();

    // Control-thread logging — direct fprintf to stderr (or callback)
    static void log(LogLevel level, const char* file, int line, const char* fmt, ...);

    // Audio-thread logging — lock-free push to internal ring buffer.
    // Caveat: uses vsnprintf, which is allocation-free for simple specifiers
    // (%d, %s, %x, %p) on mainstream platforms but not guaranteed by POSIX.
    // Avoid %f with extreme values and locale-dependent specifiers on the RT thread.
    static void logRT(LogLevel level, const char* file, int line, const char* fmt, ...);

    // Drain RT queue (control thread only)
    static void drain();

    // Optional callback for host language log capture
    using LogCallback = void(*)(int level, const char* message, void* userData);
    static void setCallback(LogCallback callback, void* userData);

private:
    static long elapsedMs();

    static std::atomic<int> level_;

    // Self-contained SPSC ring buffer (capacity 1024)
    static constexpr int kRingCapacity = 1024;
    static std::array<LogEntry, kRingCapacity + 1> ringBuffer_;
    static std::atomic<int> readPos_;
    static std::atomic<int> writePos_;

    static std::chrono::steady_clock::time_point startTime_;
    static LogCallback callback_;
    static void* callbackUserData_;
};

} // namespace squeeze

// --- Macros ---

#define SQ_WARN(fmt, ...) \
    do { if (squeeze::Logger::getLevel() >= squeeze::LogLevel::warn) \
        squeeze::Logger::log(squeeze::LogLevel::warn, __FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)

#define SQ_WARN_RT(fmt, ...) \
    do { if (squeeze::Logger::getLevel() >= squeeze::LogLevel::warn) \
        squeeze::Logger::logRT(squeeze::LogLevel::warn, __FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)

#define SQ_INFO(fmt, ...) \
    do { if (squeeze::Logger::getLevel() >= squeeze::LogLevel::info) \
        squeeze::Logger::log(squeeze::LogLevel::info, __FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)

#define SQ_INFO_RT(fmt, ...) \
    do { if (squeeze::Logger::getLevel() >= squeeze::LogLevel::info) \
        squeeze::Logger::logRT(squeeze::LogLevel::info, __FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)

#define SQ_DEBUG(fmt, ...) \
    do { if (squeeze::Logger::getLevel() >= squeeze::LogLevel::debug) \
        squeeze::Logger::log(squeeze::LogLevel::debug, __FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)

#define SQ_DEBUG_RT(fmt, ...) \
    do { if (squeeze::Logger::getLevel() >= squeeze::LogLevel::debug) \
        squeeze::Logger::logRT(squeeze::LogLevel::debug, __FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)

#define SQ_TRACE(fmt, ...) \
    do { if (squeeze::Logger::getLevel() >= squeeze::LogLevel::trace) \
        squeeze::Logger::log(squeeze::LogLevel::trace, __FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)

#define SQ_TRACE_RT(fmt, ...) \
    do { if (squeeze::Logger::getLevel() >= squeeze::LogLevel::trace) \
        squeeze::Logger::logRT(squeeze::LogLevel::trace, __FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)
