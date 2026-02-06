#include "core/Logger.h"

#include <cstring>

namespace squeeze {

std::atomic<int> Logger::level_{0};
SPSCQueue<LogEntry, 1024> Logger::rtQueue_;
std::chrono::steady_clock::time_point Logger::startTime_ = std::chrono::steady_clock::now();

static const char* basename(const char* path)
{
    const char* last = path;
    for (const char* p = path; *p; ++p)
    {
        if (*p == '/')
            last = p + 1;
    }
    return last;
}

long Logger::elapsedMs()
{
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - startTime_);
    return static_cast<long>(ms.count());
}

void Logger::enable()    { level_.store(static_cast<int>(LogLevel::debug), std::memory_order_relaxed); }
void Logger::disable()   { level_.store(static_cast<int>(LogLevel::off), std::memory_order_relaxed); }
bool Logger::isEnabled() { return level_.load(std::memory_order_relaxed) >= static_cast<int>(LogLevel::debug); }

void Logger::setLevel(LogLevel level) { level_.store(static_cast<int>(level), std::memory_order_relaxed); }
LogLevel Logger::getLevel() { return static_cast<LogLevel>(level_.load(std::memory_order_relaxed)); }

void Logger::log(const char* file, int line, const char* fmt, ...)
{
    char userMsg[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(userMsg, sizeof(userMsg), fmt, args);
    va_end(args);

    fprintf(stderr, "\033[2m[%06ld][CT] %s:%d %s\033[0m\n", elapsedMs(), basename(file), line, userMsg);
}

void Logger::logRT(const char* file, int line, const char* fmt, ...)
{
    LogEntry entry;

    char userMsg[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(userMsg, sizeof(userMsg), fmt, args);
    va_end(args);

    snprintf(entry.message, sizeof(entry.message),
             "[%06ld][RT] %s:%d %s", elapsedMs(), basename(file), line, userMsg);

    rtQueue_.tryPush(entry);  // silently drop if full
}

void Logger::drain()
{
    LogEntry entry;
    while (rtQueue_.tryPop(entry))
        fprintf(stderr, "\033[2m%s\033[0m\n", entry.message);
}

} // namespace squeeze
