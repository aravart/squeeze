#include <catch2/catch_test_macros.hpp>
#include "core/Engine.h"
#include "core/GainProcessor.h"

#include <cmath>
#include <memory>

using namespace squeeze;

// ═══════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Engine creates Master bus at construction")
{
    Engine engine(44100.0, 512);
    Bus* master = engine.getMaster();
    REQUIRE(master != nullptr);
    CHECK(master->isMaster());
    CHECK(master->getName() == "Master");
    CHECK(master->getHandle() > 0);
}

TEST_CASE("Engine getVersion returns 0.3.0")
{
    Engine engine(44100.0, 512);
    CHECK(engine.getVersion() == "0.3.0");
}

TEST_CASE("Engine getSampleRate and getBlockSize return constructor values")
{
    Engine engine(48000.0, 256);
    CHECK(engine.getSampleRate() == 48000.0);
    CHECK(engine.getBlockSize() == 256);
}

// ═══════════════════════════════════════════════════════════════════
// Source management
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("addSource creates source with unique handle")
{
    Engine engine(44100.0, 512);
    auto* s1 = engine.addSource("src1", std::make_unique<GainProcessor>());
    auto* s2 = engine.addSource("src2", std::make_unique<GainProcessor>());
    REQUIRE(s1 != nullptr);
    REQUIRE(s2 != nullptr);
    CHECK(s1->getHandle() > 0);
    CHECK(s2->getHandle() > 0);
    CHECK(s1->getHandle() != s2->getHandle());
}

TEST_CASE("addSource with null generator returns nullptr")
{
    Engine engine(44100.0, 512);
    auto* s = engine.addSource("bad", nullptr);
    REQUIRE(s == nullptr);
    CHECK(engine.getSourceCount() == 0);
}

TEST_CASE("addSource defaults routing to Master")
{
    Engine engine(44100.0, 512);
    auto* s = engine.addSource("src", std::make_unique<GainProcessor>());
    REQUIRE(s != nullptr);
    CHECK(s->getOutputBus() == engine.getMaster());
}

TEST_CASE("removeSource removes the source")
{
    Engine engine(44100.0, 512);
    auto* s = engine.addSource("src", std::make_unique<GainProcessor>());
    REQUIRE(engine.getSourceCount() == 1);
    REQUIRE(engine.removeSource(s));
    CHECK(engine.getSourceCount() == 0);
}

TEST_CASE("removeSource returns false for unknown source")
{
    Engine engine(44100.0, 512);
    Source fakeSource("fake", std::make_unique<GainProcessor>());
    CHECK_FALSE(engine.removeSource(&fakeSource));
}

TEST_CASE("getSource returns source by handle")
{
    Engine engine(44100.0, 512);
    auto* s = engine.addSource("src", std::make_unique<GainProcessor>());
    REQUIRE(s != nullptr);
    CHECK(engine.getSource(s->getHandle()) == s);
    CHECK(engine.getSource(9999) == nullptr);
}

TEST_CASE("getSources returns all sources")
{
    Engine engine(44100.0, 512);
    engine.addSource("a", std::make_unique<GainProcessor>());
    engine.addSource("b", std::make_unique<GainProcessor>());
    auto sources = engine.getSources();
    CHECK(sources.size() == 2);
}

// ═══════════════════════════════════════════════════════════════════
// Bus management
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("addBus creates bus with unique handle, routing to Master")
{
    Engine engine(44100.0, 512);
    auto* bus = engine.addBus("FX");
    REQUIRE(bus != nullptr);
    CHECK(bus->getHandle() > 0);
    CHECK(bus->getHandle() != engine.getMaster()->getHandle());
    CHECK(bus->getOutputBus() == engine.getMaster());
}

TEST_CASE("removeBus removes non-master bus")
{
    Engine engine(44100.0, 512);
    auto* bus = engine.addBus("FX");
    REQUIRE(engine.getBusCount() == 2); // Master + FX
    REQUIRE(engine.removeBus(bus));
    CHECK(engine.getBusCount() == 1);
}

TEST_CASE("removeBus returns false for Master")
{
    Engine engine(44100.0, 512);
    CHECK_FALSE(engine.removeBus(engine.getMaster()));
    CHECK(engine.getBusCount() == 1);
}

TEST_CASE("getBus returns bus by handle")
{
    Engine engine(44100.0, 512);
    auto* bus = engine.addBus("FX");
    CHECK(engine.getBus(bus->getHandle()) == bus);
    CHECK(engine.getBus(9999) == nullptr);
}

TEST_CASE("getMaster returns the Master bus")
{
    Engine engine(44100.0, 512);
    auto* m = engine.getMaster();
    REQUIRE(m != nullptr);
    CHECK(m->isMaster());
}

