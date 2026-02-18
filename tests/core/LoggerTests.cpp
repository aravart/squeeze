#include <catch2/catch_test_macros.hpp>

#include "core/Logger.h"

#include <cstring>
#include <string>
#include <vector>

using namespace squeeze;

// --- Callback test helpers ---

struct CapturedLog {
    int level;
    std::string message;
};

static std::vector<CapturedLog> g_captured;

static void captureCallback(int level, const char* message, void* /*userData*/)
{
    g_captured.push_back({level, message});
}

static void resetLogger()
{
    Logger::setCallback(nullptr, nullptr);
    Logger::setLevel(LogLevel::warn);
    Logger::drain(); // flush any leftover RT entries
    g_captured.clear();
}

// --- Level tests ---

TEST_CASE("Logger default level is warn")
{
    resetLogger();
    REQUIRE(Logger::getLevel() == LogLevel::warn);
}

TEST_CASE("Logger setLevel and getLevel round-trip")
{
    resetLogger();

    Logger::setLevel(LogLevel::off);
    REQUIRE(Logger::getLevel() == LogLevel::off);

    Logger::setLevel(LogLevel::warn);
    REQUIRE(Logger::getLevel() == LogLevel::warn);

    Logger::setLevel(LogLevel::info);
    REQUIRE(Logger::getLevel() == LogLevel::info);

    Logger::setLevel(LogLevel::debug);
    REQUIRE(Logger::getLevel() == LogLevel::debug);

    Logger::setLevel(LogLevel::trace);
    REQUIRE(Logger::getLevel() == LogLevel::trace);

    resetLogger();
}

// --- CT macro gating tests ---

TEST_CASE("SQ_WARN fires at warn level")
{
    resetLogger();
    Logger::setLevel(LogLevel::warn);
    Logger::setCallback(captureCallback, nullptr);

    SQ_WARN("warn msg %d", 42);
    REQUIRE(g_captured.size() == 1);
    REQUIRE(g_captured[0].message.find("[warn]") != std::string::npos);
    REQUIRE(g_captured[0].message.find("warn msg 42") != std::string::npos);
    REQUIRE(g_captured[0].level == static_cast<int>(LogLevel::warn));

    resetLogger();
}

TEST_CASE("SQ_WARN is a no-op when level is off")
{
    resetLogger();
    Logger::setLevel(LogLevel::off);
    Logger::setCallback(captureCallback, nullptr);

    SQ_WARN("should not appear");
    REQUIRE(g_captured.empty());

    resetLogger();
}

TEST_CASE("SQ_INFO fires at info level")
{
    resetLogger();
    Logger::setLevel(LogLevel::info);
    Logger::setCallback(captureCallback, nullptr);

    SQ_INFO("info msg %d", 7);
    REQUIRE(g_captured.size() == 1);
    REQUIRE(g_captured[0].message.find("[info]") != std::string::npos);
    REQUIRE(g_captured[0].level == static_cast<int>(LogLevel::info));

    resetLogger();
}

TEST_CASE("SQ_INFO is suppressed at warn level")
{
    resetLogger();
    Logger::setLevel(LogLevel::warn);
    Logger::setCallback(captureCallback, nullptr);

    SQ_INFO("should not appear");
    REQUIRE(g_captured.empty());

    resetLogger();
}

TEST_CASE("SQ_DEBUG fires at debug level")
{
    resetLogger();
    Logger::setLevel(LogLevel::debug);
    Logger::setCallback(captureCallback, nullptr);

    SQ_DEBUG("debug msg %d", 99);
    REQUIRE(g_captured.size() == 1);
    REQUIRE(g_captured[0].message.find("[debug]") != std::string::npos);

    resetLogger();
}

TEST_CASE("SQ_DEBUG is suppressed at info level")
{
    resetLogger();
    Logger::setLevel(LogLevel::info);
    Logger::setCallback(captureCallback, nullptr);

    SQ_DEBUG("should not appear");
    REQUIRE(g_captured.empty());

    resetLogger();
}

