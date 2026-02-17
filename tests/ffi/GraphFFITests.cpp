#include <catch2/catch_test_macros.hpp>
#include "ffi/squeeze_ffi.h"
#include <cstring>

// Helper: create an engine for each test
struct GraphFFIFixture {
    SqEngine engine;
    GraphFFIFixture()  { engine = sq_engine_create(44100.0, 512, nullptr); }
    ~GraphFFIFixture() { sq_engine_destroy(engine); }
};

// ═══════════════════════════════════════════════════════════════════
// sq_connect — success
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_connect returns connection id on success")
{
    GraphFFIFixture f;
    int a = sq_add_gain(f.engine);
    int b = sq_add_gain(f.engine);

    char* error = nullptr;
    int conn = sq_connect(f.engine, a, "out", b, "in", &error);
    CHECK(conn >= 0);
    CHECK(error == nullptr);
}

TEST_CASE("sq_connect error is NULL on success")
{
    GraphFFIFixture f;
    int a = sq_add_gain(f.engine);
    int b = sq_add_gain(f.engine);

    char* error = nullptr;
    sq_connect(f.engine, a, "out", b, "in", &error);
    CHECK(error == nullptr);
}

// ═══════════════════════════════════════════════════════════════════
// sq_connect — failures
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_connect returns -1 for invalid source node")
{
    GraphFFIFixture f;
    int b = sq_add_gain(f.engine);

    char* error = nullptr;
    int conn = sq_connect(f.engine, 999, "out", b, "in", &error);
    CHECK(conn == -1);
    REQUIRE(error != nullptr);
    CHECK(std::string(error).find("source node") != std::string::npos);
    sq_free_string(error);
}

TEST_CASE("sq_connect returns -1 for cycle")
{
    GraphFFIFixture f;
    int a = sq_add_gain(f.engine);
    int b = sq_add_gain(f.engine);

    char* error = nullptr;
    int c1 = sq_connect(f.engine, a, "out", b, "in", &error);
    REQUIRE(c1 >= 0);

    int c2 = sq_connect(f.engine, b, "out", a, "in", &error);
    CHECK(c2 == -1);
    REQUIRE(error != nullptr);
    CHECK(std::string(error).find("cycle") != std::string::npos);
    sq_free_string(error);
}

TEST_CASE("sq_connect with NULL error pointer does not crash")
{
    GraphFFIFixture f;
    int b = sq_add_gain(f.engine);
    // invalid source, but error is NULL — should not crash
    int conn = sq_connect(f.engine, 999, "out", b, "in", nullptr);
    CHECK(conn == -1);
}

// ═══════════════════════════════════════════════════════════════════
// sq_disconnect
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_disconnect succeeds for valid connection")
{
    GraphFFIFixture f;
    int a = sq_add_gain(f.engine);
    int b = sq_add_gain(f.engine);

    char* error = nullptr;
    int conn = sq_connect(f.engine, a, "out", b, "in", &error);
    REQUIRE(conn >= 0);

    CHECK(sq_disconnect(f.engine, conn));
}

TEST_CASE("sq_disconnect returns false for unknown id")
{
    GraphFFIFixture f;
    CHECK_FALSE(sq_disconnect(f.engine, 999));
}

// ═══════════════════════════════════════════════════════════════════
// sq_connections
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_connections returns correct count and fields")
{
    GraphFFIFixture f;
    int a = sq_add_gain(f.engine);
    int b = sq_add_gain(f.engine);

    char* error = nullptr;
    int conn = sq_connect(f.engine, a, "out", b, "in", &error);
    REQUIRE(conn >= 0);

    SqConnectionList list = sq_connections(f.engine);
    REQUIRE(list.count == 1);
    CHECK(list.connections[0].id == conn);
    CHECK(list.connections[0].src_node == a);
    CHECK(std::string(list.connections[0].src_port) == "out");
    CHECK(list.connections[0].dst_node == b);
    CHECK(std::string(list.connections[0].dst_port) == "in");

    sq_free_connection_list(list);
}

TEST_CASE("sq_connections returns empty when no connections")
{
    GraphFFIFixture f;
    SqConnectionList list = sq_connections(f.engine);
    CHECK(list.count == 0);
    CHECK(list.connections == nullptr);
}

TEST_CASE("sq_free_connection_list is safe with empty list")
{
    SqConnectionList empty = {nullptr, 0};
    sq_free_connection_list(empty); // must not crash
}

// ═══════════════════════════════════════════════════════════════════
// Integration: connect, query, disconnect roundtrip
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_connect query disconnect roundtrip")
{
    GraphFFIFixture f;
    int a = sq_add_gain(f.engine);
    int b = sq_add_gain(f.engine);
    int c = sq_add_gain(f.engine);

    char* error = nullptr;
    int c1 = sq_connect(f.engine, a, "out", b, "in", &error);
    int c2 = sq_connect(f.engine, b, "out", c, "in", &error);
    REQUIRE(c1 >= 0);
    REQUIRE(c2 >= 0);

    SqConnectionList list = sq_connections(f.engine);
    CHECK(list.count == 2);
    sq_free_connection_list(list);

    // Disconnect first
    REQUIRE(sq_disconnect(f.engine, c1));
    list = sq_connections(f.engine);
    CHECK(list.count == 1);
    CHECK(list.connections[0].id == c2);
    sq_free_connection_list(list);

    // Remove node cascades
    sq_remove_node(f.engine, b);
    list = sq_connections(f.engine);
    CHECK(list.count == 0);
    sq_free_connection_list(list);
}
