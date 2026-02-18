#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/Processor.h"
#include "core/GainProcessor.h"

using namespace squeeze;

// --- Local test helper: TestSynthProcessor (overrides MIDI variant) ---

class TestSynthProcessor : public Processor {
public:
    TestSynthProcessor() : Processor("TestSynth") {}

    void prepare(double /*sampleRate*/, int /*blockSize*/) override {}

    void process(juce::AudioBuffer<float>& buffer) override
    {
        buffer.clear();
    }

    void process(juce::AudioBuffer<float>& buffer,
                 const juce::MidiBuffer& midi) override
    {
        buffer.clear();
        midiEventCount_ = 0;
        for (const auto metadata : midi)
        {
            (void)metadata;
            midiEventCount_++;
        }
    }

    int getMidiEventCount() const { return midiEventCount_; }

private:
    int midiEventCount_ = 0;
};

// --- Local test helper: StatefulProcessor (has internal state that reset() clears) ---

class StatefulProcessor : public Processor {
public:
    StatefulProcessor() : Processor("Stateful") {}

    void prepare(double /*sampleRate*/, int /*blockSize*/) override { runningSum_ = 0.0f; }

    void reset() override { runningSum_ = 0.0f; }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                runningSum_ += buffer.getSample(ch, i);
    }

    float getRunningSum() const { return runningSum_; }

private:
    float runningSum_ = 0.0f;
};

// --- Local test helper: TestLatencyProcessor ---

class TestLatencyProcessor : public Processor {
public:
    TestLatencyProcessor(int latency)
        : Processor("TestLatency"), latency_(latency) {}

    void prepare(double /*sampleRate*/, int /*blockSize*/) override {}

    void process(juce::AudioBuffer<float>& /*buffer*/) override {}

    int getLatencySamples() const override { return latency_; }

private:
    int latency_;
};

// ═══════════════════════════════════════════════════════════════════
// Lifecycle & Identity
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Processor: stores and returns name")
{
    GainProcessor proc;
    CHECK(proc.getName() == "Gain");
}

TEST_CASE("Processor: handle defaults to -1")
{
    GainProcessor proc;
    CHECK(proc.getHandle() == -1);
}

TEST_CASE("Processor: handle can be set and read")
{
    GainProcessor proc;
    proc.setHandle(42);
    CHECK(proc.getHandle() == 42);
}

TEST_CASE("Processor: setHandle overwrites previous handle")
{
    GainProcessor proc;
    proc.setHandle(1);
    proc.setHandle(99);
    CHECK(proc.getHandle() == 99);
}

// ═══════════════════════════════════════════════════════════════════
// In-place processing (GainProcessor)
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("GainProcessor: unity gain passes audio through in-place")
{
    GainProcessor proc;
    proc.prepare(44100.0, 4);

    juce::AudioBuffer<float> buffer(2, 4);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            buffer.setSample(ch, i, 0.5f);

    proc.process(buffer);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            CHECK(buffer.getSample(ch, i) == Catch::Approx(0.5f));
}

TEST_CASE("GainProcessor: applies gain to buffer in-place")
{
    GainProcessor proc;
    proc.prepare(44100.0, 4);
    proc.setParameter("gain", 0.5f);

    juce::AudioBuffer<float> buffer(2, 4);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            buffer.setSample(ch, i, 1.0f);

    proc.process(buffer);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            CHECK(buffer.getSample(ch, i) == Catch::Approx(0.5f));
}

TEST_CASE("GainProcessor: zero gain produces silence")
{
    GainProcessor proc;
    proc.prepare(44100.0, 4);
    proc.setParameter("gain", 0.0f);

    juce::AudioBuffer<float> buffer(2, 4);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            buffer.setSample(ch, i, 1.0f);

    proc.process(buffer);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            CHECK(buffer.getSample(ch, i) == Catch::Approx(0.0f));
}

TEST_CASE("GainProcessor: mono buffer works")
{
    GainProcessor proc;
    proc.prepare(44100.0, 4);
    proc.setParameter("gain", 0.25f);

    juce::AudioBuffer<float> buffer(1, 4);
    for (int i = 0; i < 4; ++i)
        buffer.setSample(0, i, 1.0f);

    proc.process(buffer);

    for (int i = 0; i < 4; ++i)
        CHECK(buffer.getSample(0, i) == Catch::Approx(0.25f));
}

