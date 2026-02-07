#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/Buffer.h"

#include <juce_audio_formats/juce_audio_formats.h>

using namespace squeeze;
using Catch::Matchers::WithinAbs;

// ============================================================
// Helper: write a WAV file with known content for testing
// ============================================================

static juce::File createTestWavFile(int numChannels, int numSamples,
                                     double sampleRate, float fillValue = 0.5f)
{
    auto tempFile = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("squeeze_test.wav");

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(tempFile),
                            sampleRate, numChannels, 16, {}, 0));
    REQUIRE(writer != nullptr);

    juce::AudioBuffer<float> data(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
            data.setSample(ch, s, fillValue);

    writer->writeFromAudioSampleBuffer(data, 0, numSamples);
    writer.reset(); // flush and close

    return tempFile;
}

// ============================================================
// loadFromFile tests
// ============================================================

TEST_CASE("Buffer loadFromFile loads a WAV file correctly")
{
    juce::AudioFormatManager fmgr;
    fmgr.registerBasicFormats();

    auto tempFile = createTestWavFile(2, 1000, 44100.0, 0.25f);

    std::string err;
    auto buf = Buffer::loadFromFile(tempFile.getFullPathName().toStdString(), fmgr, err);
    REQUIRE(buf != nullptr);
    CHECK(err.empty());

    CHECK(buf->getNumChannels() == 2);
    CHECK(buf->getLengthInSamples() == 1000);
    CHECK_THAT(buf->getSampleRate(), WithinAbs(44100.0, 0.01));
    CHECK(buf->getName() == "squeeze_test.wav");
    CHECK(!buf->getFilePath().empty());

    // writePosition should be at end for file-loaded buffers
    CHECK(buf->writePosition.load() == 1000);

    // Verify audio data
    const float* ch0 = buf->getReadPointer(0);
    REQUIRE(ch0 != nullptr);
    // 16-bit WAV quantization means we can't expect exact float match
    CHECK_THAT(static_cast<double>(ch0[0]), WithinAbs(0.25, 0.001));

    tempFile.deleteFile();
}

TEST_CASE("Buffer loadFromFile returns nullptr for nonexistent file")
{
    juce::AudioFormatManager fmgr;
    fmgr.registerBasicFormats();

    std::string err;
    auto buf = Buffer::loadFromFile("/no/such/file.wav", fmgr, err);
    CHECK(buf == nullptr);
    CHECK(!err.empty());
}

TEST_CASE("Buffer loadFromFile returns nullptr for unsupported format")
{
    juce::AudioFormatManager fmgr;
    fmgr.registerBasicFormats();

    // Create a file that exists but isn't a valid audio file
    auto tempFile = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("squeeze_test.txt");
    tempFile.replaceWithText("not audio data");

    std::string err;
    auto buf = Buffer::loadFromFile(tempFile.getFullPathName().toStdString(), fmgr, err);
    CHECK(buf == nullptr);
    CHECK(!err.empty());

    tempFile.deleteFile();
}

TEST_CASE("Buffer loadFromFile reads mono file")
{
    juce::AudioFormatManager fmgr;
    fmgr.registerBasicFormats();

    auto tempFile = createTestWavFile(1, 500, 22050.0, 0.75f);

    std::string err;
    auto buf = Buffer::loadFromFile(tempFile.getFullPathName().toStdString(), fmgr, err);
    REQUIRE(buf != nullptr);

    CHECK(buf->getNumChannels() == 1);
    CHECK(buf->getLengthInSamples() == 500);
    CHECK_THAT(buf->getSampleRate(), WithinAbs(22050.0, 0.01));

    tempFile.deleteFile();
}

// ============================================================
// createEmpty tests
// ============================================================

TEST_CASE("Buffer createEmpty creates zeroed buffer with correct metadata")
{
    auto buf = Buffer::createEmpty(2, 44100, 44100.0, "recording");
    REQUIRE(buf != nullptr);

    CHECK(buf->getNumChannels() == 2);
    CHECK(buf->getLengthInSamples() == 44100);
    CHECK_THAT(buf->getSampleRate(), WithinAbs(44100.0, 0.01));
    CHECK(buf->getName() == "recording");
    CHECK(buf->getFilePath().empty());

    // writePosition starts at 0 for empty buffers
    CHECK(buf->writePosition.load() == 0);

    // All samples should be zero
    const float* ch0 = buf->getReadPointer(0);
    const float* ch1 = buf->getReadPointer(1);
    for (int s = 0; s < 44100; ++s)
    {
        CHECK(ch0[s] == 0.0f);
        CHECK(ch1[s] == 0.0f);
    }
}

TEST_CASE("Buffer createEmpty returns nullptr for invalid parameters")
{
    CHECK(Buffer::createEmpty(0, 44100, 44100.0) == nullptr);
    CHECK(Buffer::createEmpty(-1, 44100, 44100.0) == nullptr);
    CHECK(Buffer::createEmpty(2, 0, 44100.0) == nullptr);
    CHECK(Buffer::createEmpty(2, -1, 44100.0) == nullptr);
    CHECK(Buffer::createEmpty(2, 44100, 0.0) == nullptr);
    CHECK(Buffer::createEmpty(2, 44100, -1.0) == nullptr);
}

TEST_CASE("Buffer createEmpty with default empty name")
{
    auto buf = Buffer::createEmpty(1, 100, 48000.0);
    REQUIRE(buf != nullptr);
    CHECK(buf->getName().empty());
}

// ============================================================
// Audio data access tests
// ============================================================

