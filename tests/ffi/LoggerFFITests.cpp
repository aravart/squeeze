#include <catch2/catch_test_macros.hpp>

#include "ffi/squeeze_ffi.h"
#include "core/Logger.h"

#include <string>
#include <vector>

// --- Callback test helpers ---

struct CapturedFFILog {
    int level;
    std::string message;
};

static std::vector<CapturedFFILog> g_ffiCaptured;

static void ffiCaptureCallback(int level, const char* message, void* /*userData*/)
{
    g_ffiCaptured.push_back({level, message});
}

static void resetFFI()
{
    sq_set_log_callback(nullptr, nullptr);
    sq_set_log_level(1); // warn
    g_ffiCaptured.clear();
}

// --- Tests ---

TEST_CASE("sq_set_log_level sets and queries level via callback")
{
    resetFFI();

    // Set to debug (3), install callback, trigger a log via the internal API
    sq_set_log_level(3);
    sq_set_log_callback(ffiCaptureCallback, nullptr);

    // We need to trigger a log message. Use the Logger directly since
    // we're testing the FFI level control, not the logging path.
    squeeze::Logger::log(squeeze::LogLevel::debug, __FILE__, __LINE__, "ffi level test");
    REQUIRE(g_ffiCaptured.size() == 1);

    // Set to warn (1) — debug should be suppressed
    g_ffiCaptured.clear();
    sq_set_log_level(1);
    // Level check: debug macro won't fire at warn
    if (squeeze::Logger::getLevel() >= squeeze::LogLevel::debug)
        squeeze::Logger::log(squeeze::LogLevel::debug, __FILE__, __LINE__, "should not appear");
    REQUIRE(g_ffiCaptured.empty());

    resetFFI();
}

TEST_CASE("sq_set_log_callback captures messages")
{
    resetFFI();
    sq_set_log_level(3); // debug
    sq_set_log_callback(ffiCaptureCallback, nullptr);

    squeeze::Logger::log(squeeze::LogLevel::debug, __FILE__, __LINE__, "ffi callback test %d", 42);
    REQUIRE(g_ffiCaptured.size() == 1);
    REQUIRE(g_ffiCaptured[0].message.find("ffi callback test 42") != std::string::npos);
    REQUIRE(g_ffiCaptured[0].level == 3); // debug

    resetFFI();
}

TEST_CASE("sq_set_log_callback NULL reverts to stderr")
{
    resetFFI();
    sq_set_log_level(3);

    // Set callback, then clear it
    sq_set_log_callback(ffiCaptureCallback, nullptr);
    sq_set_log_callback(nullptr, nullptr);

    // Log should go to stderr, not crash, not be captured
    squeeze::Logger::log(squeeze::LogLevel::debug, __FILE__, __LINE__, "after null callback");
    REQUIRE(g_ffiCaptured.empty());

    resetFFI();
}
