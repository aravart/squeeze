#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "ffi/squeeze_ffi.h"
#include <cmath>
#include <cstring>
#include <vector>

using Catch::Matchers::WithinAbs;

// ═══════════════════════════════════════════════════════════════════
// Buffer creation
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_create_buffer returns positive ID for valid params")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    char* error = nullptr;
    int id = sq_create_buffer(e, 2, 44100, 44100.0, "test", &error);
    REQUIRE(id >= 1);
    CHECK(error == nullptr);
    sq_engine_destroy(e);
}

TEST_CASE("sq_create_buffer IDs are monotonically increasing")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int id1 = sq_create_buffer(e, 1, 100, 44100.0, "a", nullptr);
    int id2 = sq_create_buffer(e, 1, 100, 44100.0, "b", nullptr);
    int id3 = sq_create_buffer(e, 1, 100, 44100.0, "c", nullptr);
    CHECK(id1 >= 1);
    CHECK(id2 > id1);
    CHECK(id3 > id2);
    sq_engine_destroy(e);
}

TEST_CASE("sq_create_buffer returns -1 for invalid params and sets error")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    char* error = nullptr;

    CHECK(sq_create_buffer(e, 0, 100, 44100.0, "bad", &error) == -1);
    REQUIRE(error != nullptr);
    sq_free_string(error);
    error = nullptr;

    CHECK(sq_create_buffer(e, 1, 0, 44100.0, "bad", &error) == -1);
    REQUIRE(error != nullptr);
    sq_free_string(error);
    error = nullptr;

    CHECK(sq_create_buffer(e, 1, 100, 0.0, "bad", &error) == -1);
    REQUIRE(error != nullptr);
    sq_free_string(error);

    sq_engine_destroy(e);
}

TEST_CASE("sq_create_buffer with NULL error pointer does not crash on failure")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    CHECK(sq_create_buffer(e, 0, 100, 44100.0, "bad", nullptr) == -1);
    sq_engine_destroy(e);
}

// ═══════════════════════════════════════════════════════════════════
// Buffer removal
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_remove_buffer removes an existing buffer")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int id = sq_create_buffer(e, 1, 100, 44100.0, "x", nullptr);
    CHECK(sq_buffer_count(e) == 1);
    CHECK(sq_remove_buffer(e, id) == true);
    CHECK(sq_buffer_count(e) == 0);
    sq_engine_destroy(e);
}

TEST_CASE("sq_remove_buffer returns false for unknown ID")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    CHECK(sq_remove_buffer(e, 999) == false);
    sq_engine_destroy(e);
}

TEST_CASE("sq_buffer_count tracks additions and removals")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    CHECK(sq_buffer_count(e) == 0);
    int id1 = sq_create_buffer(e, 1, 100, 44100.0, "a", nullptr);
    CHECK(sq_buffer_count(e) == 1);
    sq_create_buffer(e, 1, 100, 44100.0, "b", nullptr);
    CHECK(sq_buffer_count(e) == 2);
    sq_remove_buffer(e, id1);
    CHECK(sq_buffer_count(e) == 1);
    sq_engine_destroy(e);
}

// ═══════════════════════════════════════════════════════════════════
// Buffer queries
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_buffer_num_channels returns correct value")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int id = sq_create_buffer(e, 2, 100, 44100.0, "stereo", nullptr);
    CHECK(sq_buffer_num_channels(e, id) == 2);
    CHECK(sq_buffer_num_channels(e, 999) == 0);
    sq_engine_destroy(e);
}

TEST_CASE("sq_buffer_length returns correct value")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int id = sq_create_buffer(e, 1, 44100, 44100.0, "x", nullptr);
    CHECK(sq_buffer_length(e, id) == 44100);
    CHECK(sq_buffer_length(e, 999) == 0);
    sq_engine_destroy(e);
}

TEST_CASE("sq_buffer_sample_rate returns correct value")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int id = sq_create_buffer(e, 1, 100, 48000.0, "x", nullptr);
    CHECK(sq_buffer_sample_rate(e, id) == 48000.0);
    CHECK(sq_buffer_sample_rate(e, 999) == 0.0);
    sq_engine_destroy(e);
}

TEST_CASE("sq_buffer_name returns correct name")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int id = sq_create_buffer(e, 1, 100, 44100.0, "kick", nullptr);
    char* name = sq_buffer_name(e, id);
    REQUIRE(name != nullptr);
    CHECK(std::string(name) == "kick");
    sq_free_string(name);

    CHECK(sq_buffer_name(e, 999) == nullptr);
    sq_engine_destroy(e);
}

TEST_CASE("sq_buffer_length_seconds returns correct value")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int id = sq_create_buffer(e, 1, 44100, 44100.0, "x", nullptr);
    CHECK_THAT(sq_buffer_length_seconds(e, id), WithinAbs(1.0, 1e-9));
    CHECK(sq_buffer_length_seconds(e, 999) == 0.0);
    sq_engine_destroy(e);
}