// ═══════════════════════════════════════════════════════════════════
// Routing
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("route changes source output bus")
{
    Engine engine(44100.0, 512);
    auto* src = engine.addSource("src", std::make_unique<GainProcessor>());
    auto* bus = engine.addBus("FX");
    engine.route(src, bus);
    CHECK(src->getOutputBus() == bus);
}

TEST_CASE("busRoute routes a bus to another bus")
{
    Engine engine(44100.0, 512);
    auto* bus1 = engine.addBus("A");
    auto* bus2 = engine.addBus("B");
    CHECK(engine.busRoute(bus1, bus2));
    CHECK(bus1->getOutputBus() == bus2);
}

TEST_CASE("busRoute rejects cycle")
{
    Engine engine(44100.0, 512);
    auto* busA = engine.addBus("A");
    auto* busB = engine.addBus("B");
    REQUIRE(engine.busRoute(busA, busB));

    // B->A would create cycle A->B->A
    CHECK_FALSE(engine.busRoute(busB, busA));
}

TEST_CASE("busRoute rejects self-loop")
{
    Engine engine(44100.0, 512);
    auto* bus = engine.addBus("A");
    CHECK_FALSE(engine.busRoute(bus, bus));
}

// ═══════════════════════════════════════════════════════════════════
// Sends
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sendFrom adds a send from source to bus")
{
    Engine engine(44100.0, 512);
    auto* src = engine.addSource("src", std::make_unique<GainProcessor>());
    auto* bus = engine.addBus("FX");
    int sendId = engine.sendFrom(src, bus, -6.0f);
    REQUIRE(sendId > 0);
    auto sends = src->getSends();
    REQUIRE(sends.size() == 1);
    CHECK(sends[0].bus == bus);
}

TEST_CASE("removeSend removes a send")
{
    Engine engine(44100.0, 512);
    auto* src = engine.addSource("src", std::make_unique<GainProcessor>());
    auto* bus = engine.addBus("FX");
    int sendId = engine.sendFrom(src, bus, -6.0f);
    engine.removeSend(src, sendId);
    CHECK(src->getSends().empty());
}

TEST_CASE("setSendLevel updates send level")
{
    Engine engine(44100.0, 512);
    auto* src = engine.addSource("src", std::make_unique<GainProcessor>());
    auto* bus = engine.addBus("FX");
    int sendId = engine.sendFrom(src, bus, -6.0f);
    engine.setSendLevel(src, sendId, -3.0f);
    CHECK(src->getSends()[0].levelDb == -3.0f);
}

TEST_CASE("busSend adds a send between buses")
{
    Engine engine(44100.0, 512);
    auto* busA = engine.addBus("A");
    auto* busB = engine.addBus("B");
    int sendId = engine.busSend(busA, busB, -6.0f);
    REQUIRE(sendId > 0);
    CHECK(busA->getSends().size() == 1);
}

TEST_CASE("busSend rejects cycle via send")
{
    Engine engine(44100.0, 512);
    auto* busA = engine.addBus("A");
    auto* busB = engine.addBus("B");
    REQUIRE(engine.busRoute(busA, busB));
    // B send to A would create cycle
    CHECK(engine.busSend(busB, busA, -6.0f) == -1);
}

// ═══════════════════════════════════════════════════════════════════
// Insert chains
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("sourceAppend adds processor to source chain")
{
    Engine engine(44100.0, 512);
    auto* src = engine.addSource("src", std::make_unique<GainProcessor>());
    auto* proc = engine.sourceAppend(src, std::make_unique<GainProcessor>());
    REQUIRE(proc != nullptr);
    CHECK(proc->getHandle() > 0);
    CHECK(engine.sourceChainSize(src) == 1);
}

TEST_CASE("sourceInsert inserts at index")
{
    Engine engine(44100.0, 512);
    auto* src = engine.addSource("src", std::make_unique<GainProcessor>());
    engine.sourceAppend(src, std::make_unique<GainProcessor>());
    auto* p2 = engine.sourceInsert(src, 0, std::make_unique<GainProcessor>());
    CHECK(engine.sourceChainSize(src) == 2);
    CHECK(src->getChain().at(0) == p2);
}

TEST_CASE("sourceRemove removes processor from chain")
{
    Engine engine(44100.0, 512);
    auto* src = engine.addSource("src", std::make_unique<GainProcessor>());
    engine.sourceAppend(src, std::make_unique<GainProcessor>());
    engine.sourceRemove(src, 0);
    CHECK(engine.sourceChainSize(src) == 0);
}

TEST_CASE("busAppend adds processor to bus chain")
{
    Engine engine(44100.0, 512);
    auto* bus = engine.addBus("FX");
    auto* proc = engine.busAppend(bus, std::make_unique<GainProcessor>());
    REQUIRE(proc != nullptr);
    CHECK(engine.busChainSize(bus) == 1);
}

