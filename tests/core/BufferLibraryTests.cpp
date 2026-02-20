#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/BufferLibrary.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>

using Catch::Matchers::WithinAbs;

// ═══════════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("BufferLibrary starts with zero buffers")
{
    squeeze::BufferLibrary lib;
    CHECK(lib.getNumBuffers() == 0);
    CHECK(lib.getBuffers().empty());
}

// ═══════════════════════════════════════════════════════════════════
// createBuffer
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("BufferLibrary createBuffer returns valid ID for valid params")
{
    squeeze::BufferLibrary lib;
    std::string error;
    int id = lib.createBuffer(2, 44100, 44100.0, "test", error);
    REQUIRE(id >= 1);
    CHECK(error.empty());
    CHECK(lib.getNumBuffers() == 1);
}

TEST_CASE("BufferLibrary createBuffer IDs are monotonically increasing")
{
    squeeze::BufferLibrary lib;
    std::string error;
    int id1 = lib.createBuffer(1, 100, 44100.0, "a", error);
    int id2 = lib.createBuffer(1, 100, 44100.0, "b", error);
    int id3 = lib.createBuffer(1, 100, 44100.0, "c", error);
    CHECK(id1 >= 1);
    CHECK(id2 > id1);
    CHECK(id3 > id2);
}

TEST_CASE("BufferLibrary createBuffer rejects invalid numChannels")
{
    squeeze::BufferLibrary lib;
    std::string error;
    int id = lib.createBuffer(0, 100, 44100.0, "bad", error);
    CHECK(id == -1);
    CHECK(!error.empty());
}

TEST_CASE("BufferLibrary createBuffer rejects invalid lengthInSamples")
{
    squeeze::BufferLibrary lib;
    std::string error;
    int id = lib.createBuffer(1, 0, 44100.0, "bad", error);
    CHECK(id == -1);
    CHECK(!error.empty());
}

TEST_CASE("BufferLibrary createBuffer rejects invalid sampleRate")
{
    squeeze::BufferLibrary lib;
    std::string error;
    int id = lib.createBuffer(1, 100, 0.0, "bad", error);
    CHECK(id == -1);
    CHECK(!error.empty());
}

TEST_CASE("BufferLibrary createBuffer sets error string on failure")
{
    squeeze::BufferLibrary lib;
    std::string error;
    lib.createBuffer(-1, 100, 44100.0, "bad", error);
    CHECK(error == "Invalid buffer parameters");
}

// ═══════════════════════════════════════════════════════════════════
// removeBuffer
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("BufferLibrary removeBuffer returns buffer for known ID")
{
    squeeze::BufferLibrary lib;
    std::string error;
    int id = lib.createBuffer(1, 100, 44100.0, "x", error);
    auto buf = lib.removeBuffer(id);
    REQUIRE(buf != nullptr);
    CHECK(buf->getNumChannels() == 1);
    CHECK(lib.getNumBuffers() == 0);
}

TEST_CASE("BufferLibrary removeBuffer returns nullptr for unknown ID")
{
    squeeze::BufferLibrary lib;
    auto buf = lib.removeBuffer(999);
    CHECK(buf == nullptr);
}

TEST_CASE("BufferLibrary removeBuffer makes getBuffer return nullptr")
{
    squeeze::BufferLibrary lib;
    std::string error;
    int id = lib.createBuffer(1, 100, 44100.0, "x", error);
    lib.removeBuffer(id);
    CHECK(lib.getBuffer(id) == nullptr);
}

// ═══════════════════════════════════════════════════════════════════
// getBuffer
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("BufferLibrary getBuffer returns valid pointer for known ID")
{
    squeeze::BufferLibrary lib;
    std::string error;
    int id = lib.createBuffer(2, 500, 48000.0, "stereo", error);
    auto* buf = lib.getBuffer(id);
    REQUIRE(buf != nullptr);
    CHECK(buf->getNumChannels() == 2);
    CHECK(buf->getLengthInSamples() == 500);
    CHECK(buf->getSampleRate() == 48000.0);
}

TEST_CASE("BufferLibrary getBuffer returns nullptr for unknown ID")
{
    squeeze::BufferLibrary lib;
    CHECK(lib.getBuffer(42) == nullptr);
}