TEST_CASE("SQ_TRACE fires at trace level")
{
    resetLogger();
    Logger::setLevel(LogLevel::trace);
    Logger::setCallback(captureCallback, nullptr);

    SQ_TRACE("trace msg %d", 1);
    REQUIRE(g_captured.size() == 1);
    REQUIRE(g_captured[0].message.find("[trace]") != std::string::npos);

    resetLogger();
}

TEST_CASE("SQ_TRACE is suppressed at debug level")
{
    resetLogger();
    Logger::setLevel(LogLevel::debug);
    Logger::setCallback(captureCallback, nullptr);

    SQ_TRACE("should not appear");
    REQUIRE(g_captured.empty());

    resetLogger();
}

// --- RT macro gating tests ---

TEST_CASE("SQ_WARN_RT pushes entry and drain writes it")
{
    resetLogger();
    Logger::setLevel(LogLevel::warn);
    Logger::setCallback(captureCallback, nullptr);

    SQ_WARN_RT("rt warn %d", 77);
    REQUIRE(g_captured.empty()); // not yet drained

    Logger::drain();
    REQUIRE(g_captured.size() == 1);
    REQUIRE(g_captured[0].message.find("[RT]") != std::string::npos);
    REQUIRE(g_captured[0].message.find("[warn]") != std::string::npos);
    REQUIRE(g_captured[0].message.find("rt warn 77") != std::string::npos);
    REQUIRE(g_captured[0].level == static_cast<int>(LogLevel::warn));

    resetLogger();
}

TEST_CASE("SQ_WARN_RT is a no-op when level is off")
{
    resetLogger();
    Logger::setLevel(LogLevel::off);
    Logger::setCallback(captureCallback, nullptr);

    SQ_WARN_RT("should not appear");
    Logger::drain();
    REQUIRE(g_captured.empty());

    resetLogger();
}

TEST_CASE("SQ_INFO_RT fires at info level and drains")
{
    resetLogger();
    Logger::setLevel(LogLevel::info);
    Logger::setCallback(captureCallback, nullptr);

    SQ_INFO_RT("rt info msg");
    Logger::drain();
    REQUIRE(g_captured.size() == 1);
    REQUIRE(g_captured[0].message.find("[info]") != std::string::npos);

    resetLogger();
}

TEST_CASE("SQ_INFO_RT is suppressed at warn level")
{
    resetLogger();
    Logger::setLevel(LogLevel::warn);
    Logger::setCallback(captureCallback, nullptr);

    SQ_INFO_RT("should not appear");
    Logger::drain();
    REQUIRE(g_captured.empty());

    resetLogger();
}

TEST_CASE("SQ_DEBUG_RT fires at debug level and drains")
{
    resetLogger();
    Logger::setLevel(LogLevel::debug);
    Logger::setCallback(captureCallback, nullptr);

    SQ_DEBUG_RT("rt debug msg");
    Logger::drain();
    REQUIRE(g_captured.size() == 1);
    REQUIRE(g_captured[0].message.find("[debug]") != std::string::npos);

    resetLogger();
}

TEST_CASE("SQ_DEBUG_RT is suppressed at warn level")
{
    resetLogger();
    Logger::setLevel(LogLevel::warn);
    Logger::setCallback(captureCallback, nullptr);

    SQ_DEBUG_RT("should not appear");
    Logger::drain();
    REQUIRE(g_captured.empty());

    resetLogger();
}

TEST_CASE("SQ_TRACE_RT fires at trace level and drains")
{
    resetLogger();
    Logger::setLevel(LogLevel::trace);
    Logger::setCallback(captureCallback, nullptr);

    SQ_TRACE_RT("rt trace msg");
    Logger::drain();
    REQUIRE(g_captured.size() == 1);
    REQUIRE(g_captured[0].message.find("[trace]") != std::string::npos);

    resetLogger();
}

