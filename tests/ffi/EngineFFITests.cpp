#include <catch2/catch_test_macros.hpp>
#include "ffi/squeeze_ffi.h"
#include <cstring>
#include <cmath>

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
    sq_engine_destroy(nullptr);
}

TEST_CASE("sq_free_string with NULL is a no-op")
{
    sq_free_string(nullptr);
}

TEST_CASE("sq_version returns 0.3.0")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    char* version = sq_version(engine);
    REQUIRE(version != nullptr);
    CHECK(std::string(version) == "0.3.0");
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
    sq_engine_destroy(a);
    sq_engine_destroy(b);
}

// ═══════════════════════════════════════════════════════════════════
// Master bus
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_master returns valid handle")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int master = sq_master(engine);
    REQUIRE(master > 0);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_remove_bus on master returns false")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int master = sq_master(engine);
    REQUIRE_FALSE(sq_remove_bus(engine, master));
    sq_engine_destroy(engine);
}

TEST_CASE("sq_bus_count starts at 1 (Master)")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    REQUIRE(sq_bus_count(engine) == 1);
    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Source management
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_add_source returns positive handle")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int src = sq_add_source(engine, "synth");
    REQUIRE(src > 0);
    CHECK(sq_source_count(engine) == 1);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_remove_source removes the source")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int src = sq_add_source(engine, "synth");
    REQUIRE(sq_remove_source(engine, src));
    CHECK(sq_source_count(engine) == 0);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_remove_source returns false for unknown handle")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    CHECK_FALSE(sq_remove_source(engine, 9999));
    sq_engine_destroy(engine);
}

TEST_CASE("sq_source_generator returns generator proc handle")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int src = sq_add_source(engine, "synth");
    int gen = sq_source_generator(engine, src);
    REQUIRE(gen > 0);
    // Generator should have a "gain" parameter (GainProcessor)
    CHECK(sq_get_param(engine, gen, "gain") == 1.0f);
    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Bus management
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_add_bus returns positive handle")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int bus = sq_add_bus(engine, "FX");
    REQUIRE(bus > 0);
    CHECK(sq_bus_count(engine) == 2); // Master + FX
    sq_engine_destroy(engine);
}

TEST_CASE("sq_remove_bus removes non-master bus")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int bus = sq_add_bus(engine, "FX");
    REQUIRE(sq_remove_bus(engine, bus));
    CHECK(sq_bus_count(engine) == 1);
    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Routing
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_route routes source to bus")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int src = sq_add_source(engine, "synth");
    int bus = sq_add_bus(engine, "FX");
    sq_route(engine, src, bus);
    // No crash, verify by rendering
    sq_render(engine, 512);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_bus_route routes bus to bus")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int bus1 = sq_add_bus(engine, "A");
    int bus2 = sq_add_bus(engine, "B");
    CHECK(sq_bus_route(engine, bus1, bus2));
    sq_engine_destroy(engine);
}

TEST_CASE("sq_bus_route rejects cycle")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int busA = sq_add_bus(engine, "A");
    int busB = sq_add_bus(engine, "B");
    REQUIRE(sq_bus_route(engine, busA, busB));
    CHECK_FALSE(sq_bus_route(engine, busB, busA));
    sq_engine_destroy(engine);
}

TEST_CASE("sq_send adds send from source to bus")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int src = sq_add_source(engine, "synth");
    int bus = sq_add_bus(engine, "FX");
    int sendId = sq_send(engine, src, bus, -6.0f, 0);
    REQUIRE(sendId > 0);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_remove_send removes a send")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int src = sq_add_source(engine, "synth");
    int bus = sq_add_bus(engine, "FX");
    int sendId = sq_send(engine, src, bus, -6.0f, 0);
    sq_remove_send(engine, src, sendId);
    sq_render(engine, 512);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_bus_send rejects cycle via send")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int busA = sq_add_bus(engine, "A");
    int busB = sq_add_bus(engine, "B");
    REQUIRE(sq_bus_route(engine, busA, busB));
    CHECK(sq_bus_send(engine, busB, busA, -6.0f, 0) == -1);
    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Source/Bus chain
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_source_append_proc adds to source chain")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int src = sq_add_source(engine, "synth");
    int proc = sq_source_append_proc(engine, src);
    REQUIRE(proc > 0);
    CHECK(sq_source_chain_size(engine, src) == 1);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_source_insert_proc inserts at index")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int src = sq_add_source(engine, "synth");
    sq_source_append_proc(engine, src);
    int proc = sq_source_insert_proc(engine, src, 0);
    REQUIRE(proc > 0);
    CHECK(sq_source_chain_size(engine, src) == 2);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_source_remove_proc removes from chain")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int src = sq_add_source(engine, "synth");
    sq_source_append_proc(engine, src);
    sq_source_remove_proc(engine, src, 0);
    CHECK(sq_source_chain_size(engine, src) == 0);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_bus_append_proc adds to bus chain")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int master = sq_master(engine);
    int proc = sq_bus_append_proc(engine, master);
    REQUIRE(proc > 0);
    CHECK(sq_bus_chain_size(engine, master) == 1);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_bus_remove_proc removes from bus chain")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int master = sq_master(engine);
    sq_bus_append_proc(engine, master);
    sq_bus_remove_proc(engine, master, 0);
    CHECK(sq_bus_chain_size(engine, master) == 0);
    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Source properties
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_source_name returns source name")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int src = sq_add_source(engine, "Lead");
    char* name = sq_source_name(engine, src);
    REQUIRE(name != nullptr);
    CHECK(std::string(name) == "Lead");
    sq_free_string(name);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_source_gain/sq_source_set_gain roundtrip")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int src = sq_add_source(engine, "synth");
    CHECK(sq_source_gain(engine, src) == 1.0f);
    sq_source_set_gain(engine, src, 0.5f);
    CHECK(sq_source_gain(engine, src) == 0.5f);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_source_pan/sq_source_set_pan roundtrip")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int src = sq_add_source(engine, "synth");
    CHECK(sq_source_pan(engine, src) == 0.0f);
    sq_source_set_pan(engine, src, -0.5f);
    CHECK(sq_source_pan(engine, src) == -0.5f);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_source_bypassed/sq_source_set_bypassed roundtrip")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int src = sq_add_source(engine, "synth");
    CHECK_FALSE(sq_source_bypassed(engine, src));
    sq_source_set_bypassed(engine, src, true);
    CHECK(sq_source_bypassed(engine, src));
    sq_source_set_bypassed(engine, src, false);
    CHECK_FALSE(sq_source_bypassed(engine, src));
    sq_engine_destroy(engine);
}