// ═══════════════════════════════════════════════════════════════════
// getBufferName
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("BufferLibrary getBufferName returns correct name")
{
    squeeze::BufferLibrary lib;
    std::string error;
    int id = lib.createBuffer(1, 100, 44100.0, "kick", error);
    CHECK(lib.getBufferName(id) == "kick");
}

TEST_CASE("BufferLibrary getBufferName returns empty for unknown ID")
{
    squeeze::BufferLibrary lib;
    CHECK(lib.getBufferName(999).empty());
}

// ═══════════════════════════════════════════════════════════════════
// getBuffers
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("BufferLibrary getBuffers returns sorted list")
{
    squeeze::BufferLibrary lib;
    std::string error;
    int id1 = lib.createBuffer(1, 100, 44100.0, "c", error);
    int id2 = lib.createBuffer(1, 100, 44100.0, "a", error);
    int id3 = lib.createBuffer(1, 100, 44100.0, "b", error);

    auto list = lib.getBuffers();
    REQUIRE(list.size() == 3);
    CHECK(list[0].first == id1);
    CHECK(list[0].second == "c");
    CHECK(list[1].first == id2);
    CHECK(list[1].second == "a");
    CHECK(list[2].first == id3);
    CHECK(list[2].second == "b");

    // Verify sorted by ID
    CHECK(list[0].first < list[1].first);
    CHECK(list[1].first < list[2].first);
}

// ═══════════════════════════════════════════════════════════════════
// getNumBuffers
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("BufferLibrary getNumBuffers tracks additions and removals")
{
    squeeze::BufferLibrary lib;
    std::string error;
    CHECK(lib.getNumBuffers() == 0);
    int id1 = lib.createBuffer(1, 100, 44100.0, "a", error);
    CHECK(lib.getNumBuffers() == 1);
    lib.createBuffer(1, 100, 44100.0, "b", error);
    CHECK(lib.getNumBuffers() == 2);
    lib.removeBuffer(id1);
    CHECK(lib.getNumBuffers() == 1);
}

// ═══════════════════════════════════════════════════════════════════
// loadBuffer
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("BufferLibrary loadBuffer fails for nonexistent file")
{
    squeeze::BufferLibrary lib;
    std::string error;
    int id = lib.loadBuffer("/nonexistent/path/foo.wav", error);
    CHECK(id == -1);
    CHECK(!error.empty());
}

TEST_CASE("BufferLibrary loadBuffer loads a valid WAV file")
{
    // Create a temporary WAV file using JUCE
    juce::TemporaryFile tmpFile(".wav");
    auto outFile = tmpFile.getFile();
    {
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wavFormat.createWriterFor(
                new juce::FileOutputStream(outFile),
                44100.0, 2, 16, {}, 0));
        REQUIRE(writer != nullptr);

        // Write 100 samples of silence
        juce::AudioBuffer<float> data(2, 100);
        data.clear();
        // Put known values in channel 0
        for (int i = 0; i < 100; ++i)
            data.setSample(0, i, static_cast<float>(i) / 100.0f);
        writer->writeFromAudioSampleBuffer(data, 0, 100);
    }

    squeeze::BufferLibrary lib;
    std::string error;
    int id = lib.loadBuffer(outFile.getFullPathName().toStdString(), error);
    REQUIRE(id >= 1);
    CHECK(error.empty());

    auto* buf = lib.getBuffer(id);
    REQUIRE(buf != nullptr);
    CHECK(buf->getNumChannels() == 2);
    CHECK(buf->getLengthInSamples() == 100);
    CHECK_THAT(buf->getSampleRate(), WithinAbs(44100.0, 1.0));

    // Check name is filename without extension
    CHECK(lib.getBufferName(id) == outFile.getFileNameWithoutExtension().toStdString());

    // Check file path is stored
    CHECK(buf->getFilePath() == outFile.getFullPathName().toStdString());
}

TEST_CASE("BufferLibrary loadBuffer with unsupported format returns -1")
{
    // Create a temp file with garbage content
    juce::TemporaryFile tmpFile(".xyz");
    auto outFile = tmpFile.getFile();
    outFile.replaceWithText("this is not audio data");

    squeeze::BufferLibrary lib;
    std::string error;
    int id = lib.loadBuffer(outFile.getFullPathName().toStdString(), error);
    CHECK(id == -1);
    CHECK(!error.empty());
}