TEST_CASE("sq_buffer_write_position starts at 0 for empty buffer")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int id = sq_create_buffer(e, 1, 100, 44100.0, "x", nullptr);
    CHECK(sq_buffer_write_position(e, id) == 0);
    CHECK(sq_buffer_write_position(e, 999) == -1);
    sq_engine_destroy(e);
}

TEST_CASE("sq_buffer_set_write_position updates position")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int id = sq_create_buffer(e, 1, 100, 44100.0, "x", nullptr);
    sq_buffer_set_write_position(e, id, 50);
    CHECK(sq_buffer_write_position(e, id) == 50);
    sq_engine_destroy(e);
}

// ═══════════════════════════════════════════════════════════════════
// Buffer sample data
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_buffer_write and sq_buffer_read round-trip samples")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int id = sq_create_buffer(e, 2, 100, 44100.0, "x", nullptr);

    std::vector<float> src(100);
    for (int i = 0; i < 100; ++i) src[i] = static_cast<float>(i) / 100.0f;

    int written = sq_buffer_write(e, id, 0, 0, src.data(), 100);
    CHECK(written == 100);

    std::vector<float> dest(100, -1.0f);
    int nread = sq_buffer_read(e, id, 0, 0, dest.data(), 100);
    CHECK(nread == 100);

    for (int i = 0; i < 100; ++i)
        CHECK(dest[i] == src[i]);

    sq_engine_destroy(e);
}

TEST_CASE("sq_buffer_read clamps to buffer length")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int id = sq_create_buffer(e, 1, 50, 44100.0, "x", nullptr);

    std::vector<float> dest(100, -1.0f);
    int nread = sq_buffer_read(e, id, 0, 0, dest.data(), 100);
    CHECK(nread == 50);

    sq_engine_destroy(e);
}

TEST_CASE("sq_buffer_write clamps to buffer length")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int id = sq_create_buffer(e, 1, 50, 44100.0, "x", nullptr);

    std::vector<float> src(100, 1.0f);
    int written = sq_buffer_write(e, id, 0, 0, src.data(), 100);
    CHECK(written == 50);

    sq_engine_destroy(e);
}

TEST_CASE("sq_buffer_read with offset")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int id = sq_create_buffer(e, 1, 100, 44100.0, "x", nullptr);

    std::vector<float> src(100);
    for (int i = 0; i < 100; ++i) src[i] = static_cast<float>(i);
    sq_buffer_write(e, id, 0, 0, src.data(), 100);

    std::vector<float> dest(10);
    int nread = sq_buffer_read(e, id, 0, 90, dest.data(), 10);
    CHECK(nread == 10);
    CHECK(dest[0] == 90.0f);

    sq_engine_destroy(e);
}

TEST_CASE("sq_buffer_read returns 0 for invalid channel")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int id = sq_create_buffer(e, 1, 100, 44100.0, "x", nullptr);

    float dest;
    CHECK(sq_buffer_read(e, id, 5, 0, &dest, 1) == 0);
    CHECK(sq_buffer_read(e, id, -1, 0, &dest, 1) == 0);

    sq_engine_destroy(e);
}

TEST_CASE("sq_buffer_read returns 0 for invalid buffer ID")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    float dest;
    CHECK(sq_buffer_read(e, 999, 0, 0, &dest, 1) == 0);
    sq_engine_destroy(e);
}

TEST_CASE("sq_buffer_read returns 0 for out-of-range offset")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int id = sq_create_buffer(e, 1, 100, 44100.0, "x", nullptr);
    float dest;
    CHECK(sq_buffer_read(e, id, 0, 100, &dest, 1) == 0);
    CHECK(sq_buffer_read(e, id, 0, -1, &dest, 1) == 0);
    sq_engine_destroy(e);
}

TEST_CASE("sq_buffer_clear zeroes data and resets write position")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int id = sq_create_buffer(e, 1, 100, 44100.0, "x", nullptr);

    float val = 1.0f;
    sq_buffer_write(e, id, 0, 0, &val, 1);
    sq_buffer_set_write_position(e, id, 50);

    sq_buffer_clear(e, id);

    CHECK(sq_buffer_write_position(e, id) == 0);
    float dest;
    sq_buffer_read(e, id, 0, 0, &dest, 1);
    CHECK(dest == 0.0f);

    sq_engine_destroy(e);
}

TEST_CASE("sq_buffer_clear on unknown ID is a no-op")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    sq_buffer_clear(e, 999);  // should not crash
    sq_engine_destroy(e);
}

// ═══════════════════════════════════════════════════════════════════
// PlayerProcessor integration tests
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sq_add_source_player creates a source with PlayerProcessor")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    char* error = nullptr;
    int src = sq_add_source_player(e, "player1", &error);
    REQUIRE(src > 0);
    CHECK(error == nullptr);

    // Should have a generator
    int gen = sq_source_generator(e, src);
    CHECK(gen > 0);

    // Generator should have 7 parameters
    SqParamDescriptorList descs = sq_param_descriptors(e, gen);
    CHECK(descs.count == 7);
    sq_free_param_descriptor_list(descs);

    sq_engine_destroy(e);
}

