#include <catch2/catch_test_macros.hpp>
#include "ffi/squeeze_ffi.h"

#include <cstring>

// ═══════════════════════════════════════════════════════════════════
// Initial state
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_is_running returns false before sq_start")
{
    SqEngine engine = sq_engine_create(nullptr);
    REQUIRE(engine != nullptr);

    CHECK_FALSE(sq_is_running(engine));

    sq_engine_destroy(engine);
}

TEST_CASE("sq_sample_rate returns 0.0 when not running")
{
    SqEngine engine = sq_engine_create(nullptr);
    REQUIRE(engine != nullptr);

    CHECK(sq_sample_rate(engine) == 0.0);

    sq_engine_destroy(engine);
}

TEST_CASE("sq_block_size returns 0 when not running")
{
    SqEngine engine = sq_engine_create(nullptr);
    REQUIRE(engine != nullptr);

    CHECK(sq_block_size(engine) == 0);

    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Stop when not running
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_stop when not running is a no-op")
{
    SqEngine engine = sq_engine_create(nullptr);
    REQUIRE(engine != nullptr);

    sq_stop(engine); // must not crash
    CHECK_FALSE(sq_is_running(engine));

    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Start — handles both headless (no device) and real device
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_start attempts to open audio device")
{
    SqEngine engine = sq_engine_create(nullptr);
    REQUIRE(engine != nullptr);

    char* error = nullptr;
    bool ok = sq_start(engine, 44100.0, 512, &error);

    if (ok)
    {
        // Real audio device available
        CHECK(sq_is_running(engine));
        CHECK(sq_sample_rate(engine) > 0.0);
        CHECK(sq_block_size(engine) > 0);
        sq_stop(engine);
        CHECK_FALSE(sq_is_running(engine));
    }
    else
    {
        // No audio device (headless/CI)
        REQUIRE(error != nullptr);
        CHECK(std::strlen(error) > 0);
        sq_free_string(error);
        CHECK_FALSE(sq_is_running(engine));
        CHECK(sq_sample_rate(engine) == 0.0);
        CHECK(sq_block_size(engine) == 0);
        WARN("No audio device available — skipping real device assertions");
    }

    sq_engine_destroy(engine);
}

TEST_CASE("sq_start with NULL error pointer does not crash on failure")
{
    SqEngine engine = sq_engine_create(nullptr);
    REQUIRE(engine != nullptr);

    // Even if it succeeds, passing nullptr for error should be safe
    bool ok = sq_start(engine, 44100.0, 512, nullptr);
    if (ok)
        sq_stop(engine);

    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Stop resets state
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_stop resets sample_rate and block_size to 0")
{
    SqEngine engine = sq_engine_create(nullptr);
    REQUIRE(engine != nullptr);

    char* error = nullptr;
    bool ok = sq_start(engine, 44100.0, 512, &error);

    if (ok)
    {
        sq_stop(engine);
        CHECK(sq_sample_rate(engine) == 0.0);
        CHECK(sq_block_size(engine) == 0);
    }
    else
    {
        sq_free_string(error);
        WARN("No audio device — skipping stop-reset test");
    }

    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Double stop is safe
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_stop called twice is safe")
{
    SqEngine engine = sq_engine_create(nullptr);
    REQUIRE(engine != nullptr);

    char* error = nullptr;
    bool ok = sq_start(engine, 44100.0, 512, &error);

    if (ok)
    {
        sq_stop(engine);
        sq_stop(engine); // second stop must not crash
    }
    else
    {
        sq_free_string(error);
    }

    sq_engine_destroy(engine);
}
