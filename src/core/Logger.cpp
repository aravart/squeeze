#include "core/Logger.h"

#include <cstring>

namespace squeeze {

// --- Static storage ---

std::atomic<int> Logger::level_{static_cast<int>(LogLevel::warn)};

std::array<LogEntry, Logger::kRingCapacity + 1> Logger::ringBuffer_;
std::atomic<int> Logger::readPos_{0};
std::atomic<int> Logger::writePos_{0};

std::chrono::steady_clock::time_point Logger::startTime_ = std::chrono::steady_clock::now();

Logger::LogCallback Logger::callback_ = nullptr;
void* Logger::callbackUserData_ = nullptr;

// --- Helpers ---

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
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_);
    return static_cast<long>(ms.count());
}

static const char* levelTag(LogLevel level)
{
    switch (level)
    {
        case LogLevel::warn:  return "warn";
        case LogLevel::info:  return "info";
        case LogLevel::debug: return "debug";
        case LogLevel::trace: return "trace";
        default:              return "???";
    }
}

// --- Public API ---

void Logger::setLevel(LogLevel level)
{
    level_.store(static_cast<int>(level), std::memory_order_relaxed);
}

LogLevel Logger::getLevel()
{
    return static_cast<LogLevel>(level_.load(std::memory_order_relaxed));
}

void Logger::log(LogLevel level, const char* file, int line, const char* fmt, ...)
{
    char userMsg[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(userMsg, sizeof(userMsg), fmt, args);
    va_end(args);

    char fullMsg[256];
    snprintf(fullMsg, sizeof(fullMsg), "[%06ld][CT][%s] %s:%d %s",
             elapsedMs(), levelTag(level), basename(file), line, userMsg);

    if (callback_)
        callback_(static_cast<int>(level), fullMsg, callbackUserData_);
    else
        fprintf(stderr, "%s\n", fullMsg);
}

void Logger::logRT(LogLevel level, const char* file, int line, const char* fmt, ...)
{
    int w = writePos_.load(std::memory_order_relaxed);
    int nextW = (w + 1) % (kRingCapacity + 1);
    if (nextW == readPos_.load(std::memory_order_acquire))
        return; // full — silently drop

    char userMsg[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(userMsg, sizeof(userMsg), fmt, args);
    va_end(args);

    snprintf(ringBuffer_[w].message, sizeof(LogEntry::message),
             "[%06ld][RT][%s] %s:%d %s",
             elapsedMs(), levelTag(level), basename(file), line, userMsg);
    ringBuffer_[w].level = static_cast<int>(level);

    writePos_.store(nextW, std::memory_order_release);
}

void Logger::drain()
{
    while (true)
    {
        int r = readPos_.load(std::memory_order_relaxed);
        if (r == writePos_.load(std::memory_order_acquire))
            break;

        if (callback_)
            callback_(ringBuffer_[r].level, ringBuffer_[r].message, callbackUserData_);
        else
            fprintf(stderr, "%s\n", ringBuffer_[r].message);

        readPos_.store((r + 1) % (kRingCapacity + 1), std::memory_order_release);
    }
}

void Logger::setCallback(LogCallback callback, void* userData)
{
    callback_ = callback;
    callbackUserData_ = userData;
}

} // namespace squeeze