// ═══════════════════════════════════════════════════════════════════
// MIDI variant
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Processor: default MIDI variant delegates to audio-only process")
{
    GainProcessor proc;
    proc.prepare(44100.0, 4);
    proc.setParameter("gain", 0.5f);

    juce::AudioBuffer<float> buffer(2, 4);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            buffer.setSample(ch, i, 1.0f);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);

    proc.process(buffer, midi);

    // Gain was still applied — MIDI was ignored, audio-only process ran
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            CHECK(buffer.getSample(ch, i) == Catch::Approx(0.5f));
}

TEST_CASE("Processor: custom MIDI override receives MIDI events")
{
    TestSynthProcessor proc;
    proc.prepare(44100.0, 4);

    juce::AudioBuffer<float> buffer(2, 4);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            buffer.setSample(ch, i, 1.0f);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 2);

    proc.process(buffer, midi);

    CHECK(proc.getMidiEventCount() == 2);
    // Buffer was cleared by TestSynthProcessor
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            CHECK(buffer.getSample(ch, i) == Catch::Approx(0.0f));
}

// ═══════════════════════════════════════════════════════════════════
// Parameters — defaults (base class)
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Processor: default getParameterCount returns 0")
{
    TestSynthProcessor proc;
    CHECK(proc.getParameterCount() == 0);
}

TEST_CASE("Processor: default getParameterDescriptors returns empty")
{
    TestSynthProcessor proc;
    CHECK(proc.getParameterDescriptors().empty());
}

TEST_CASE("Processor: default getParameter returns 0.0f")
{
    TestSynthProcessor proc;
    CHECK(proc.getParameter("anything") == Catch::Approx(0.0f));
}

TEST_CASE("Processor: default getParameterText returns empty string")
{
    TestSynthProcessor proc;
    CHECK(proc.getParameterText("anything") == "");
}

// ═══════════════════════════════════════════════════════════════════
// Parameters — GainProcessor
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("GainProcessor: getParameterCount returns 1")
{
    GainProcessor proc;
    CHECK(proc.getParameterCount() == 1);
}

TEST_CASE("GainProcessor: getParameterDescriptors returns correct metadata")
{
    GainProcessor proc;
    auto descs = proc.getParameterDescriptors();
    REQUIRE(descs.size() == 1);
    CHECK(descs[0].name == "gain");
    CHECK(descs[0].defaultValue == Catch::Approx(1.0f));
    CHECK(descs[0].minValue == Catch::Approx(0.0f));
    CHECK(descs[0].maxValue == Catch::Approx(1.0f));
    CHECK(descs[0].numSteps == 0);
    CHECK(descs[0].automatable == true);
    CHECK(descs[0].boolean == false);
    CHECK(descs[0].label == "");
    CHECK(descs[0].group == "");
}

TEST_CASE("GainProcessor: getParameter returns current value")
{
    GainProcessor proc;
    CHECK(proc.getParameter("gain") == Catch::Approx(1.0f));
}

TEST_CASE("GainProcessor: setParameter updates value and affects processing")
{
    GainProcessor proc;
    proc.prepare(44100.0, 4);
    proc.setParameter("gain", 0.5f);
    CHECK(proc.getParameter("gain") == Catch::Approx(0.5f));

    juce::AudioBuffer<float> buffer(1, 4);
    for (int i = 0; i < 4; ++i)
        buffer.setSample(0, i, 1.0f);

    proc.process(buffer);

    for (int i = 0; i < 4; ++i)
        CHECK(buffer.getSample(0, i) == Catch::Approx(0.5f));
}

TEST_CASE("GainProcessor: getParameter with unknown name returns 0.0f")
{
    GainProcessor proc;
    CHECK(proc.getParameter("unknown") == Catch::Approx(0.0f));
}

TEST_CASE("GainProcessor: setParameter with unknown name is a no-op")
{
    GainProcessor proc;
    proc.setParameter("unknown", 1.0f);
    CHECK(proc.getParameter("gain") == Catch::Approx(1.0f));
}

TEST_CASE("GainProcessor: getParameterText returns text for known name")
{
    GainProcessor proc;
    auto text = proc.getParameterText("gain");
    CHECK_FALSE(text.empty());
}

TEST_CASE("GainProcessor: getParameterText returns empty for unknown name")
{
    GainProcessor proc;
    CHECK(proc.getParameterText("unknown") == "");
}

// ═══════════════════════════════════════════════════════════════════
// Latency
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Processor: default getLatencySamples returns 0")
{
    GainProcessor proc;
    CHECK(proc.getLatencySamples() == 0);
}

TEST_CASE("Processor: custom latency override returns nonzero")
{
    TestLatencyProcessor proc(256);
    CHECK(proc.getLatencySamples() == 256);
}

