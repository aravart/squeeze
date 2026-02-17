#include <catch2/catch_test_macros.hpp>
#include "ffi/squeeze_ffi.h"

#include <cstring>
#include <string>

// ═══════════════════════════════════════════════════════════════════
// Initial state
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_midi_devices returns a list (may be empty in CI)")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(engine != nullptr);

    SqStringList list = sq_midi_devices(engine);
    // May be empty in headless CI — just verify it doesn't crash
    CHECK(list.count >= 0);
    sq_free_string_list(list);

    sq_engine_destroy(engine);
}

TEST_CASE("sq_midi_open_devices returns empty list initially")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(engine != nullptr);

    SqStringList list = sq_midi_open_devices(engine);
    CHECK(list.count == 0);
    sq_free_string_list(list);

    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Device open/close — error paths
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_midi_open with unknown device returns false and sets error")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(engine != nullptr);

    char* error = nullptr;
    bool ok = sq_midi_open(engine, "NonexistentMidiDevice12345", &error);
    CHECK_FALSE(ok);
    REQUIRE(error != nullptr);
    CHECK(std::strlen(error) > 0);
    sq_free_string(error);

    sq_engine_destroy(engine);
}

TEST_CASE("sq_midi_open with NULL error pointer does not crash on failure")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(engine != nullptr);

    bool ok = sq_midi_open(engine, "NonexistentMidiDevice12345", nullptr);
    CHECK_FALSE(ok);

    sq_engine_destroy(engine);
}

TEST_CASE("sq_midi_close with unknown name is a no-op")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(engine != nullptr);

    sq_midi_close(engine, "NonexistentMidiDevice12345"); // must not crash

    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Routing — error paths (no devices open)
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_midi_route with unregistered device returns -1 and sets error")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(engine != nullptr);

    char* error = nullptr;
    int id = sq_midi_route(engine, "no_such_device", 1, 0, -1, &error);
    CHECK(id == -1);
    REQUIRE(error != nullptr);
    sq_free_string(error);

    sq_engine_destroy(engine);
}

TEST_CASE("sq_midi_unroute with invalid id returns false")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(engine != nullptr);

    CHECK_FALSE(sq_midi_unroute(engine, 999));

    sq_engine_destroy(engine);
}

TEST_CASE("sq_midi_routes returns empty list initially")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(engine != nullptr);

    SqMidiRouteList routes = sq_midi_routes(engine);
    CHECK(routes.count == 0);
    sq_free_midi_route_list(routes);

    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Free helpers
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_free_midi_route_list with empty list is safe")
{
    SqMidiRouteList list = {nullptr, 0};
    sq_free_midi_route_list(list); // must not crash
}

TEST_CASE("sq_free_string_list with empty midi device list is safe")
{
    SqStringList list = {nullptr, 0};
    sq_free_string_list(list); // must not crash
}

// ═══════════════════════════════════════════════════════════════════
// Real device tests (conditional — skip if no MIDI hardware)
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_midi_open with real device succeeds if available")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(engine != nullptr);

    SqStringList devices = sq_midi_devices(engine);
    if (devices.count == 0)
    {
        sq_free_string_list(devices);
        sq_engine_destroy(engine);
        WARN("No MIDI devices available — skipping real device test");
        return;
    }

    // Try to open the first available device
    std::string firstName(devices.items[0]);
    sq_free_string_list(devices);

    char* error = nullptr;
    bool ok = sq_midi_open(engine, firstName.c_str(), &error);
    if (!ok)
    {
        // Some devices may be busy or fail to open
        if (error) sq_free_string(error);
        sq_engine_destroy(engine);
        WARN("MIDI device open failed — skipping");
        return;
    }

    // Verify the device shows in open devices
    SqStringList openList = sq_midi_open_devices(engine);
    CHECK(openList.count == 1);
    if (openList.count > 0)
        CHECK(std::string(openList.items[0]) == firstName);
    sq_free_string_list(openList);

    // Opening same device again is a no-op (returns true)
    char* error2 = nullptr;
    CHECK(sq_midi_open(engine, firstName.c_str(), &error2));

    // Close
    sq_midi_close(engine, firstName.c_str());

    openList = sq_midi_open_devices(engine);
    CHECK(openList.count == 0);
    sq_free_string_list(openList);

    sq_engine_destroy(engine);
}
