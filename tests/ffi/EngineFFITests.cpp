#include <catch2/catch_test_macros.hpp>
#include "ffi/squeeze_ffi.h"
#include <cstring>

TEST_CASE("sq_engine_create returns a non-NULL handle")
{
    char* error = nullptr;
    SqEngine engine = sq_engine_create(&error);
    REQUIRE(engine != nullptr);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_engine_create with NULL error pointer does not crash")
{
    SqEngine engine = sq_engine_create(nullptr);
    REQUIRE(engine != nullptr);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_engine_destroy with NULL is a no-op")
{
    sq_engine_destroy(nullptr); // must not crash
}

TEST_CASE("sq_free_string with NULL is a no-op")
{
    sq_free_string(nullptr); // must not crash
}

TEST_CASE("sq_version returns a non-NULL version string")
{
    SqEngine engine = sq_engine_create(nullptr);
    REQUIRE(engine != nullptr);

    char* version = sq_version(engine);
    REQUIRE(version != nullptr);
    REQUIRE(std::strlen(version) > 0);

    sq_free_string(version);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_version returns expected version")
{
    SqEngine engine = sq_engine_create(nullptr);
    char* version = sq_version(engine);

    REQUIRE(std::string(version) == "0.2.0");

    sq_free_string(version);
    sq_engine_destroy(engine);
}

TEST_CASE("Multiple engines can be created and destroyed independently")
{
    SqEngine a = sq_engine_create(nullptr);
    SqEngine b = sq_engine_create(nullptr);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(a != b);

    char* va = sq_version(a);
    char* vb = sq_version(b);
    REQUIRE(std::string(va) == std::string(vb));

    sq_free_string(va);
    sq_free_string(vb);
    sq_engine_destroy(a);
    sq_engine_destroy(b);
}
