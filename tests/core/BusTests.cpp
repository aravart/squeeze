#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/Processor.h"
#include "core/Bus.h"

#include <cmath>

using namespace squeeze;
using Catch::Approx;

namespace {

// --- Test helpers ---

class LatencyProcessor : public Processor {
public:
    explicit LatencyProcessor(int latency)
        : Processor("Latency"), latency_(latency) {}

    void prepare(double, int) override {}
    void process(juce::AudioBuffer<float>&) override {}
    int getLatencySamples() const override { return latency_; }

    int prepareCount_ = 0;
    int releaseCount_ = 0;

private:
    int latency_;
};

class TrackingProcessor : public Processor {
public:
    explicit TrackingProcessor(const std::string& name = "Track")
        : Processor(name) {}

    void prepare(double sr, int bs) override { prepareCount_++; sr_ = sr; bs_ = bs; }
    void release() override { releaseCount_++; }
    void process(juce::AudioBuffer<float>&) override {}

    int prepareCount_ = 0;
    int releaseCount_ = 0;
    double sr_ = 0.0;
    int bs_ = 0;
};

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════
// Construction & Identity
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Bus: stores name from construction")
{
    Bus bus("Drums");
    CHECK(bus.getName() == "Drums");
}

TEST_CASE("Bus: handle defaults to -1")
{
    Bus bus("B");
    CHECK(bus.getHandle() == -1);
}

TEST_CASE("Bus: handle can be set and read")
{
    Bus bus("B");
    bus.setHandle(10);
    CHECK(bus.getHandle() == 10);
}

TEST_CASE("Bus: isMaster defaults to false")
{
    Bus bus("B");
    CHECK_FALSE(bus.isMaster());
}

TEST_CASE("Bus: isMaster true when constructed as master")
{
    Bus bus("Master", true);
    CHECK(bus.isMaster());
}

// ═══════════════════════════════════════════════════════════════════
// Lifecycle: prepare / release
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Bus: prepare forwards to chain")
{
    Bus bus("B");
    auto fx = std::make_unique<TrackingProcessor>("FX");
    auto* raw = fx.get();
    bus.getChain().append(std::move(fx));

    bus.prepare(48000.0, 256);

    CHECK(raw->prepareCount_ == 1);
    CHECK(raw->sr_ == 48000.0);
    CHECK(raw->bs_ == 256);
}

TEST_CASE("Bus: release forwards to chain")
{
    Bus bus("B");
    auto fx = std::make_unique<TrackingProcessor>("FX");
    auto* raw = fx.get();
    bus.getChain().append(std::move(fx));

    bus.prepare(44100.0, 512);
    bus.release();

    CHECK(raw->releaseCount_ == 1);
}

TEST_CASE("Bus: prepare on empty chain does not crash")
{
    Bus bus("B");
    bus.prepare(44100.0, 512);
}

TEST_CASE("Bus: release on empty chain does not crash")
{
    Bus bus("B");
    bus.release();
}

// ═══════════════════════════════════════════════════════════════════
// Chain
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Bus: chain is initially empty")
{
    Bus bus("B");
    CHECK(bus.getChain().size() == 0);
}

TEST_CASE("Bus: can append processors to chain")
{
    Bus bus("B");
    bus.getChain().append(std::make_unique<TrackingProcessor>());
    bus.getChain().append(std::make_unique<TrackingProcessor>());
    CHECK(bus.getChain().size() == 2);
}

TEST_CASE("Bus: const getChain returns same chain")
{
    Bus bus("B");
    bus.getChain().append(std::make_unique<TrackingProcessor>());

    const Bus& cref = bus;
    CHECK(cref.getChain().size() == 1);
}

// ═══════════════════════════════════════════════════════════════════
// Gain
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Bus: gain defaults to 1.0")
{
    Bus bus("B");
    CHECK(bus.getGain() == Approx(1.0f));
}

TEST_CASE("Bus: setGain and getGain roundtrip")
{
    Bus bus("B");
    bus.setGain(0.5f);
    CHECK(bus.getGain() == Approx(0.5f));
}

TEST_CASE("Bus: setGain clamps negative to 0.0")
{
    Bus bus("B");
    bus.setGain(-0.5f);
    CHECK(bus.getGain() == Approx(0.0f));
}

TEST_CASE("Bus: setGain allows zero")
{
    Bus bus("B");
    bus.setGain(0.0f);
    CHECK(bus.getGain() == Approx(0.0f));
}

TEST_CASE("Bus: setGain allows values above 1.0")
{
    Bus bus("B");
    bus.setGain(2.0f);
    CHECK(bus.getGain() == Approx(2.0f));
}

// ═══════════════════════════════════════════════════════════════════
// Pan
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Bus: pan defaults to 0.0 (center)")
{
    Bus bus("B");
    CHECK(bus.getPan() == Approx(0.0f));
}

TEST_CASE("Bus: setPan and getPan roundtrip")
{
    Bus bus("B");
    bus.setPan(-0.5f);
    CHECK(bus.getPan() == Approx(-0.5f));
}

TEST_CASE("Bus: setPan clamps below -1.0")
{
    Bus bus("B");
    bus.setPan(-2.0f);
    CHECK(bus.getPan() == Approx(-1.0f));
}

TEST_CASE("Bus: setPan clamps above 1.0")
{
    Bus bus("B");
    bus.setPan(3.0f);
    CHECK(bus.getPan() == Approx(1.0f));
}

TEST_CASE("Bus: setPan allows extremes")
{
    Bus bus("B");
    bus.setPan(-1.0f);
    CHECK(bus.getPan() == Approx(-1.0f));
    bus.setPan(1.0f);
    CHECK(bus.getPan() == Approx(1.0f));
}

// ═══════════════════════════════════════════════════════════════════
// Bus Routing
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Bus: outputBus defaults to nullptr")
{
    Bus bus("B");
    CHECK(bus.getOutputBus() == nullptr);
}

TEST_CASE("Bus: routeTo sets output bus")
{
    Bus bus("B");
    Bus master("Master", true);
    bus.routeTo(&master);
    CHECK(bus.getOutputBus() == &master);
}

TEST_CASE("Bus: routeTo nullptr is a no-op")
{
    Bus bus("B");
    Bus master("Master", true);
    bus.routeTo(&master);
    bus.routeTo(nullptr);
    CHECK(bus.getOutputBus() == &master);  // unchanged
}

TEST_CASE("Bus: routeTo changes output bus")
{
    Bus bus("B");
    Bus a("A"), b("B2");
    bus.routeTo(&a);
    CHECK(bus.getOutputBus() == &a);
    bus.routeTo(&b);
    CHECK(bus.getOutputBus() == &b);
}

TEST_CASE("Bus: Master bus routeTo is a no-op")
{
    Bus master("Master", true);
    Bus other("Other");
    master.routeTo(&other);
    CHECK(master.getOutputBus() == nullptr);  // unchanged
}

// ═══════════════════════════════════════════════════════════════════
// Sends
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Bus: no sends by default")
{
    Bus bus("B");
    CHECK(bus.getSends().empty());
}

TEST_CASE("Bus: addSend returns unique monotonic IDs")
{
    Bus bus("B");
    Bus dest1("D1"), dest2("D2");
    int id1 = bus.addSend(&dest1, -6.0f);
    int id2 = bus.addSend(&dest2, -3.0f);

    CHECK(id1 > 0);
    CHECK(id2 > id1);
}

TEST_CASE("Bus: addSend stores correct data")
{
    Bus bus("B");
    Bus dest("D");
    int id = bus.addSend(&dest, -6.0f, SendTap::preFader);

    auto sends = bus.getSends();
    REQUIRE(sends.size() == 1);
    CHECK(sends[0].bus == &dest);
    CHECK(sends[0].levelDb == Approx(-6.0f));
    CHECK(sends[0].tap == SendTap::preFader);
    CHECK(sends[0].id == id);
}

TEST_CASE("Bus: addSend defaults to postFader")
{
    Bus bus("B");
    Bus dest("D");
    bus.addSend(&dest, -6.0f);

    auto sends = bus.getSends();
    REQUIRE(sends.size() == 1);
    CHECK(sends[0].tap == SendTap::postFader);
}

TEST_CASE("Bus: addSend with nullptr bus returns -1")
{
    Bus bus("B");
    int id = bus.addSend(nullptr, -6.0f);
    CHECK(id == -1);
    CHECK(bus.getSends().empty());
}

TEST_CASE("Bus: removeSend removes by ID")
{
    Bus bus("B");
    Bus dest("D");
    int id1 = bus.addSend(&dest, -6.0f);
    int id2 = bus.addSend(&dest, -3.0f);

    CHECK(bus.removeSend(id1));
    auto sends = bus.getSends();
    REQUIRE(sends.size() == 1);
    CHECK(sends[0].id == id2);
}

TEST_CASE("Bus: removeSend with unknown ID returns false")
{
    Bus bus("B");
    CHECK_FALSE(bus.removeSend(999));
}

TEST_CASE("Bus: setSendLevel updates existing send")
{
    Bus bus("B");
    Bus dest("D");
    int id = bus.addSend(&dest, -6.0f);

    bus.setSendLevel(id, -12.0f);

    auto sends = bus.getSends();
    REQUIRE(sends.size() == 1);
    CHECK(sends[0].levelDb == Approx(-12.0f));
}

TEST_CASE("Bus: setSendLevel with unknown ID is a no-op")
{
    Bus bus("B");
    Bus dest("D");
    bus.addSend(&dest, -6.0f);
    bus.setSendLevel(999, -12.0f);

    auto sends = bus.getSends();
    CHECK(sends[0].levelDb == Approx(-6.0f));
}

TEST_CASE("Bus: setSendTap updates existing send")
{
    Bus bus("B");
    Bus dest("D");
    int id = bus.addSend(&dest, -6.0f, SendTap::postFader);

    bus.setSendTap(id, SendTap::preFader);

    auto sends = bus.getSends();
    REQUIRE(sends.size() == 1);
    CHECK(sends[0].tap == SendTap::preFader);
}

TEST_CASE("Bus: setSendTap with unknown ID is a no-op")
{
    Bus bus("B");
    Bus dest("D");
    bus.addSend(&dest, -6.0f, SendTap::postFader);
    bus.setSendTap(999, SendTap::preFader);

    auto sends = bus.getSends();
    CHECK(sends[0].tap == SendTap::postFader);
}

TEST_CASE("Bus: send IDs are never reused after removal")
{
    Bus bus("B");
    Bus dest("D");
    int id1 = bus.addSend(&dest, -6.0f);
    bus.removeSend(id1);
    int id2 = bus.addSend(&dest, -3.0f);

    CHECK(id2 > id1);
}

// ═══════════════════════════════════════════════════════════════════
// Bypass
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Bus: bypass defaults to false")
{
    Bus bus("B");
    CHECK_FALSE(bus.isBypassed());
}

TEST_CASE("Bus: setBypassed and isBypassed roundtrip")
{
    Bus bus("B");
    bus.setBypassed(true);
    CHECK(bus.isBypassed());
    bus.setBypassed(false);
    CHECK_FALSE(bus.isBypassed());
}

// ═══════════════════════════════════════════════════════════════════
// Metering
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Bus: metering defaults to 0.0")
{
    Bus bus("B");
    CHECK(bus.getPeak() == Approx(0.0f));
    CHECK(bus.getRMS() == Approx(0.0f));
}

TEST_CASE("Bus: updateMetering computes correct peak")
{
    Bus bus("B");
    juce::AudioBuffer<float> buffer(2, 4);
    buffer.clear();
    buffer.setSample(0, 0, 0.5f);
    buffer.setSample(0, 2, -0.8f);
    buffer.setSample(1, 1, 0.3f);

    bus.updateMetering(buffer, 4);

    CHECK(bus.getPeak() == Approx(0.8f));
}

TEST_CASE("Bus: updateMetering computes correct RMS")
{
    Bus bus("B");
    // Mono buffer, 4 samples of 0.5
    juce::AudioBuffer<float> buffer(1, 4);
    for (int i = 0; i < 4; ++i)
        buffer.setSample(0, i, 0.5f);

    bus.updateMetering(buffer, 4);

    // RMS of [0.5, 0.5, 0.5, 0.5] = sqrt(0.25) = 0.5
    CHECK(bus.getRMS() == Approx(0.5f));
}

TEST_CASE("Bus: updateMetering with stereo computes RMS across channels")
{
    Bus bus("B");
    juce::AudioBuffer<float> buffer(2, 4);
    // All samples = 1.0 across 2 channels
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            buffer.setSample(ch, i, 1.0f);

    bus.updateMetering(buffer, 4);

    // 8 total samples, all 1.0 => RMS = sqrt(8/8) = 1.0
    CHECK(bus.getPeak() == Approx(1.0f));
    CHECK(bus.getRMS() == Approx(1.0f));
}

TEST_CASE("Bus: updateMetering with silence")
{
    Bus bus("B");
    juce::AudioBuffer<float> buffer(2, 4);
    buffer.clear();

    bus.updateMetering(buffer, 4);

    CHECK(bus.getPeak() == Approx(0.0f));
    CHECK(bus.getRMS() == Approx(0.0f));
}

TEST_CASE("Bus: resetMetering clears peak and RMS")
{
    Bus bus("B");
    juce::AudioBuffer<float> buffer(1, 4);
    for (int i = 0; i < 4; ++i)
        buffer.setSample(0, i, 0.8f);

    bus.updateMetering(buffer, 4);
    CHECK(bus.getPeak() > 0.0f);

    bus.resetMetering();
    CHECK(bus.getPeak() == Approx(0.0f));
    CHECK(bus.getRMS() == Approx(0.0f));
}

TEST_CASE("Bus: updateMetering overwrites previous values")
{
    Bus bus("B");

    // First update with loud signal
    juce::AudioBuffer<float> buffer(1, 4);
    for (int i = 0; i < 4; ++i)
        buffer.setSample(0, i, 1.0f);
    bus.updateMetering(buffer, 4);
    CHECK(bus.getPeak() == Approx(1.0f));

    // Second update with quiet signal
    for (int i = 0; i < 4; ++i)
        buffer.setSample(0, i, 0.1f);
    bus.updateMetering(buffer, 4);
    CHECK(bus.getPeak() == Approx(0.1f));
}

// ═══════════════════════════════════════════════════════════════════
// Latency
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Bus: latency is chain latency")
{
    Bus bus("B");
    bus.getChain().append(std::make_unique<LatencyProcessor>(128));
    bus.getChain().append(std::make_unique<LatencyProcessor>(64));

    CHECK(bus.getLatencySamples() == 192);
}

TEST_CASE("Bus: latency with empty chain is 0")
{
    Bus bus("B");
    CHECK(bus.getLatencySamples() == 0);
}

TEST_CASE("Bus: latency updates after chain modification")
{
    Bus bus("B");
    bus.getChain().append(std::make_unique<LatencyProcessor>(100));
    CHECK(bus.getLatencySamples() == 100);

    bus.getChain().append(std::make_unique<LatencyProcessor>(50));
    CHECK(bus.getLatencySamples() == 150);

    bus.getChain().remove(0);
    CHECK(bus.getLatencySamples() == 50);
}

// ═══════════════════════════════════════════════════════════════════
// Combined / Integration
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Bus: full workflow — master bus with chain, sends, metering")
{
    Bus master("Master", true);
    master.prepare(44100.0, 512);

    // Chain
    master.getChain().append(std::make_unique<TrackingProcessor>("Limiter"));
    CHECK(master.getChain().size() == 1);

    // Gain and pan
    master.setGain(0.9f);
    master.setPan(0.0f);
    CHECK(master.getGain() == Approx(0.9f));

    // Master can't route
    Bus other("Other");
    master.routeTo(&other);
    CHECK(master.getOutputBus() == nullptr);

    // Master can have sends (e.g., recording bus)
    Bus recordBus("Record");
    int sendId = master.addSend(&recordBus, 0.0f, SendTap::postFader);
    CHECK(master.getSends().size() == 1);

    // Metering
    juce::AudioBuffer<float> buffer(2, 4);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            buffer.setSample(ch, i, 0.7f);
    master.updateMetering(buffer, 4);
    CHECK(master.getPeak() == Approx(0.7f));
    CHECK(master.getRMS() == Approx(0.7f));

    // Cleanup
    master.removeSend(sendId);
    CHECK(master.getSends().empty());
}

TEST_CASE("Bus: regular bus routes to master")
{
    Bus master("Master", true);
    Bus drumBus("Drums");

    drumBus.routeTo(&master);
    CHECK(drumBus.getOutputBus() == &master);

    drumBus.setGain(0.85f);
    drumBus.setPan(0.1f);
    CHECK(drumBus.getGain() == Approx(0.85f));
    CHECK(drumBus.getPan() == Approx(0.1f));

    // Sends from bus to bus
    Bus reverbBus("Reverb");
    reverbBus.routeTo(&master);
    int sendId = drumBus.addSend(&reverbBus, -6.0f);
    CHECK(sendId > 0);
    CHECK(drumBus.getSends().size() == 1);
}
