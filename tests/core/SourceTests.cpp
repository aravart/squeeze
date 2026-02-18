#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/Processor.h"
#include "core/Chain.h"
#include "core/Source.h"
#include "core/Bus.h"

using namespace squeeze;
using Catch::Approx;

namespace {

// --- Test helpers ---

class TestGenerator : public Processor {
public:
    explicit TestGenerator(const std::string& name = "TestGen", int latency = 0)
        : Processor(name), latency_(latency) {}

    void prepare(double sr, int bs) override { prepareCount_++; sampleRate_ = sr; blockSize_ = bs; }
    void release() override { releaseCount_++; }
    void reset() override { resetCount_++; }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        // Fill buffer with 1.0 to simulate audio generation
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample(ch, i, 1.0f);
    }

    void process(juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) override
    {
        midiEventCount_ = 0;
        for (const auto metadata : midi) {
            (void)metadata;
            midiEventCount_++;
        }
        process(buffer);
    }

    int getLatencySamples() const override { return latency_; }

    int prepareCount_ = 0;
    int releaseCount_ = 0;
    int resetCount_ = 0;
    double sampleRate_ = 0.0;
    int blockSize_ = 0;
    int midiEventCount_ = 0;

private:
    int latency_;
};

class ScaleProcessor : public Processor {
public:
    explicit ScaleProcessor(float factor)
        : Processor("Scale"), factor_(factor) {}

    void prepare(double, int) override {}
    void process(juce::AudioBuffer<float>& buffer) override
    {
        buffer.applyGain(factor_);
    }
    int getLatencySamples() const override { return 0; }

private:
    float factor_;
};

class LatencyProcessor : public Processor {
public:
    explicit LatencyProcessor(int latency)
        : Processor("Latency"), latency_(latency) {}

    void prepare(double, int) override {}
    void process(juce::AudioBuffer<float>&) override {}
    int getLatencySamples() const override { return latency_; }

private:
    int latency_;
};