TEST_CASE("Buffer getAudioData returns reference to internal data")
{
    auto buf = Buffer::createEmpty(2, 100, 44100.0);
    REQUIRE(buf != nullptr);

    const auto& constData = buf->getAudioData();
    CHECK(constData.getNumChannels() == 2);
    CHECK(constData.getNumSamples() == 100);

    auto& mutData = buf->getAudioData();
    CHECK(mutData.getNumChannels() == 2);
    CHECK(mutData.getNumSamples() == 100);
}

TEST_CASE("Buffer getWritePointer allows writing samples")
{
    auto buf = Buffer::createEmpty(1, 10, 44100.0);
    REQUIRE(buf != nullptr);

    float* ptr = buf->getWritePointer(0);
    for (int s = 0; s < 10; ++s)
        ptr[s] = static_cast<float>(s) * 0.1f;

    const float* rptr = buf->getReadPointer(0);
    for (int s = 0; s < 10; ++s)
        CHECK_THAT(static_cast<double>(rptr[s]), WithinAbs(s * 0.1, 0.0001));
}

// ============================================================
// Metadata tests
// ============================================================

TEST_CASE("Buffer getLengthInSeconds computes correctly")
{
    auto buf = Buffer::createEmpty(1, 44100, 44100.0);
    REQUIRE(buf != nullptr);
    CHECK_THAT(buf->getLengthInSeconds(), WithinAbs(1.0, 0.0001));

    auto buf2 = Buffer::createEmpty(1, 22050, 44100.0);
    REQUIRE(buf2 != nullptr);
    CHECK_THAT(buf2->getLengthInSeconds(), WithinAbs(0.5, 0.0001));

    auto buf3 = Buffer::createEmpty(2, 96000, 48000.0);
    REQUIRE(buf3 != nullptr);
    CHECK_THAT(buf3->getLengthInSeconds(), WithinAbs(2.0, 0.0001));
}

// ============================================================
// Recording support tests
// ============================================================

TEST_CASE("Buffer writePosition is atomic and starts at 0 for empty buffers")
{
    auto buf = Buffer::createEmpty(1, 1000, 44100.0);
    REQUIRE(buf != nullptr);

    CHECK(buf->writePosition.load() == 0);

    // Simulate recording: write and advance
    float* ptr = buf->getWritePointer(0);
    ptr[0] = 1.0f;
    ptr[1] = 2.0f;
    buf->writePosition.store(2, std::memory_order_release);

    CHECK(buf->writePosition.load(std::memory_order_acquire) == 2);
    CHECK(buf->getReadPointer(0)[0] == 1.0f);
    CHECK(buf->getReadPointer(0)[1] == 2.0f);
}

TEST_CASE("Buffer clear zeroes all samples and resets writePosition")
{
    auto buf = Buffer::createEmpty(2, 100, 44100.0);
    REQUIRE(buf != nullptr);

    // Write some data
    float* ch0 = buf->getWritePointer(0);
    for (int s = 0; s < 100; ++s)
        ch0[s] = 1.0f;
    buf->writePosition.store(100);

    // Clear
    buf->clear();

    CHECK(buf->writePosition.load() == 0);
    for (int s = 0; s < 100; ++s)
        CHECK(buf->getReadPointer(0)[s] == 0.0f);
}

TEST_CASE("Buffer resize preserves existing data")
{
    auto buf = Buffer::createEmpty(1, 100, 44100.0);
    REQUIRE(buf != nullptr);

    // Write known pattern
    float* ptr = buf->getWritePointer(0);
    for (int s = 0; s < 100; ++s)
        ptr[s] = static_cast<float>(s);

    // Grow
    buf->resize(1, 200);
    CHECK(buf->getLengthInSamples() == 200);
    CHECK(buf->getNumChannels() == 1);

    // Old data preserved
    const float* rptr = buf->getReadPointer(0);
    for (int s = 0; s < 100; ++s)
        CHECK(rptr[s] == static_cast<float>(s));

    // New region zeroed
    for (int s = 100; s < 200; ++s)
        CHECK(rptr[s] == 0.0f);
}

TEST_CASE("Buffer resize to smaller preserves data up to new length")
{
    auto buf = Buffer::createEmpty(1, 100, 44100.0);
    REQUIRE(buf != nullptr);

    float* ptr = buf->getWritePointer(0);
    for (int s = 0; s < 100; ++s)
        ptr[s] = static_cast<float>(s);

    buf->resize(1, 50);
    CHECK(buf->getLengthInSamples() == 50);

    const float* rptr = buf->getReadPointer(0);
    for (int s = 0; s < 50; ++s)
        CHECK(rptr[s] == static_cast<float>(s));
}

TEST_CASE("Buffer resize can change channel count")
{
    auto buf = Buffer::createEmpty(1, 100, 44100.0);
    REQUIRE(buf != nullptr);

    buf->resize(2, 100);
    CHECK(buf->getNumChannels() == 2);
    CHECK(buf->getLengthInSamples() == 100);
}

// ============================================================
// Pointer stability tests
// ============================================================

TEST_CASE("Buffer read/write pointers are stable between calls")
{
    auto buf = Buffer::createEmpty(2, 1000, 44100.0);
    REQUIRE(buf != nullptr);

    const float* r1 = buf->getReadPointer(0);
    const float* r2 = buf->getReadPointer(0);
    CHECK(r1 == r2);

    float* w1 = buf->getWritePointer(0);
    float* w2 = buf->getWritePointer(0);
    CHECK(w1 == w2);

    // Read and write pointers should point to same memory
    CHECK(static_cast<const float*>(w1) == r1);
}