TEST_CASE("SQ_TRACE_RT is suppressed at debug level")
{
    resetLogger();
    Logger::setLevel(LogLevel::debug);
    Logger::setCallback(captureCallback, nullptr);

    SQ_TRACE_RT("should not appear");
    Logger::drain();
    REQUIRE(g_captured.empty());

    resetLogger();
}

// --- Message format tests ---

TEST_CASE("CT log message contains timestamp, CT tag, level, file, and user message")
{
    resetLogger();
    Logger::setLevel(LogLevel::debug);
    Logger::setCallback(captureCallback, nullptr);

    SQ_DEBUG("format test %d", 123);
    REQUIRE(g_captured.size() == 1);

    const auto& msg = g_captured[0].message;
    REQUIRE(msg.find("[CT]") != std::string::npos);
    REQUIRE(msg.find("[debug]") != std::string::npos);
    REQUIRE(msg.find("LoggerTests.cpp") != std::string::npos);
    REQUIRE(msg.find("format test 123") != std::string::npos);
    // Timestamp: [NNNNNN] at the start
    REQUIRE(msg[0] == '[');

    resetLogger();
}

TEST_CASE("RT log message contains timestamp, RT tag, level, file, and user message")
{
    resetLogger();
    Logger::setLevel(LogLevel::debug);
    Logger::setCallback(captureCallback, nullptr);

    SQ_DEBUG_RT("rt format test %d", 456);
    Logger::drain();
    REQUIRE(g_captured.size() == 1);

    const auto& msg = g_captured[0].message;
    REQUIRE(msg.find("[RT]") != std::string::npos);
    REQUIRE(msg.find("[debug]") != std::string::npos);
    REQUIRE(msg.find("LoggerTests.cpp") != std::string::npos);
    REQUIRE(msg.find("rt format test 456") != std::string::npos);

    resetLogger();
}

// --- Callback tests ---

TEST_CASE("setCallback captures log messages")
{
    resetLogger();
    Logger::setLevel(LogLevel::debug);
    Logger::setCallback(captureCallback, nullptr);

    SQ_DEBUG("callback test");
    REQUIRE(g_captured.size() == 1);
    REQUIRE(g_captured[0].message.find("callback test") != std::string::npos);

    resetLogger();
}

TEST_CASE("setCallback captures drain messages")
{
    resetLogger();
    Logger::setLevel(LogLevel::debug);
    Logger::setCallback(captureCallback, nullptr);

    SQ_DEBUG_RT("drain callback test");
    Logger::drain();
    REQUIRE(g_captured.size() == 1);
    REQUIRE(g_captured[0].message.find("drain callback test") != std::string::npos);

    resetLogger();
}

TEST_CASE("setCallback nullptr reverts to stderr")
{
    resetLogger();
    Logger::setLevel(LogLevel::debug);
    Logger::setCallback(captureCallback, nullptr);
    Logger::setCallback(nullptr, nullptr);

    // Should not crash, should go to stderr (not captured)
    SQ_DEBUG("after clear");
    REQUIRE(g_captured.empty());

    resetLogger();
}

TEST_CASE("callback receives correct level for CT log")
{
    resetLogger();
    Logger::setLevel(LogLevel::trace);
    Logger::setCallback(captureCallback, nullptr);

    SQ_WARN("w");
    SQ_INFO("i");
    SQ_DEBUG("d");
    SQ_TRACE("t");

    REQUIRE(g_captured.size() == 4);
    REQUIRE(g_captured[0].level == static_cast<int>(LogLevel::warn));
    REQUIRE(g_captured[1].level == static_cast<int>(LogLevel::info));
    REQUIRE(g_captured[2].level == static_cast<int>(LogLevel::debug));
    REQUIRE(g_captured[3].level == static_cast<int>(LogLevel::trace));

    resetLogger();
}