TEST_CASE("busInsert inserts at index")
{
    Engine engine(44100.0, 512);
    auto* bus = engine.addBus("FX");
    engine.busAppend(bus, std::make_unique<GainProcessor>());
    auto* p2 = engine.busInsert(bus, 0, std::make_unique<GainProcessor>());
    CHECK(engine.busChainSize(bus) == 2);
    CHECK(bus->getChain().at(0) == p2);
}

TEST_CASE("busRemove removes processor from chain")
{
    Engine engine(44100.0, 512);
    auto* bus = engine.addBus("FX");
    engine.busAppend(bus, std::make_unique<GainProcessor>());
    engine.busRemove(bus, 0);
    CHECK(engine.busChainSize(bus) == 0);
}

// ═══════════════════════════════════════════════════════════════════
// Parameters via processor handle
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("getParameter/setParameter work via proc handle")
{
    Engine engine(44100.0, 512);
    auto* src = engine.addSource("src", std::make_unique<GainProcessor>());
    int genHandle = src->getGenerator()->getHandle();

    CHECK(engine.getParameter(genHandle, "gain") == 1.0f);
    REQUIRE(engine.setParameter(genHandle, "gain", 0.5f));
    CHECK(engine.getParameter(genHandle, "gain") == 0.5f);
}

TEST_CASE("setParameter returns false for unknown handle")
{
    Engine engine(44100.0, 512);
    CHECK_FALSE(engine.setParameter(9999, "gain", 0.5f));
}

TEST_CASE("getParameterDescriptors works via proc handle")
{
    Engine engine(44100.0, 512);
    auto* src = engine.addSource("src", std::make_unique<GainProcessor>());
    int genHandle = src->getGenerator()->getHandle();
    auto descs = engine.getParameterDescriptors(genHandle);
    REQUIRE(descs.size() == 1);
    CHECK(descs[0].name == "gain");
}

TEST_CASE("getProcessor returns processor by handle")
{
    Engine engine(44100.0, 512);
    auto* src = engine.addSource("src", std::make_unique<GainProcessor>());
    int genHandle = src->getGenerator()->getHandle();
    CHECK(engine.getProcessor(genHandle) == src->getGenerator());
    CHECK(engine.getProcessor(9999) == nullptr);
}

// ═══════════════════════════════════════════════════════════════════
// Handle uniqueness
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Processor handles are globally unique and monotonically increasing")
{
    Engine engine(44100.0, 512);
    auto* src1 = engine.addSource("a", std::make_unique<GainProcessor>());
    auto* src2 = engine.addSource("b", std::make_unique<GainProcessor>());
    auto* proc1 = engine.sourceAppend(src1, std::make_unique<GainProcessor>());
    auto* proc2 = engine.busAppend(engine.getMaster(), std::make_unique<GainProcessor>());

    int h1 = src1->getGenerator()->getHandle();
    int h2 = src2->getGenerator()->getHandle();
    int h3 = proc1->getHandle();
    int h4 = proc2->getHandle();

    // All unique
    CHECK(h1 != h2);
    CHECK(h1 != h3);
    CHECK(h1 != h4);
    CHECK(h2 != h3);
    CHECK(h2 != h4);
    CHECK(h3 != h4);

    // Monotonically increasing
    CHECK(h1 < h2);
    CHECK(h2 < h3);
    CHECK(h3 < h4);
}

// ═══════════════════════════════════════════════════════════════════
// processBlock
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("render does not crash")
{
    Engine engine(44100.0, 512);
    engine.render(512);
}

TEST_CASE("processBlock outputs silence with no sources")
{
    Engine engine(44100.0, 512);
    engine.render(512); // drain snapshot

    const int N = 512;
    float left[N], right[N];
    for (int i = 0; i < N; ++i) { left[i] = 1.0f; right[i] = 1.0f; }
    float* channels[2] = {left, right};
    engine.processBlock(channels, 2, N);

    for (int i = 0; i < N; ++i)
    {
        CHECK(left[i] == 0.0f);
        CHECK(right[i] == 0.0f);
    }
}

TEST_CASE("Source with ConstGenerator generates audio at Master")
{
    Engine engine(44100.0, 512);
    engine.addSource("synth", std::make_unique<ConstGenerator>(0.5f));
    engine.render(512); // drain snapshot

    const int N = 128;
    float left[N] = {};
    float right[N] = {};
    float* channels[2] = {left, right};
    engine.processBlock(channels, 2, N);

    // TestSynth writes 0.5f, gain=1.0, pan=0.0 → output should be 0.5
    for (int i = 0; i < N; ++i)
    {
        CHECK(left[i] != 0.0f);
        CHECK(right[i] != 0.0f);
    }
}

