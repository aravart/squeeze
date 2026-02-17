#include <catch2/catch_test_macros.hpp>
#include "ffi/squeeze_ffi.h"
#include <cstring>

// ═══════════════════════════════════════════════════════════════════
// sq_open_editor error paths
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_open_editor on non-existent node returns false with error")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    char* error = nullptr;

    REQUIRE_FALSE(sq_open_editor(engine, 9999, &error));
    REQUIRE(error != nullptr);
    CHECK(std::string(error).find("not found") != std::string::npos);

    sq_free_string(error);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_open_editor on GainNode returns false with not-a-plugin error")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int gain = sq_add_gain(engine);
    char* error = nullptr;

    REQUIRE_FALSE(sq_open_editor(engine, gain, &error));
    REQUIRE(error != nullptr);
    CHECK(std::string(error).find("not a plugin") != std::string::npos);

    sq_free_string(error);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_open_editor on test synth returns false with no-editor error")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int synth = sq_add_test_synth(engine);
    char* error = nullptr;

    REQUIRE_FALSE(sq_open_editor(engine, synth, &error));
    REQUIRE(error != nullptr);
    CHECK(std::string(error).find("no editor") != std::string::npos);

    sq_free_string(error);
    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// sq_has_editor
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_has_editor returns false by default")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int gain = sq_add_gain(engine);

    CHECK_FALSE(sq_has_editor(engine, gain));
    CHECK_FALSE(sq_has_editor(engine, 9999));

    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// sq_close_editor error paths
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_close_editor when no editor open returns false with error")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int gain = sq_add_gain(engine);
    char* error = nullptr;

    REQUIRE_FALSE(sq_close_editor(engine, gain, &error));
    REQUIRE(error != nullptr);
    CHECK(std::string(error).find("No editor open") != std::string::npos);

    sq_free_string(error);
    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// sq_process_events
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_process_events with zero timeout does not crash")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    sq_process_events(0);
    sq_engine_destroy(engine);
}