TEST_CASE("callback receives correct level for RT drain")
{
    resetLogger();
    Logger::setLevel(LogLevel::trace);
    Logger::setCallback(captureCallback, nullptr);

    SQ_WARN_RT("w");
    SQ_INFO_RT("i");
    SQ_DEBUG_RT("d");
    SQ_TRACE_RT("t");
    Logger::drain();

    REQUIRE(g_captured.size() == 4);
    REQUIRE(g_captured[0].level == static_cast<int>(LogLevel::warn));
    REQUIRE(g_captured[1].level == static_cast<int>(LogLevel::info));
    REQUIRE(g_captured[2].level == static_cast<int>(LogLevel::debug));
    REQUIRE(g_captured[3].level == static_cast<int>(LogLevel::trace));

    resetLogger();
}

// --- Edge cases ---

TEST_CASE("Drain on empty queue is safe")
{
    resetLogger();
    Logger::setCallback(captureCallback, nullptr);
    Logger::drain();
    Logger::drain();
    REQUIRE(g_captured.empty());

    resetLogger();
}

TEST_CASE("RT queue overflow is handled gracefully")
{
    resetLogger();
    Logger::setLevel(LogLevel::debug);

    // Push 2000 entries — queue capacity is 1024, so many will be dropped
    for (int i = 0; i < 2000; ++i)
        Logger::logRT(LogLevel::debug, __FILE__, __LINE__, "overflow test %d", i);

    // Should not crash or block
    Logger::setCallback(captureCallback, nullptr);
    Logger::drain();

    // At most kRingCapacity entries should have been stored
    REQUIRE(g_captured.size() <= 1024);
    REQUIRE(g_captured.size() > 0);

    resetLogger();
}

TEST_CASE("Long messages are truncated safely")
{
    resetLogger();
    Logger::setLevel(LogLevel::debug);
    Logger::setCallback(captureCallback, nullptr);

    char longMsg[1024];
    memset(longMsg, 'A', sizeof(longMsg) - 1);
    longMsg[sizeof(longMsg) - 1] = '\0';

    // Should not crash — message gets truncated by vsnprintf/snprintf
    SQ_DEBUG("%s", longMsg);
    REQUIRE(g_captured.size() == 1);
    // Full message is capped at 512 chars total (including format prefix)
    REQUIRE(g_captured[0].message.size() <= 512);

    g_captured.clear();

    SQ_DEBUG_RT("%s", longMsg);
    Logger::drain();
    REQUIRE(g_captured.size() == 1);
    REQUIRE(g_captured[0].message.size() <= 512);

    resetLogger();
}

// --- Multiple messages in sequence ---

TEST_CASE("Multiple CT logs are captured in order")
{
    resetLogger();
    Logger::setLevel(LogLevel::debug);
    Logger::setCallback(captureCallback, nullptr);

    SQ_DEBUG("first");
    SQ_DEBUG("second");
    SQ_DEBUG("third");

    REQUIRE(g_captured.size() == 3);
    REQUIRE(g_captured[0].message.find("first") != std::string::npos);
    REQUIRE(g_captured[1].message.find("second") != std::string::npos);
    REQUIRE(g_captured[2].message.find("third") != std::string::npos);

    resetLogger();
}

TEST_CASE("Multiple RT logs drain in order")
{
    resetLogger();
    Logger::setLevel(LogLevel::debug);
    Logger::setCallback(captureCallback, nullptr);

    SQ_DEBUG_RT("first");
    SQ_DEBUG_RT("second");
    SQ_DEBUG_RT("third");
    Logger::drain();

    REQUIRE(g_captured.size() == 3);
    REQUIRE(g_captured[0].message.find("first") != std::string::npos);
    REQUIRE(g_captured[1].message.find("second") != std::string::npos);
    REQUIRE(g_captured[2].message.find("third") != std::string::npos);

    resetLogger();
}