TEST_CASE("GainProcessor in chain attenuates signal")
{
    Engine engine(44100.0, 512);
    auto* src = engine.addSource("synth", std::make_unique<ConstGenerator>(1.0f));
    auto* gain = engine.sourceAppend(src, std::make_unique<GainProcessor>());
    engine.setParameter(gain->getHandle(), "gain", 0.5f);
    engine.render(512); // drain snapshot

    const int N = 128;
    float left[N] = {};
    float right[N] = {};
    float* channels[2] = {left, right};
    engine.processBlock(channels, 2, N);

    // TestSynth writes 1.0f, gain proc halves it → ~0.5
    for (int i = 0; i < N; ++i)
    {
        CHECK(std::fabs(left[i] - 0.5f) < 0.01f);
    }
}

TEST_CASE("Send copies signal to another bus")
{
    Engine engine(44100.0, 512);
    auto* src = engine.addSource("synth", std::make_unique<ConstGenerator>(1.0f));
    auto* fxBus = engine.addBus("FX");

    // Send at 0dB (unity)
    engine.sendFrom(src, fxBus, 0.0f);
    engine.render(512); // drain snapshot

    const int N = 128;
    float left[N] = {};
    float right[N] = {};
    float* channels[2] = {left, right};
    engine.processBlock(channels, 2, N);

    // FX bus receives send signal, routes to Master → Master gets source + send
    float peak = engine.busPeak(fxBus);
    CHECK(peak > 0.0f);
}

TEST_CASE("Bus chain processes bus audio")
{
    Engine engine(44100.0, 512);
    engine.addSource("synth", std::make_unique<ConstGenerator>(1.0f));

    // Add a gain processor to Master bus chain that halves the signal
    auto* gain = engine.busAppend(engine.getMaster(), std::make_unique<GainProcessor>());
    engine.setParameter(gain->getHandle(), "gain", 0.5f);
    engine.render(512); // drain snapshot

    const int N = 128;
    float left[N] = {};
    float right[N] = {};
    float* channels[2] = {left, right};
    engine.processBlock(channels, 2, N);

    for (int i = 0; i < N; ++i)
    {
        CHECK(std::fabs(left[i] - 0.5f) < 0.01f);
    }
}

TEST_CASE("Metering updates after processBlock")
{
    Engine engine(44100.0, 512);
    engine.addSource("synth", std::make_unique<ConstGenerator>(0.5f));
    engine.render(512);

    const int N = 128;
    float left[N] = {};
    float right[N] = {};
    float* channels[2] = {left, right};
    engine.processBlock(channels, 2, N);

    float peak = engine.busPeak(engine.getMaster());
    float rms = engine.busRMS(engine.getMaster());
    CHECK(peak > 0.0f);
    CHECK(rms > 0.0f);
}

// ═══════════════════════════════════════════════════════════════════
// Batching
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("batchBegin/batchCommit defers snapshot rebuild")
{
    Engine engine(44100.0, 512);

    engine.batchBegin();
    engine.addSource("a", std::make_unique<GainProcessor>());
    engine.addSource("b", std::make_unique<GainProcessor>());
    engine.addSource("c", std::make_unique<GainProcessor>());
    // No crash, sources added
    CHECK(engine.getSourceCount() == 3);
    engine.batchCommit();

    // After commit, render should work fine
    engine.render(512);
}

// ═══════════════════════════════════════════════════════════════════
// Transport stubs
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Transport query stubs return defaults")
{
    Engine engine(44100.0, 512);
    CHECK(engine.getTransportPosition() == 0.0);
    CHECK(engine.getTransportTempo() == 120.0);
    CHECK_FALSE(engine.isTransportPlaying());
}

TEST_CASE("Transport commands do not crash")
{
    Engine engine(44100.0, 512);
    engine.transportPlay();
    engine.transportStop();
    engine.transportPause();
    engine.transportSetTempo(140.0);
    engine.transportSetTimeSignature(3, 4);
    engine.transportSeekSamples(0);
    engine.transportSeekBeats(0.0);
    engine.transportSetLoopPoints(0.0, 4.0);
    engine.transportSetLooping(true);
    engine.render(512); // drain commands
}

// ═══════════════════════════════════════════════════════════════════
// Event scheduling
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Event scheduling functions return true")
{
    Engine engine(44100.0, 512);
    CHECK(engine.scheduleNoteOn(1, 0.0, 1, 60, 0.8f));
    CHECK(engine.scheduleNoteOff(1, 1.0, 1, 60));
    CHECK(engine.scheduleCC(1, 0.0, 1, 1, 64));
    CHECK(engine.schedulePitchBend(1, 0.0, 1, 8192));
    CHECK(engine.scheduleParamChange(1, 0.0, "gain", 0.5f));
}
