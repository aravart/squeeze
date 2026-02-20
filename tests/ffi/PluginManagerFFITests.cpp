#include <catch2/catch_test_macros.hpp>
#include "ffi/squeeze_ffi.h"

#include <cstring>
#include <string>

// ═══════════════════════════════════════════════════════════════════
// Plugin cache — initial state
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_num_plugins returns 0 initially")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(engine != nullptr);

    CHECK(sq_num_plugins(engine) == 0);

    sq_engine_destroy(engine);
}

TEST_CASE("sq_available_plugins returns empty list initially")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(engine != nullptr);

    SqStringList list = sq_available_plugins(engine);
    CHECK(list.count == 0);
    sq_free_string_list(list);

    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Plugin cache loading
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_load_plugin_cache with nonexistent file returns false and sets error")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(engine != nullptr);

    char* error = nullptr;
    bool ok = sq_load_plugin_cache(engine, "/no/such/file.xml", &error);
    CHECK_FALSE(ok);
    REQUIRE(error != nullptr);
    CHECK(std::strlen(error) > 0);
    sq_free_string(error);

    CHECK(sq_num_plugins(engine) == 0);

    sq_engine_destroy(engine);
}

TEST_CASE("sq_load_plugin_cache with real cache file succeeds")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(engine != nullptr);

    // Use the real plugin-cache.xml at the project root
    // The file is expected to exist during testing
    char* error = nullptr;
    bool ok = sq_load_plugin_cache(engine, "plugin-cache.xml", &error);

    // If the file doesn't exist in the working directory, try the project root
    if (!ok)
    {
        sq_free_string(error);
        error = nullptr;
        ok = sq_load_plugin_cache(engine, "../plugin-cache.xml", &error);
    }

    if (ok)
    {
        CHECK(sq_num_plugins(engine) > 0);

        SqStringList list = sq_available_plugins(engine);
        CHECK(list.count > 0);

        // Verify sorted order
        for (int i = 1; i < list.count; i++)
            CHECK(std::string(list.items[i - 1]) <= std::string(list.items[i]));

        sq_free_string_list(list);
    }
    else
    {
        // If plugin-cache.xml isn't available, just verify the error was set
        REQUIRE(error != nullptr);
        sq_free_string(error);
        WARN("plugin-cache.xml not found — skipping real cache test");
    }

    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Plugin info
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_plugin_infos returns empty list initially")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(engine != nullptr);

    SqPluginInfoList list = sq_plugin_infos(engine);
    CHECK(list.count == 0);
    CHECK(list.items == nullptr);
    sq_free_plugin_info_list(list);

    sq_engine_destroy(engine);
}

TEST_CASE("sq_free_plugin_info_list with empty list is safe")
{
    SqPluginInfoList list = {nullptr, 0};
    sq_free_plugin_info_list(list); // must not crash
}

// ═══════════════════════════════════════════════════════════════════
// Plugin instantiation
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_add_plugin with unknown name returns -1 and sets error")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(engine != nullptr);


    char* error = nullptr;
    int id = sq_add_plugin(engine, "NonexistentPlugin", &error);
    CHECK(id == -1);
    REQUIRE(error != nullptr);
    CHECK(std::strlen(error) > 0);
    sq_free_string(error);

    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Free helpers
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_free_string_list with empty list is safe")
{
    SqStringList list = {nullptr, 0};
    sq_free_string_list(list); // must not crash
}

TEST_CASE("sq_free_string_list with NULL error pointer in sq_load_plugin_cache is safe")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(engine != nullptr);

    bool ok = sq_load_plugin_cache(engine, "/no/such/file.xml", nullptr);
    CHECK_FALSE(ok);

    sq_engine_destroy(engine);
}