static std::unique_ptr<TestGenerator> makeGen(const std::string& name = "TestGen", int latency = 0)
{
    return std::make_unique<TestGenerator>(name, latency);
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════
// Construction & Identity
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Source: stores name from construction")
{
    Source src("Vocal", makeGen());
    CHECK(src.getName() == "Vocal");
}

TEST_CASE("Source: handle defaults to -1")
{
    Source src("Vocal", makeGen());
    CHECK(src.getHandle() == -1);
}

TEST_CASE("Source: handle can be set and read")
{
    Source src("Vocal", makeGen());
    src.setHandle(42);
    CHECK(src.getHandle() == 42);
}

TEST_CASE("Source: generator is accessible after construction")
{
    auto gen = makeGen("MySynth");
    auto* rawGen = gen.get();
    Source src("Synth", std::move(gen));

    CHECK(src.getGenerator() == rawGen);
    CHECK(src.getGenerator()->getName() == "MySynth");
}

// ═══════════════════════════════════════════════════════════════════
// Lifecycle: prepare / release
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Source: prepare forwards to generator and chain")
{
    auto gen = makeGen();
    auto* rawGen = gen.get();
    Source src("S", std::move(gen));

    // Add a processor to chain so we can verify chain is prepared too
    auto fx = std::make_unique<TestGenerator>("FX");
    auto* rawFx = fx.get();
    src.getChain().append(std::move(fx));

    src.prepare(48000.0, 256);

    CHECK(rawGen->prepareCount_ == 1);
    CHECK(rawGen->sampleRate_ == 48000.0);
    CHECK(rawGen->blockSize_ == 256);
    CHECK(rawFx->prepareCount_ == 1);
    CHECK(rawFx->sampleRate_ == 48000.0);
}

TEST_CASE("Source: release forwards to generator and chain")
{
    auto gen = makeGen();
    auto* rawGen = gen.get();
    Source src("S", std::move(gen));

    auto fx = std::make_unique<TestGenerator>("FX");
    auto* rawFx = fx.get();
    src.getChain().append(std::move(fx));

    src.prepare(44100.0, 512);
    src.release();

    CHECK(rawGen->releaseCount_ == 1);
    CHECK(rawFx->releaseCount_ == 1);
}

// ═══════════════════════════════════════════════════════════════════
// Generator
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Source: setGenerator swaps generator")
{
    Source src("S", makeGen("Old"));
    CHECK(src.getGenerator()->getName() == "Old");

    src.setGenerator(makeGen("New"));
    CHECK(src.getGenerator()->getName() == "New");
}

TEST_CASE("Source: setGenerator preserves chain")
{
    Source src("S", makeGen());
    src.getChain().append(std::make_unique<ScaleProcessor>(0.5f));
    CHECK(src.getChain().size() == 1);

    src.setGenerator(makeGen("New"));
    CHECK(src.getChain().size() == 1);
    CHECK(src.getGenerator()->getName() == "New");
}

TEST_CASE("Source: setGenerator with nullptr is a no-op")
{
    Source src("S", makeGen("Original"));
    src.setGenerator(nullptr);
    CHECK(src.getGenerator()->getName() == "Original");
}

TEST_CASE("Source: setGenerator prepares new generator if source is prepared")
{
    Source src("S", makeGen());
    src.prepare(44100.0, 512);

    auto gen = makeGen("New");
    auto* rawGen = gen.get();
    src.setGenerator(std::move(gen));

    CHECK(rawGen->prepareCount_ == 1);
    CHECK(rawGen->sampleRate_ == 44100.0);
    CHECK(rawGen->blockSize_ == 512);
}

// ═══════════════════════════════════════════════════════════════════
// Chain
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Source: chain is initially empty")
{
    Source src("S", makeGen());
    CHECK(src.getChain().size() == 0);
}

TEST_CASE("Source: can append processors to chain")
{
    Source src("S", makeGen());
    src.getChain().append(std::make_unique<ScaleProcessor>(0.5f));
    src.getChain().append(std::make_unique<ScaleProcessor>(0.25f));
    CHECK(src.getChain().size() == 2);
}

TEST_CASE("Source: const getChain returns same chain")
{
    Source src("S", makeGen());
    src.getChain().append(std::make_unique<ScaleProcessor>(0.5f));

    const Source& cref = src;
    CHECK(cref.getChain().size() == 1);
}

// ═══════════════════════════════════════════════════════════════════
// Gain
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Source: gain defaults to 1.0")
{
    Source src("S", makeGen());
    CHECK(src.getGain() == Approx(1.0f));
}

TEST_CASE("Source: setGain and getGain roundtrip")
{
    Source src("S", makeGen());
    src.setGain(0.5f);
    CHECK(src.getGain() == Approx(0.5f));
}

TEST_CASE("Source: setGain clamps negative to 0.0")
{
    Source src("S", makeGen());
    src.setGain(-0.5f);
    CHECK(src.getGain() == Approx(0.0f));
}

TEST_CASE("Source: setGain allows zero")
{
    Source src("S", makeGen());
    src.setGain(0.0f);
    CHECK(src.getGain() == Approx(0.0f));
}

TEST_CASE("Source: setGain allows values above 1.0")
{
    Source src("S", makeGen());
    src.setGain(2.0f);
    CHECK(src.getGain() == Approx(2.0f));
}

// ═══════════════════════════════════════════════════════════════════
// Pan
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Source: pan defaults to 0.0 (center)")
{
    Source src("S", makeGen());
    CHECK(src.getPan() == Approx(0.0f));
}

TEST_CASE("Source: setPan and getPan roundtrip")
{
    Source src("S", makeGen());
    src.setPan(-0.5f);
    CHECK(src.getPan() == Approx(-0.5f));
}

TEST_CASE("Source: setPan clamps below -1.0")
{
    Source src("S", makeGen());
    src.setPan(-2.0f);
    CHECK(src.getPan() == Approx(-1.0f));
}

TEST_CASE("Source: setPan clamps above 1.0")
{
    Source src("S", makeGen());
    src.setPan(3.0f);
    CHECK(src.getPan() == Approx(1.0f));
}

TEST_CASE("Source: setPan allows extremes")
{
    Source src("S", makeGen());
    src.setPan(-1.0f);
    CHECK(src.getPan() == Approx(-1.0f));
    src.setPan(1.0f);
    CHECK(src.getPan() == Approx(1.0f));
}

// ═══════════════════════════════════════════════════════════════════
// Bus Routing
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Source: outputBus defaults to nullptr")
{
    Source src("S", makeGen());
    CHECK(src.getOutputBus() == nullptr);
}

TEST_CASE("Source: routeTo sets output bus")
{
    Source src("S", makeGen());
    Bus dummyBus("dummy");
    src.routeTo(&dummyBus);
    CHECK(src.getOutputBus() == &dummyBus);
}

TEST_CASE("Source: routeTo nullptr is a no-op")
{
    Source src("S", makeGen());
    Bus dummyBus("dummy");
    src.routeTo(&dummyBus);
    src.routeTo(nullptr);
    CHECK(src.getOutputBus() == &dummyBus);  // unchanged
}

TEST_CASE("Source: routeTo changes output bus")
{
    Source src("S", makeGen());
    Bus bus1("b1"), bus2("b2");
    src.routeTo(&bus1);
    CHECK(src.getOutputBus() == &bus1);
    src.routeTo(&bus2);
    CHECK(src.getOutputBus() == &bus2);
}

// ═══════════════════════════════════════════════════════════════════
// Sends
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Source: no sends by default")
{
    Source src("S", makeGen());
    CHECK(src.getSends().empty());
}

TEST_CASE("Source: addSend returns unique monotonic IDs")
{
    Source src("S", makeGen());
    Bus bus1("b1"), bus2("b2");
    int id1 = src.addSend(&bus1, -6.0f);
    int id2 = src.addSend(&bus2, -3.0f);

    CHECK(id1 > 0);
    CHECK(id2 > id1);
}

TEST_CASE("Source: addSend stores correct data")
{
    Source src("S", makeGen());
    Bus bus("b");
    int id = src.addSend(&bus, -6.0f, SendTap::preFader);

    auto sends = src.getSends();
    REQUIRE(sends.size() == 1);
    CHECK(sends[0].bus == &bus);
    CHECK(sends[0].levelDb == Approx(-6.0f));
    CHECK(sends[0].tap == SendTap::preFader);
    CHECK(sends[0].id == id);
}

TEST_CASE("Source: addSend defaults to postFader")
{
    Source src("S", makeGen());
    Bus bus("b");
    src.addSend(&bus, -6.0f);

    auto sends = src.getSends();
    REQUIRE(sends.size() == 1);
    CHECK(sends[0].tap == SendTap::postFader);
}

TEST_CASE("Source: addSend with nullptr bus returns -1")
{
    Source src("S", makeGen());
    int id = src.addSend(nullptr, -6.0f);
    CHECK(id == -1);
    CHECK(src.getSends().empty());
}

TEST_CASE("Source: removeSend removes by ID")
{
    Source src("S", makeGen());
    Bus bus("b");
    int id1 = src.addSend(&bus, -6.0f);
    int id2 = src.addSend(&bus, -3.0f);

    CHECK(src.removeSend(id1));
    auto sends = src.getSends();
    REQUIRE(sends.size() == 1);
    CHECK(sends[0].id == id2);
}

TEST_CASE("Source: removeSend with unknown ID returns false")
{
    Source src("S", makeGen());
    CHECK_FALSE(src.removeSend(999));
}

TEST_CASE("Source: setSendLevel updates existing send")
{
    Source src("S", makeGen());
    Bus bus("b");
    int id = src.addSend(&bus, -6.0f);

    src.setSendLevel(id, -12.0f);

    auto sends = src.getSends();
    REQUIRE(sends.size() == 1);
    CHECK(sends[0].levelDb == Approx(-12.0f));
}

TEST_CASE("Source: setSendLevel with unknown ID is a no-op")
{
    Source src("S", makeGen());
    Bus bus("b");
    int id = src.addSend(&bus, -6.0f);
    src.setSendLevel(999, -12.0f);

    auto sends = src.getSends();
    CHECK(sends[0].levelDb == Approx(-6.0f));  // unchanged
    (void)id;
}

TEST_CASE("Source: setSendTap updates existing send")
{
    Source src("S", makeGen());
    Bus bus("b");
    int id = src.addSend(&bus, -6.0f, SendTap::postFader);

    src.setSendTap(id, SendTap::preFader);

    auto sends = src.getSends();
    REQUIRE(sends.size() == 1);
    CHECK(sends[0].tap == SendTap::preFader);
}

TEST_CASE("Source: setSendTap with unknown ID is a no-op")
{
    Source src("S", makeGen());
    Bus bus("b");
    src.addSend(&bus, -6.0f, SendTap::postFader);
    src.setSendTap(999, SendTap::preFader);

    auto sends = src.getSends();
    CHECK(sends[0].tap == SendTap::postFader);  // unchanged
}

TEST_CASE("Source: send IDs are never reused after removal")
{
    Source src("S", makeGen());
    Bus bus("b");
    int id1 = src.addSend(&bus, -6.0f);
    src.removeSend(id1);
    int id2 = src.addSend(&bus, -3.0f);

    CHECK(id2 > id1);
}

// ═══════════════════════════════════════════════════════════════════
// MIDI Assignment
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Source: MIDI assignment defaults to none")
{
    Source src("S", makeGen());
    auto midi = src.getMidiAssignment();
    CHECK(midi.channel == -1);  // none: channel -1
}

TEST_CASE("Source: setMidiAssignment and getMidiAssignment roundtrip")
{
    Source src("S", makeGen());
    MidiAssignment assignment{"Keylab", 1, 0, 127};
    src.setMidiAssignment(assignment);

    auto result = src.getMidiAssignment();
    CHECK(result.device == "Keylab");
    CHECK(result.channel == 1);
    CHECK(result.noteLow == 0);
    CHECK(result.noteHigh == 127);
}

TEST_CASE("Source: MidiAssignment::all returns catch-all")
{
    auto all = MidiAssignment::all();
    CHECK(all.device == "");
    CHECK(all.channel == 0);
    CHECK(all.noteLow == 0);
    CHECK(all.noteHigh == 127);
}

TEST_CASE("Source: MidiAssignment::none returns disabled")
{
    auto none = MidiAssignment::none();
    CHECK(none.channel == -1);
}

// ═══════════════════════════════════════════════════════════════════
// Bypass
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Source: bypass defaults to false")
{
    Source src("S", makeGen());
    CHECK_FALSE(src.isBypassed());
}

TEST_CASE("Source: setBypassed and isBypassed roundtrip")
{
    Source src("S", makeGen());
    src.setBypassed(true);
    CHECK(src.isBypassed());
    src.setBypassed(false);
    CHECK_FALSE(src.isBypassed());
}

// ═══════════════════════════════════════════════════════════════════
// Processing
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Source: process runs generator (fills buffer)")
{
    Source src("S", makeGen());
    src.prepare(44100.0, 4);

    juce::AudioBuffer<float> buffer(2, 4);
    buffer.clear();
    juce::MidiBuffer midi;

    src.process(buffer, midi);

    // TestGenerator fills with 1.0
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            CHECK(buffer.getSample(ch, i) == Approx(1.0f));
}

TEST_CASE("Source: process runs generator then chain in order")
{
    Source src("S", makeGen());
    // Chain scales by 0.5 — so result should be 1.0 * 0.5 = 0.5
    src.getChain().append(std::make_unique<ScaleProcessor>(0.5f));
    src.prepare(44100.0, 4);

    juce::AudioBuffer<float> buffer(2, 4);
    buffer.clear();
    juce::MidiBuffer midi;

    src.process(buffer, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            CHECK(buffer.getSample(ch, i) == Approx(0.5f));
}

TEST_CASE("Source: process runs chain processors sequentially")
{
    Source src("S", makeGen());
    // Generator fills with 1.0, then scale by 0.5, then scale by 0.5 again => 0.25
    src.getChain().append(std::make_unique<ScaleProcessor>(0.5f));
    src.getChain().append(std::make_unique<ScaleProcessor>(0.5f));
    src.prepare(44100.0, 4);

    juce::AudioBuffer<float> buffer(2, 4);
    buffer.clear();
    juce::MidiBuffer midi;

    src.process(buffer, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            CHECK(buffer.getSample(ch, i) == Approx(0.25f));
}

TEST_CASE("Source: process forwards MIDI to generator")
{
    auto gen = makeGen();
    auto* rawGen = gen.get();
    Source src("S", std::move(gen));
    src.prepare(44100.0, 4);

    juce::AudioBuffer<float> buffer(2, 4);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 2);

    src.process(buffer, midi);
    CHECK(rawGen->midiEventCount_ == 2);
}

// ═══════════════════════════════════════════════════════════════════
// Latency
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Source: latency is generator latency + chain latency")
{
    Source src("S", makeGen("Gen", 64));
    src.getChain().append(std::make_unique<LatencyProcessor>(128));
    src.getChain().append(std::make_unique<LatencyProcessor>(32));

    CHECK(src.getLatencySamples() == 224);  // 64 + 128 + 32
}

TEST_CASE("Source: latency with zero-latency generator and empty chain")
{
    Source src("S", makeGen("Gen", 0));
    CHECK(src.getLatencySamples() == 0);
}

TEST_CASE("Source: latency updates after chain modification")
{
    Source src("S", makeGen("Gen", 64));
    CHECK(src.getLatencySamples() == 64);

    src.getChain().append(std::make_unique<LatencyProcessor>(100));
    CHECK(src.getLatencySamples() == 164);

    src.getChain().remove(0);
    CHECK(src.getLatencySamples() == 64);
}

TEST_CASE("Source: latency updates after generator swap")
{
    Source src("S", makeGen("Gen", 64));
    src.getChain().append(std::make_unique<LatencyProcessor>(100));
    CHECK(src.getLatencySamples() == 164);

    src.setGenerator(makeGen("NewGen", 256));
    CHECK(src.getLatencySamples() == 356);  // 256 + 100
}

// ═══════════════════════════════════════════════════════════════════
// Combined / Integration
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Source: full channel strip workflow")
{
    // Build a source with generator, chain, routing, sends, MIDI
    Source src("Vocal", makeGen("Synth", 32));
    src.setHandle(1);
    src.prepare(44100.0, 512);

    // Chain
    src.getChain().append(std::make_unique<ScaleProcessor>(0.5f));
    CHECK(src.getChain().size() == 1);

    // Gain and pan
    src.setGain(0.75f);
    src.setPan(-0.3f);
    CHECK(src.getGain() == Approx(0.75f));
    CHECK(src.getPan() == Approx(-0.3f));

    // Bus routing
    Bus masterBus("master");
    src.routeTo(&masterBus);
    CHECK(src.getOutputBus() == &masterBus);

    // Sends
    Bus reverbBus("reverb"), monitorBus("monitor");
    int sendReverb = src.addSend(&reverbBus, -6.0f);
    int sendMonitor = src.addSend(&monitorBus, 0.0f, SendTap::preFader);
    CHECK(src.getSends().size() == 2);

    // MIDI
    src.setMidiAssignment({"Keylab", 1, 0, 127});
    CHECK(src.getMidiAssignment().device == "Keylab");

    // Process
    juce::AudioBuffer<float> buffer(2, 4);
    buffer.clear();
    juce::MidiBuffer midi;
    src.process(buffer, midi);

    // Generator fills 1.0, chain scales by 0.5 => 0.5
    CHECK(buffer.getSample(0, 0) == Approx(0.5f));

    // Latency
    CHECK(src.getLatencySamples() == 32);  // generator only, chain scale has 0

    // Cleanup
    src.removeSend(sendReverb);
    src.removeSend(sendMonitor);
    CHECK(src.getSends().empty());
}