TEST_CASE("Processor: latency override with different values")
{
    TestLatencyProcessor a(0);
    TestLatencyProcessor b(512);
    TestLatencyProcessor c(1024);
    CHECK(a.getLatencySamples() == 0);
    CHECK(b.getLatencySamples() == 512);
    CHECK(c.getLatencySamples() == 1024);
}

// ═══════════════════════════════════════════════════════════════════
// Polymorphism
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Processor: unique_ptr works for concrete subclass")
{
    std::unique_ptr<Processor> proc = std::make_unique<GainProcessor>();
    CHECK(proc->getName() == "Gain");
    CHECK(proc->getParameterCount() == 1);
}

TEST_CASE("Processor: virtual dispatch works through base pointer")
{
    std::unique_ptr<Processor> proc = std::make_unique<GainProcessor>();
    proc->prepare(44100.0, 4);
    proc->setParameter("gain", 0.5f);

    juce::AudioBuffer<float> buffer(2, 4);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 4; ++i)
            buffer.setSample(ch, i, 1.0f);

    proc->process(buffer);

    CHECK(buffer.getSample(0, 0) == Catch::Approx(0.5f));
}

TEST_CASE("Processor: different processor types coexist in a vector")
{
    std::vector<std::unique_ptr<Processor>> procs;
    procs.push_back(std::make_unique<GainProcessor>());
    procs.push_back(std::make_unique<TestSynthProcessor>());
    procs.push_back(std::make_unique<TestLatencyProcessor>(128));

    REQUIRE(procs.size() == 3);
    CHECK(procs[0]->getName() == "Gain");
    CHECK(procs[1]->getName() == "TestSynth");
    CHECK(procs[2]->getName() == "TestLatency");
    CHECK(procs[0]->getParameterCount() == 1);
    CHECK(procs[1]->getParameterCount() == 0);
    CHECK(procs[2]->getLatencySamples() == 128);
}

// ═══════════════════════════════════════════════════════════════════
// Bypass
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Processor: bypass defaults to false")
{
    GainProcessor proc;
    CHECK_FALSE(proc.isBypassed());
}

TEST_CASE("Processor: setBypassed and isBypassed roundtrip")
{
    GainProcessor proc;
    proc.setBypassed(true);
    CHECK(proc.isBypassed());
    proc.setBypassed(false);
    CHECK_FALSE(proc.isBypassed());
}

TEST_CASE("Processor: latency is unaffected by bypass state")
{
    TestLatencyProcessor proc(256);
    CHECK(proc.getLatencySamples() == 256);
    proc.setBypassed(true);
    CHECK(proc.getLatencySamples() == 256);
}

TEST_CASE("Processor: bypass is per-instance")
{
    GainProcessor a;
    GainProcessor b;
    a.setBypassed(true);
    CHECK(a.isBypassed());
    CHECK_FALSE(b.isBypassed());
}

// ═══════════════════════════════════════════════════════════════════
// Reset
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Processor: default reset is a no-op")
{
    GainProcessor proc;
    proc.prepare(44100.0, 4);
    proc.setParameter("gain", 0.5f);
    proc.reset();
    // Parameters are preserved after reset
    CHECK(proc.getParameter("gain") == Catch::Approx(0.5f));
}

TEST_CASE("Processor: reset clears internal processing state")
{
    StatefulProcessor proc;
    proc.prepare(44100.0, 4);

    juce::AudioBuffer<float> buffer(1, 4);
    for (int i = 0; i < 4; ++i)
        buffer.setSample(0, i, 1.0f);

    proc.process(buffer);
    CHECK(proc.getRunningSum() == Catch::Approx(4.0f));

    proc.reset();
    CHECK(proc.getRunningSum() == Catch::Approx(0.0f));
}

TEST_CASE("Processor: reset does not affect parameters")
{
    StatefulProcessor proc;
    proc.prepare(44100.0, 4);

    juce::AudioBuffer<float> buffer(1, 4);
    for (int i = 0; i < 4; ++i)
        buffer.setSample(0, i, 1.0f);

    proc.process(buffer);
    proc.reset();

    // Process again — state starts fresh
    proc.process(buffer);
    CHECK(proc.getRunningSum() == Catch::Approx(4.0f));
}

// ═══════════════════════════════════════════════════════════════════
// Release (default no-op)
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Processor: default release is a no-op")
{
    GainProcessor proc;
    proc.prepare(44100.0, 512);
    proc.release();  // should not crash
    CHECK(proc.getName() == "Gain");
}