TEST_CASE("sq_source_set_buffer assigns a buffer to player source")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int buf = sq_create_buffer(e, 1, 1000, 44100.0, "test", nullptr);
    int src = sq_add_source_player(e, "player", nullptr);

    CHECK(sq_source_set_buffer(e, src, buf) == true);
    sq_engine_destroy(e);
}

TEST_CASE("sq_source_set_buffer returns false for unknown buffer ID")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int src = sq_add_source_player(e, "player", nullptr);

    CHECK(sq_source_set_buffer(e, src, 999) == false);
    sq_engine_destroy(e);
}

TEST_CASE("sq_source_set_buffer returns false for non-player source")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);
    int buf = sq_create_buffer(e, 1, 100, 44100.0, "test", nullptr);
    int src = sq_add_source(e, "gain_src");

    CHECK(sq_source_set_buffer(e, src, buf) == false);
    sq_engine_destroy(e);
}

TEST_CASE("PlayerProcessor plays audio through FFI after buffer assignment")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);

    // Create buffer with constant signal
    int buf_id = sq_create_buffer(e, 2, 44100, 44100.0, "tone", nullptr);
    std::vector<float> tone(44100, 0.5f);
    sq_buffer_write(e, buf_id, 0, 0, tone.data(), 44100);
    sq_buffer_write(e, buf_id, 1, 0, tone.data(), 44100);

    // Create player source and assign buffer
    int src = sq_add_source_player(e, "player", nullptr);
    sq_source_set_buffer(e, src, buf_id);

    // Route to master
    int master = sq_master(e);
    sq_route(e, src, master);

    // Set playing with no fade
    int gen = sq_source_generator(e, src);
    sq_set_param(e, gen, "fade_ms", 0.0f);
    sq_set_param(e, gen, "playing", 1.0f);

    // Render
    sq_render(e, 512);

    // Master should have signal
    float peak = sq_bus_peak(e, master);
    CHECK(peak > 0.0f);

    sq_engine_destroy(e);
}

TEST_CASE("PlayerProcessor loop_mode through FFI")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);

    int buf_id = sq_create_buffer(e, 1, 100, 44100.0, "short", nullptr);
    std::vector<float> data(100, 0.3f);
    sq_buffer_write(e, buf_id, 0, 0, data.data(), 100);

    int src = sq_add_source_player(e, "loop", nullptr);
    sq_source_set_buffer(e, src, buf_id);
    sq_route(e, src, sq_master(e));

    int gen = sq_source_generator(e, src);
    sq_set_param(e, gen, "fade_ms", 0.0f);
    sq_set_param(e, gen, "loop_mode", 1.0f); // forward loop
    sq_set_param(e, gen, "playing", 1.0f);

    // Render more samples than buffer length — should loop
    sq_render(e, 512);

    // Should still be playing
    CHECK(sq_get_param(e, gen, "playing") >= 0.5f);

    sq_engine_destroy(e);
}

TEST_CASE("PlayerProcessor auto-stops with loop off through FFI")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);

    int buf_id = sq_create_buffer(e, 1, 100, 44100.0, "short", nullptr);
    std::vector<float> data(100, 0.3f);
    sq_buffer_write(e, buf_id, 0, 0, data.data(), 100);

    int src = sq_add_source_player(e, "once", nullptr);
    sq_source_set_buffer(e, src, buf_id);
    sq_route(e, src, sq_master(e));

    int gen = sq_source_generator(e, src);
    sq_set_param(e, gen, "fade_ms", 0.0f);
    sq_set_param(e, gen, "loop_mode", 0.0f); // loop off
    sq_set_param(e, gen, "playing", 1.0f);

    sq_render(e, 512);

    CHECK(sq_get_param(e, gen, "playing") < 0.5f);

    sq_engine_destroy(e);
}

TEST_CASE("PlayerProcessor speed parameter through FFI")
{
    SqEngine e = sq_engine_create(44100.0, 512, nullptr);

    int buf_id = sq_create_buffer(e, 1, 10000, 44100.0, "long", nullptr);
    std::vector<float> data(10000, 0.4f);
    sq_buffer_write(e, buf_id, 0, 0, data.data(), 10000);

    int src = sq_add_source_player(e, "fast", nullptr);
    sq_source_set_buffer(e, src, buf_id);
    sq_route(e, src, sq_master(e));

    int gen = sq_source_generator(e, src);
    sq_set_param(e, gen, "fade_ms", 0.0f);
    sq_set_param(e, gen, "speed", 2.0f);
    sq_set_param(e, gen, "playing", 1.0f);

    sq_render(e, 512);

    float pos = sq_get_param(e, gen, "position");
    // At 2x speed, position should be further than at 1x
    CHECK(pos > 0.05f);

    sq_engine_destroy(e);
}
