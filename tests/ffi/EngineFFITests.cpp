#include <catch2/catch_test_macros.hpp>
#include "ffi/squeeze_ffi.h"
#include <cstring>

// ═══════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_engine_create returns a non-NULL handle")
{
    char* error = nullptr;
    SqEngine engine = sq_engine_create(44100.0, 512, &error);
    REQUIRE(engine != nullptr);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_engine_create with NULL error pointer does not crash")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
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
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(engine != nullptr);

    char* version = sq_version(engine);
    REQUIRE(version != nullptr);
    REQUIRE(std::strlen(version) > 0);

    sq_free_string(version);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_version returns expected version")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    char* version = sq_version(engine);

    REQUIRE(std::string(version) == "0.2.0");

    sq_free_string(version);
    sq_engine_destroy(engine);
}

TEST_CASE("Multiple engines can be created and destroyed independently")
{
    SqEngine a = sq_engine_create(44100.0, 512, nullptr);
    SqEngine b = sq_engine_create(44100.0, 512, nullptr);
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

// ═══════════════════════════════════════════════════════════════════
// Output node
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_output_node returns valid ID")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int outId = sq_output_node(engine);
    REQUIRE(outId > 0);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_remove_node on output node returns false")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int outId = sq_output_node(engine);
    REQUIRE_FALSE(sq_remove_node(engine, outId));
    sq_engine_destroy(engine);
}

TEST_CASE("sq_node_count includes output node")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(sq_node_count(engine) == 1);

    int g = sq_add_gain(engine);
    REQUIRE(sq_node_count(engine) == 2);

    sq_remove_node(engine, g);
    REQUIRE(sq_node_count(engine) == 1);

    sq_engine_destroy(engine);
}

TEST_CASE("Output node has 'in' port")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int outId = sq_output_node(engine);

    SqPortList ports = sq_get_ports(engine, outId);
    REQUIRE(ports.count >= 1);

    bool foundIn = false;
    for (int i = 0; i < ports.count; i++)
    {
        if (std::string(ports.ports[i].name) == "in" && ports.ports[i].direction == 0)
            foundIn = true;
    }
    CHECK(foundIn);

    sq_free_port_list(ports);
    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Testing and processBlock
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_render does not crash")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    sq_render(engine, 512);
    sq_engine_destroy(engine);
}

TEST_CASE("Connect gain to output, render succeeds")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);


    int g = sq_add_gain(engine);
    int out = sq_output_node(engine);

    char* error = nullptr;
    int connId = sq_connect(engine, g, "out", out, "in", &error);
    REQUIRE(connId >= 0);

    sq_render(engine, 512);

    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Transport stubs
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Transport stubs do not crash and return defaults")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);


    sq_transport_play(engine);
    sq_transport_stop(engine);
    sq_transport_pause(engine);
    sq_transport_set_tempo(engine, 140.0);
    sq_transport_set_time_signature(engine, 3, 4);
    sq_transport_seek_samples(engine, 0);
    sq_transport_seek_beats(engine, 0.0);
    sq_transport_set_loop_points(engine, 0.0, 4.0);
    sq_transport_set_looping(engine, true);

    CHECK(sq_transport_position(engine) == 0.0);
    CHECK(sq_transport_tempo(engine) == 120.0);
    CHECK_FALSE(sq_transport_is_playing(engine));

    sq_render(engine, 512); // drain commands
    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Event scheduling stubs
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Event scheduling stubs return false")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    CHECK_FALSE(sq_schedule_note_on(engine, 1, 0.0, 1, 60, 0.8f));
    CHECK_FALSE(sq_schedule_note_off(engine, 1, 1.0, 1, 60));
    CHECK_FALSE(sq_schedule_cc(engine, 1, 0.0, 1, 1, 64));
    CHECK_FALSE(sq_schedule_param_change(engine, 1, 0.0, "gain", 0.5f));
    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Parameters through FFI
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_get_param and sq_set_param work through engine")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int g = sq_add_gain(engine);

    CHECK(sq_get_param(engine, g, "gain") == 1.0f);
    REQUIRE(sq_set_param(engine, g, "gain", 0.75f));
    CHECK(sq_get_param(engine, g, "gain") == 0.75f);

    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// PluginNode / Test Synth
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_add_test_synth returns valid ID")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int synth = sq_add_test_synth(engine);
    REQUIRE(synth > 0);
    CHECK(sq_node_count(engine) == 2); // output + synth
    sq_engine_destroy(engine);
}

TEST_CASE("Test synth has correct ports")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int synth = sq_add_test_synth(engine);

    SqPortList ports = sq_get_ports(engine, synth);
    REQUIRE(ports.count >= 3); // midi_in, out, midi_out

    bool foundAudioOut = false;
    bool foundMidiIn = false;
    bool foundMidiOut = false;
    for (int i = 0; i < ports.count; i++)
    {
        std::string name(ports.ports[i].name);
        int dir = ports.ports[i].direction;
        int sig = ports.ports[i].signal_type;

        if (name == "out" && dir == 1 && sig == 0) foundAudioOut = true;
        if (name == "midi_in" && dir == 0 && sig == 1) foundMidiIn = true;
        if (name == "midi_out" && dir == 1 && sig == 1) foundMidiOut = true;
    }
    CHECK(foundAudioOut);
    CHECK(foundMidiIn);
    CHECK(foundMidiOut);

    sq_free_port_list(ports);
    sq_engine_destroy(engine);
}

TEST_CASE("Test synth parameters accessible via sq_get_param/sq_set_param")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int synth = sq_add_test_synth(engine);

    CHECK(sq_get_param(engine, synth, "Gain") == 1.0f);
    REQUIRE(sq_set_param(engine, synth, "Gain", 0.25f));
    CHECK(sq_get_param(engine, synth, "Gain") != 1.0f);

    sq_engine_destroy(engine);
}

TEST_CASE("Connect test synth to output, render succeeds")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);


    int synth = sq_add_test_synth(engine);
    int out = sq_output_node(engine);

    char* error = nullptr;
    int connId = sq_connect(engine, synth, "out", out, "in", &error);
    REQUIRE(connId >= 0);

    sq_render(engine, 512);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_param_descriptors returns expected params for test synth")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int synth = sq_add_test_synth(engine);

    SqParamDescriptorList descs = sq_param_descriptors(engine, synth);
    REQUIRE(descs.count == 2);

    bool foundGain = false, foundMix = false;
    for (int i = 0; i < descs.count; i++)
    {
        std::string name(descs.descriptors[i].name);
        if (name == "Gain") foundGain = true;
        if (name == "Mix") foundMix = true;
    }
    CHECK(foundGain);
    CHECK(foundMix);

    sq_free_param_descriptor_list(descs);
    sq_engine_destroy(engine);
}
