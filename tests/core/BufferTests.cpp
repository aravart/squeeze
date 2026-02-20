#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/Buffer.h"

using namespace squeeze;
using Catch::Matchers::WithinAbs;

// ═══════════════════════════════════════════════════════════════════
// createEmpty
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Buffer::createEmpty succeeds with valid params")
{
    auto buf = Buffer::createEmpty(2, 44100, 44100.0, "test");
    REQUIRE(buf != nullptr);
    CHECK(buf->getNumChannels() == 2);
    CHECK(buf->getLengthInSamples() == 44100);
    CHECK(buf->getSampleRate() == 44100.0);
    CHECK(buf->getName() == "test");
    CHECK(buf->getFilePath().empty());
    CHECK(buf->writePosition.load() == 0);
}

TEST_CASE("Buffer::createEmpty with default name gives empty string")
{
    auto buf = Buffer::createEmpty(1, 100, 44100.0);
    REQUIRE(buf != nullptr);
    CHECK(buf->getName().empty());
}

TEST_CASE("Buffer::createEmpty returns nullptr for zero channels")
{
    CHECK(Buffer::createEmpty(0, 100, 44100.0) == nullptr);
}

TEST_CASE("Buffer::createEmpty returns nullptr for negative channels")
{
    CHECK(Buffer::createEmpty(-1, 100, 44100.0) == nullptr);
}

TEST_CASE("Buffer::createEmpty returns nullptr for zero length")
{
    CHECK(Buffer::createEmpty(2, 0, 44100.0) == nullptr);
}

TEST_CASE("Buffer::createEmpty returns nullptr for negative length")
{
    CHECK(Buffer::createEmpty(2, -1, 44100.0) == nullptr);
}

TEST_CASE("Buffer::createEmpty returns nullptr for zero sample rate")
{
    CHECK(Buffer::createEmpty(2, 100, 0.0) == nullptr);
}

TEST_CASE("Buffer::createEmpty returns nullptr for negative sample rate")
{
    CHECK(Buffer::createEmpty(2, 100, -44100.0) == nullptr);
}

TEST_CASE("Buffer::createEmpty produces zeroed samples")
{
    auto buf = Buffer::createEmpty(2, 256, 44100.0);
    REQUIRE(buf != nullptr);
    const float* L = buf->getReadPointer(0);
    const float* R = buf->getReadPointer(1);
    for (int i = 0; i < 256; ++i)
    {
        CHECK(L[i] == 0.0f);
        CHECK(R[i] == 0.0f);
    }
}

// ═══════════════════════════════════════════════════════════════════
// createFromData
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Buffer::createFromData succeeds with valid data")
{
    juce::AudioBuffer<float> data(2, 1000);
    data.clear();
    for (int i = 0; i < 1000; ++i)
        data.setSample(0, i, static_cast<float>(i) / 1000.0f);

    auto buf = Buffer::createFromData(std::move(data), 48000.0, "kick", "/samples/kick.wav");
    REQUIRE(buf != nullptr);
    CHECK(buf->getNumChannels() == 2);
    CHECK(buf->getLengthInSamples() == 1000);
    CHECK(buf->getSampleRate() == 48000.0);
    CHECK(buf->getName() == "kick");
    CHECK(buf->getFilePath() == "/samples/kick.wav");
    CHECK(buf->writePosition.load() == 1000);
}

TEST_CASE("Buffer::createFromData returns nullptr for zero-length AudioBuffer")
{
    juce::AudioBuffer<float> data(2, 0);
    CHECK(Buffer::createFromData(std::move(data), 44100.0, "bad") == nullptr);
}

TEST_CASE("Buffer::createFromData returns nullptr for zero-channel AudioBuffer")
{
    juce::AudioBuffer<float> data(0, 100);
    CHECK(Buffer::createFromData(std::move(data), 44100.0, "bad") == nullptr);
}

TEST_CASE("Buffer::createFromData returns nullptr for invalid sample rate")
{
    juce::AudioBuffer<float> data(1, 100);
    CHECK(Buffer::createFromData(std::move(data), 0.0, "bad") == nullptr);
}

