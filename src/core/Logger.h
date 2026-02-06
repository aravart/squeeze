#pragma once

#include "core/SPSCQueue.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>

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

private:
    static long elapsedMs();

    static std::atomic<int> level_;
    static SPSCQueue<LogEntry, 1024> rtQueue_;
    static std::chrono::steady_clock::time_point startTime_;
};

} // namespace squeeze

#define SQ_LOG(fmt, ...) \
    do { if (squeeze::Logger::isEnabled()) squeeze::Logger::log(__FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)

#define SQ_LOG_RT(fmt, ...) \
    do { if (squeeze::Logger::isEnabled()) squeeze::Logger::logRT(__FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)

#define SQ_LOG_TRACE(fmt, ...) \
    do { if (squeeze::Logger::getLevel() >= squeeze::LogLevel::trace) squeeze::Logger::log(__FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)

#define SQ_LOG_RT_TRACE(fmt, ...) \
    do { if (squeeze::Logger::getLevel() >= squeeze::LogLevel::trace) squeeze::Logger::logRT(__FILE__, __LINE__, fmt, ##__VA_ARGS__); } while(0)
