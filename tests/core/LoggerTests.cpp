#include <catch2/catch_test_macros.hpp>

#include "core/Logger.h"

#include <thread>

using namespace squeeze;

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