TEST_CASE("sq_source_midi_assign does not crash")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int src = sq_add_source(engine, "synth");
    sq_source_midi_assign(engine, src, "Keylab", 1, 36, 72);
    sq_render(engine, 512);
    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Bus properties
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_bus_name returns bus name")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int bus = sq_add_bus(engine, "Reverb");
    char* name = sq_bus_name(engine, bus);
    REQUIRE(name != nullptr);
    CHECK(std::string(name) == "Reverb");
    sq_free_string(name);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_bus_gain/sq_bus_set_gain roundtrip")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int master = sq_master(engine);
    CHECK(sq_bus_gain(engine, master) == 1.0f);
    sq_bus_set_gain(engine, master, 0.75f);
    CHECK(sq_bus_gain(engine, master) == 0.75f);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_bus_pan/sq_bus_set_pan roundtrip")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int master = sq_master(engine);
    CHECK(sq_bus_pan(engine, master) == 0.0f);
    sq_bus_set_pan(engine, master, 1.0f);
    CHECK(sq_bus_pan(engine, master) == 1.0f);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_bus_bypassed/sq_bus_set_bypassed roundtrip")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int master = sq_master(engine);
    CHECK_FALSE(sq_bus_bypassed(engine, master));
    sq_bus_set_bypassed(engine, master, true);
    CHECK(sq_bus_bypassed(engine, master));
    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Send tap
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_set_send_tap does not crash")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int src = sq_add_source(engine, "synth");
    int bus = sq_add_bus(engine, "FX");
    int sendId = sq_send(engine, src, bus, -6.0f, 0);
    sq_set_send_tap(engine, src, sendId, 1);
    sq_set_send_tap(engine, src, sendId, 0);
    sq_render(engine, 512);
    sq_engine_destroy(engine);
}

TEST_CASE("sq_bus_set_send_tap does not crash")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int busA = sq_add_bus(engine, "A");
    int busB = sq_add_bus(engine, "B");
    int sendId = sq_bus_send(engine, busA, busB, -6.0f, 0);
    REQUIRE(sendId > 0);
    sq_bus_set_send_tap(engine, busA, sendId, 1);
    sq_bus_set_send_tap(engine, busA, sendId, 0);
    sq_render(engine, 512);
    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Parameters
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_get_param/sq_set_param work via proc handle")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int src = sq_add_source(engine, "synth");
    int gen = sq_source_generator(engine, src);

    CHECK(sq_get_param(engine, gen, "gain") == 1.0f);
    REQUIRE(sq_set_param(engine, gen, "gain", 0.5f));
    CHECK(sq_get_param(engine, gen, "gain") == 0.5f);

    sq_engine_destroy(engine);
}

TEST_CASE("sq_param_descriptors returns descriptors for proc handle")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int src = sq_add_source(engine, "synth");
    int gen = sq_source_generator(engine, src);

    SqParamDescriptorList descs = sq_param_descriptors(engine, gen);
    REQUIRE(descs.count == 1);
    CHECK(std::string(descs.descriptors[0].name) == "gain");

    sq_free_param_descriptor_list(descs);
    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Metering
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_bus_peak and sq_bus_rms return 0 initially")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    int master = sq_master(engine);
    CHECK(sq_bus_peak(engine, master) == 0.0f);
    CHECK(sq_bus_rms(engine, master) == 0.0f);
    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Batching
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_batch_begin/sq_batch_commit work without crash")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    sq_batch_begin(engine);
    sq_add_source(engine, "a");
    sq_add_source(engine, "b");
    sq_batch_commit(engine);
    CHECK(sq_source_count(engine) == 2);
    sq_render(engine, 512);
    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Testing
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_render does not crash")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    sq_render(engine, 512);
    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Transport stubs
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Transport commands do not crash and queries reflect state")
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
    CHECK(sq_transport_tempo(engine) == 140.0);
    CHECK_FALSE(sq_transport_is_playing(engine));

    sq_render(engine, 512);
    sq_engine_destroy(engine);
}

// ═══════════════════════════════════════════════════════════════════
// Event scheduling stubs
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Event scheduling stubs return false via FFI")
{
    SqEngine engine = sq_engine_create(44100.0, 512, nullptr);
    CHECK_FALSE(sq_schedule_note_on(engine, 1, 0.0, 1, 60, 0.8f));
    CHECK_FALSE(sq_schedule_note_off(engine, 1, 1.0, 1, 60));
    CHECK_FALSE(sq_schedule_cc(engine, 1, 0.0, 1, 1, 64));
    CHECK_FALSE(sq_schedule_param_change(engine, 1, 0.0, "gain", 0.5f));
    sq_engine_destroy(engine);
}
