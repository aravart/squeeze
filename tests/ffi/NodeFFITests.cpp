#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ffi/squeeze_ffi.h"
#include <cstring>

// Helper: create an engine for each test
struct FFIFixture {
    SqEngine engine;
    FFIFixture()  { engine = sq_engine_create(44100.0, 512, nullptr); }
    ~FFIFixture() { sq_engine_destroy(engine); }
};

// ═══════════════════════════════════════════════════════════════════
// sq_add_gain
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_add_gain returns a positive node id")
{
    FFIFixture f;
    int id = sq_add_gain(f.engine);
    REQUIRE(id > 0);
}

TEST_CASE("sq_add_gain returns unique ids")
{
    FFIFixture f;
    int a = sq_add_gain(f.engine);
    int b = sq_add_gain(f.engine);
    REQUIRE(a != b);
}

// ═══════════════════════════════════════════════════════════════════
// sq_node_name
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_node_name returns gain for a gain node")
{
    FFIFixture f;
    int id = sq_add_gain(f.engine);
    char* name = sq_node_name(f.engine, id);
    REQUIRE(name != nullptr);
    CHECK(std::string(name) == "gain");
    sq_free_string(name);
}

TEST_CASE("sq_node_name returns NULL for invalid id")
{
    FFIFixture f;
    char* name = sq_node_name(f.engine, 9999);
    CHECK(name == nullptr);
}

// ═══════════════════════════════════════════════════════════════════
// sq_get_ports
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_get_ports returns correct ports for gain node")
{
    FFIFixture f;
    int id = sq_add_gain(f.engine);
    SqPortList ports = sq_get_ports(f.engine, id);
    REQUIRE(ports.count == 2);

    // input port
    CHECK(std::string(ports.ports[0].name) == "in");
    CHECK(ports.ports[0].direction == 0);
    CHECK(ports.ports[0].signal_type == 0);
    CHECK(ports.ports[0].channels == 2);

    // output port
    CHECK(std::string(ports.ports[1].name) == "out");
    CHECK(ports.ports[1].direction == 1);
    CHECK(ports.ports[1].signal_type == 0);
    CHECK(ports.ports[1].channels == 2);

    sq_free_port_list(ports);
}

TEST_CASE("sq_get_ports returns empty for invalid id")
{
    FFIFixture f;
    SqPortList ports = sq_get_ports(f.engine, 9999);
    CHECK(ports.count == 0);
    CHECK(ports.ports == nullptr);
}

// ═══════════════════════════════════════════════════════════════════
// sq_param_descriptors
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_param_descriptors returns correct descriptors for gain node")
{
    FFIFixture f;
    int id = sq_add_gain(f.engine);
    SqParamDescriptorList descs = sq_param_descriptors(f.engine, id);
    REQUIRE(descs.count == 1);

    CHECK(std::string(descs.descriptors[0].name) == "gain");
    CHECK(descs.descriptors[0].default_value == Catch::Approx(1.0f));
    CHECK(descs.descriptors[0].num_steps == 0);
    CHECK(descs.descriptors[0].automatable == true);
    CHECK(descs.descriptors[0].boolean_param == false);

    sq_free_param_descriptor_list(descs);
}

TEST_CASE("sq_param_descriptors returns empty for invalid id")
{
    FFIFixture f;
    SqParamDescriptorList descs = sq_param_descriptors(f.engine, 9999);
    CHECK(descs.count == 0);
    CHECK(descs.descriptors == nullptr);
}

// ═══════════════════════════════════════════════════════════════════
// sq_get_param / sq_set_param
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_get_param returns default value for gain")
{
    FFIFixture f;
    int id = sq_add_gain(f.engine);
    CHECK(sq_get_param(f.engine, id, "gain") == Catch::Approx(1.0f));
}

TEST_CASE("sq_set_param and sq_get_param roundtrip")
{
    FFIFixture f;
    int id = sq_add_gain(f.engine);
    REQUIRE(sq_set_param(f.engine, id, "gain", 0.25f));
    CHECK(sq_get_param(f.engine, id, "gain") == Catch::Approx(0.25f));
}

TEST_CASE("sq_get_param with unknown name returns 0.0f")
{
    FFIFixture f;
    int id = sq_add_gain(f.engine);
    CHECK(sq_get_param(f.engine, id, "unknown") == Catch::Approx(0.0f));
}

TEST_CASE("sq_get_param with invalid node id returns 0.0f")
{
    FFIFixture f;
    CHECK(sq_get_param(f.engine, 9999, "gain") == Catch::Approx(0.0f));
}

TEST_CASE("sq_set_param with invalid node id returns false")
{
    FFIFixture f;
    CHECK_FALSE(sq_set_param(f.engine, 9999, "gain", 0.5f));
}

// ═══════════════════════════════════════════════════════════════════
// sq_param_text
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_param_text returns text for valid name")
{
    FFIFixture f;
    int id = sq_add_gain(f.engine);
    char* text = sq_param_text(f.engine, id, "gain");
    REQUIRE(text != nullptr);
    CHECK(std::strlen(text) > 0);
    sq_free_string(text);
}

TEST_CASE("sq_param_text returns NULL for unknown name")
{
    FFIFixture f;
    int id = sq_add_gain(f.engine);
    char* text = sq_param_text(f.engine, id, "unknown");
    CHECK(text == nullptr);
}

// ═══════════════════════════════════════════════════════════════════
// sq_remove_node
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_remove_node succeeds for existing node")
{
    FFIFixture f;
    int id = sq_add_gain(f.engine);
    REQUIRE(sq_remove_node(f.engine, id));
    // node is gone
    CHECK(sq_node_name(f.engine, id) == nullptr);
}

TEST_CASE("sq_remove_node returns false for invalid id")
{
    FFIFixture f;
    CHECK_FALSE(sq_remove_node(f.engine, 9999));
}

// ═══════════════════════════════════════════════════════════════════
// sq_free_port_list / sq_free_param_descriptor_list — empty safety
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_free_port_list is safe with empty list")
{
    SqPortList empty = {nullptr, 0};
    sq_free_port_list(empty); // must not crash
}

TEST_CASE("sq_free_param_descriptor_list is safe with empty list")
{
    SqParamDescriptorList empty = {nullptr, 0};
    sq_free_param_descriptor_list(empty); // must not crash
}