// ═══════════════════════════════════════════════════════════════════
// Metadata
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Buffer::getLengthInSeconds equals length / sampleRate")
{
    auto buf = Buffer::createEmpty(1, 44100, 44100.0);
    REQUIRE(buf != nullptr);
    CHECK_THAT(buf->getLengthInSeconds(), WithinAbs(1.0, 1e-9));

    auto buf2 = Buffer::createEmpty(1, 22050, 44100.0);
    CHECK_THAT(buf2->getLengthInSeconds(), WithinAbs(0.5, 1e-9));
}

// ═══════════════════════════════════════════════════════════════════
// Read/Write pointers
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Buffer::getReadPointer returns valid pointer for valid channel")
{
    auto buf = Buffer::createEmpty(2, 100, 44100.0);
    REQUIRE(buf->getReadPointer(0) != nullptr);
    REQUIRE(buf->getReadPointer(1) != nullptr);
}

TEST_CASE("Buffer::getReadPointer returns nullptr for out-of-range channel")
{
    auto buf = Buffer::createEmpty(2, 100, 44100.0);
    CHECK(buf->getReadPointer(-1) == nullptr);
    CHECK(buf->getReadPointer(2) == nullptr);
    CHECK(buf->getReadPointer(100) == nullptr);
}

TEST_CASE("Buffer::getWritePointer returns valid pointer for valid channel")
{
    auto buf = Buffer::createEmpty(2, 100, 44100.0);
    REQUIRE(buf->getWritePointer(0) != nullptr);
    REQUIRE(buf->getWritePointer(1) != nullptr);
}

TEST_CASE("Buffer::getWritePointer returns nullptr for out-of-range channel")
{
    auto buf = Buffer::createEmpty(2, 100, 44100.0);
    CHECK(buf->getWritePointer(-1) == nullptr);
    CHECK(buf->getWritePointer(2) == nullptr);
}

TEST_CASE("Buffer read and write pointers are stable")
{
    auto buf = Buffer::createEmpty(1, 100, 44100.0);
    const float* r1 = buf->getReadPointer(0);
    float* w1 = buf->getWritePointer(0);
    const float* r2 = buf->getReadPointer(0);
    float* w2 = buf->getWritePointer(0);
    CHECK(r1 == r2);
    CHECK(w1 == w2);
}

// ═══════════════════════════════════════════════════════════════════
// writePosition
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Buffer writePosition can be stored and loaded atomically")
{
    auto buf = Buffer::createEmpty(1, 1000, 44100.0);
    CHECK(buf->writePosition.load(std::memory_order_acquire) == 0);
    buf->writePosition.store(500, std::memory_order_release);
    CHECK(buf->writePosition.load(std::memory_order_acquire) == 500);
}

// ═══════════════════════════════════════════════════════════════════
// clear
// ═══════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════
// Tempo
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Buffer::getTempo defaults to 0.0")
{
    auto buf = Buffer::createEmpty(1, 100, 44100.0);
    REQUIRE(buf != nullptr);
    CHECK(buf->getTempo() == 0.0);
}

TEST_CASE("Buffer::setTempo/getTempo round-trip")
{
    auto buf = Buffer::createEmpty(1, 100, 44100.0);
    REQUIRE(buf != nullptr);
    buf->setTempo(120.0);
    CHECK(buf->getTempo() == 120.0);
    buf->setTempo(98.5);
    CHECK(buf->getTempo() == 98.5);
}

TEST_CASE("Buffer::setTempo to 0.0 clears it")
{
    auto buf = Buffer::createEmpty(1, 100, 44100.0);
    buf->setTempo(140.0);
    buf->setTempo(0.0);
    CHECK(buf->getTempo() == 0.0);
}

TEST_CASE("Buffer::createFromData defaults tempo to 0.0")
{
    juce::AudioBuffer<float> data(1, 100);
    data.clear();
    auto buf = Buffer::createFromData(std::move(data), 44100.0, "test");
    REQUIRE(buf != nullptr);
    CHECK(buf->getTempo() == 0.0);
}

// ═══════════════════════════════════════════════════════════════════
// clear
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Buffer::clear zeroes all samples and resets writePosition")
{
    auto buf = Buffer::createEmpty(2, 100, 44100.0);
    float* L = buf->getWritePointer(0);
    for (int i = 0; i < 100; ++i)
        L[i] = 1.0f;
    buf->writePosition.store(100, std::memory_order_release);

    buf->clear();

    CHECK(buf->writePosition.load() == 0);
    const float* rL = buf->getReadPointer(0);
    for (int i = 0; i < 100; ++i)
        CHECK(rL[i] == 0.0f);
}
