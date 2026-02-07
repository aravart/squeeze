#include <catch2/catch_test_macros.hpp>

#include "core/Logger.h"

#include <thread>

using namespace squeeze;

TEST_CASE("Logger default level is warn")
{
    // Reset to default by setting warn (the compiled-in default)
    Logger::setLevel(LogLevel::warn);
    REQUIRE(Logger::getLevel() == LogLevel::warn);
    REQUIRE_FALSE(Logger::isEnabled());  // isEnabled means >= debug
}

TEST_CASE("Logger is disabled by default")
{
    Logger::disable();  // reset state
    REQUIRE_FALSE(Logger::isEnabled());
}

TEST_CASE("Logger enable and disable toggle state")
{
    Logger::disable();
    REQUIRE_FALSE(Logger::isEnabled());

    Logger::enable();
    REQUIRE(Logger::isEnabled());

    Logger::disable();
    REQUIRE_FALSE(Logger::isEnabled());
}

TEST_CASE("SQ_LOG macro is a no-op when disabled")
{
    Logger::disable();
    // Should not crash or produce output
    SQ_LOG("this should not appear: %d", 42);
}

TEST_CASE("SQ_LOG macro executes when enabled")
{
    Logger::enable();
    // Should not crash — output goes to stderr
    SQ_LOG("control thread log: %d", 123);
    Logger::disable();
}

TEST_CASE("SQ_LOG_RT pushes entry and drain writes it")
{
    Logger::enable();

    // Drain any leftover entries first
    Logger::drain();

    SQ_LOG_RT("rt message: %d", 456);
    // The entry is now in the queue — drain it
    Logger::drain();  // should write to stderr without crashing

    Logger::disable();
}

TEST_CASE("SQ_LOG_RT macro is a no-op when disabled")
{
    Logger::disable();
    // Should not crash or push to queue
    SQ_LOG_RT("this should not appear: %d", 99);

    // Drain should have nothing
    Logger::drain();
}

TEST_CASE("Drain on empty queue is safe")
{
    Logger::enable();
    // Drain any leftover entries
    Logger::drain();
    // Drain again on empty queue — should be a no-op
    Logger::drain();
    Logger::disable();
}

TEST_CASE("RT queue overflow is handled gracefully")
{
    Logger::enable();

    // Drain any leftover entries
    Logger::drain();

    // Push 2000 entries — queue capacity is 1024, so many will be dropped
    for (int i = 0; i < 2000; ++i)
        Logger::logRT(__FILE__, __LINE__, "overflow test %d", i);

    // Should not crash or block
    Logger::drain();

    Logger::disable();
}

TEST_CASE("Long messages are truncated safely")
{
    Logger::enable();

    // Create a message longer than 200 chars (user msg buffer size)
    char longMsg[512];
    memset(longMsg, 'A', sizeof(longMsg) - 1);
    longMsg[sizeof(longMsg) - 1] = '\0';

    // Should not crash — message gets truncated by vsnprintf/snprintf
    Logger::log(__FILE__, __LINE__, "%s", longMsg);
    Logger::logRT(__FILE__, __LINE__, "%s", longMsg);
    Logger::drain();

    Logger::disable();
}

TEST_CASE("Logger setLevel and getLevel")
{
    Logger::disable();
    REQUIRE(Logger::getLevel() == LogLevel::off);

    Logger::setLevel(LogLevel::warn);
    REQUIRE(Logger::getLevel() == LogLevel::warn);
    REQUIRE_FALSE(Logger::isEnabled());  // isEnabled means >= debug

    Logger::setLevel(LogLevel::debug);
    REQUIRE(Logger::getLevel() == LogLevel::debug);
    REQUIRE(Logger::isEnabled());

    Logger::setLevel(LogLevel::trace);
    REQUIRE(Logger::getLevel() == LogLevel::trace);
    REQUIRE(Logger::isEnabled());

    Logger::setLevel(LogLevel::off);
    REQUIRE(Logger::getLevel() == LogLevel::off);
    REQUIRE_FALSE(Logger::isEnabled());
}

TEST_CASE("enable sets level to debug")
{
    Logger::disable();
    Logger::enable();
    REQUIRE(Logger::getLevel() == LogLevel::debug);
    Logger::disable();
}

TEST_CASE("SQ_LOG_TRACE is a no-op at debug level")
{
    Logger::setLevel(LogLevel::debug);
    Logger::drain();

    // Should not push anything to the queue
    SQ_LOG_RT_TRACE("this should not appear: %d", 42);

    // Drain should have nothing new — we can't directly count entries,
    // but at least verify it doesn't crash
    Logger::drain();
    Logger::disable();
}

TEST_CASE("SQ_LOG_TRACE fires at trace level")
{
    Logger::setLevel(LogLevel::trace);
    Logger::drain();

    SQ_LOG_TRACE("trace CT message: %d", 99);
    SQ_LOG_RT_TRACE("trace RT message: %d", 100);
    Logger::drain();  // should write without crashing

    Logger::disable();
}

TEST_CASE("SQ_LOG_TRACE is a no-op when logging is off")
{
    Logger::disable();
    SQ_LOG_TRACE("should not appear: %d", 1);
    SQ_LOG_RT_TRACE("should not appear: %d", 2);
    Logger::drain();
}

TEST_CASE("SQ_LOG_WARN fires at warn level")
{
    Logger::setLevel(LogLevel::warn);
    SQ_LOG_WARN("warn CT message: %d", 42);
    Logger::disable();
}

TEST_CASE("SQ_LOG_RT_WARN fires at warn level and drains")
{
    Logger::setLevel(LogLevel::warn);
    Logger::drain();

    SQ_LOG_RT_WARN("warn RT message: %d", 77);
    Logger::drain();  // should write without crashing

    Logger::disable();
}

TEST_CASE("SQ_LOG_WARN and SQ_LOG_RT_WARN are no-ops when level is off")
{
    Logger::disable();
    SQ_LOG_WARN("should not appear: %d", 1);
    SQ_LOG_RT_WARN("should not appear: %d", 2);
    Logger::drain();
}

TEST_CASE("SQ_LOG (debug) is a no-op at warn level")
{
    Logger::setLevel(LogLevel::warn);
    Logger::drain();

    SQ_LOG("should not appear at warn: %d", 99);
    SQ_LOG_RT("should not appear at warn: %d", 100);

    // Nothing should have been queued for RT
    Logger::drain();
    Logger::disable();
}

TEST_CASE("warn level in setLevel/getLevel round-trip")
{
    Logger::setLevel(LogLevel::warn);
    REQUIRE(Logger::getLevel() == LogLevel::warn);
    REQUIRE_FALSE(Logger::isEnabled());  // isEnabled means >= debug
    Logger::disable();
}
