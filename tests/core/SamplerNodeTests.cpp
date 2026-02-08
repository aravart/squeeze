#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/SamplerNode.h"
#include "core/Buffer.h"

using namespace squeeze;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// --- Helpers ---

static std::unique_ptr<Buffer> makeConstBuffer(float value = 1.0f,
                                                int numSamples = 10000,
                                                int numChannels = 1) {
    auto buf = Buffer::createEmpty(numChannels, numSamples, 44100.0, "const");
    auto& data = buf->getAudioData();
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            data.setSample(ch, i, value);
    return buf;
}

static float rms(const juce::AudioBuffer<float>& buf, int channel,
                 int start = 0, int numSamples = -1) {
    if (numSamples < 0) numSamples = buf.getNumSamples() - start;
    if (numSamples <= 0) return 0.0f;
    float sum = 0.0f;
    auto* p = buf.getReadPointer(channel);
    for (int i = start; i < start + numSamples; ++i)
        sum += p[i] * p[i];
    return std::sqrt(sum / numSamples);
}

static float peak(const juce::AudioBuffer<float>& buf, int channel,
                  int start = 0, int numSamples = -1) {
    if (numSamples < 0) numSamples = buf.getNumSamples() - start;
    if (numSamples <= 0) return 0.0f;
    float mx = 0.0f;
    auto* p = buf.getReadPointer(channel);
    for (int i = start; i < start + numSamples; ++i)
        mx = std::max(mx, std::abs(p[i]));
    return mx;
}

static bool isSilent(const juce::AudioBuffer<float>& buf, int channel,
                     int start = 0, int numSamples = -1) {
    return peak(buf, channel, start, numSamples) < 1e-6f;
}

static ProcessContext makeContext(juce::AudioBuffer<float>& inAudio,
                                  juce::AudioBuffer<float>& outAudio,
                                  juce::MidiBuffer& midi, int numSamples) {
    return { inAudio, outAudio, midi, midi, numSamples };
}

// =============================================================================
// Ports
// =============================================================================

TEST_CASE("SamplerNode: getInputPorts returns 1 MIDI input", "[SamplerNode]") {
    SamplerNode node;
    auto ports = node.getInputPorts();
    REQUIRE(ports.size() == 1);
    CHECK(ports[0].name == "midi_in");
    CHECK(ports[0].direction == PortDirection::input);
    CHECK(ports[0].signalType == SignalType::midi);
    CHECK(ports[0].channels == 1);
}

TEST_CASE("SamplerNode: getOutputPorts returns 1 stereo audio output", "[SamplerNode]") {
    SamplerNode node;
    auto ports = node.getOutputPorts();
    REQUIRE(ports.size() == 1);
    CHECK(ports[0].name == "out");
    CHECK(ports[0].direction == PortDirection::output);
    CHECK(ports[0].signalType == SignalType::audio);
    CHECK(ports[0].channels == 2);
}

// =============================================================================
// Parameter Descriptors
// =============================================================================

TEST_CASE("SamplerNode: getParameterDescriptors returns 32 entries", "[SamplerNode]") {
    SamplerNode node;
    auto descs = node.getParameterDescriptors();
    REQUIRE(descs.size() == 32);

    // Spot-check first and last
    CHECK(descs[0].name == "sample_start");
    CHECK(descs[0].index == 0);
    CHECK_THAT(descs[0].defaultValue, WithinAbs(0.0f, 0.001f));

    CHECK(descs[31].name == "filter_release_curve");
    CHECK(descs[31].index == 31);
    CHECK_THAT(descs[31].defaultValue, WithinAbs(0.333f, 0.001f));
}

TEST_CASE("SamplerNode: parameter descriptors have correct groups", "[SamplerNode]") {
    SamplerNode node;
    auto descs = node.getParameterDescriptors();

    CHECK(descs[0].group == "Playback");   // sample_start
    CHECK(descs[3].group == "Loop");       // loop_start
    CHECK(descs[8].group == "Pitch");      // pitch_coarse
    CHECK(descs[10].group == "Amp");       // volume
    CHECK(descs[13].group == "Amp Env");   // amp_attack
    CHECK(descs[21].group == "Filter");    // filter_type
    CHECK(descs[25].group == "Filter Env"); // filter_attack
}

TEST_CASE("SamplerNode: parameter descriptors have correct steps", "[SamplerNode]") {
    SamplerNode node;
    auto descs = node.getParameterDescriptors();

    CHECK(descs[0].numSteps == 0);    // sample_start (continuous)
    CHECK(descs[2].numSteps == 128);  // root_note
    CHECK(descs[5].numSteps == 4);    // loop_mode
    CHECK(descs[7].numSteps == 2);    // direction
    CHECK(descs[8].numSteps == 97);   // pitch_coarse
    CHECK(descs[18].numSteps == 3);   // amp_attack_curve
    CHECK(descs[21].numSteps == 5);   // filter_type
}

TEST_CASE("SamplerNode: findParameterIndex works for all names", "[SamplerNode]") {
    SamplerNode node;
    auto descs = node.getParameterDescriptors();

    for (const auto& d : descs) {
        CHECK(node.findParameterIndex(d.name) == d.index);
    }
}

TEST_CASE("SamplerNode: findParameterIndex returns -1 for unknown name", "[SamplerNode]") {
    SamplerNode node;
    CHECK(node.findParameterIndex("nonexistent") == -1);
    CHECK(node.findParameterIndex("") == -1);
}

// =============================================================================
// Parameter Round-trips
// =============================================================================

TEST_CASE("SamplerNode: sample_start round-trip (direct)", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(0, 0.3f);
    CHECK_THAT(node.getParameter(0), WithinAbs(0.3f, 0.001f));
}

TEST_CASE("SamplerNode: amp_attack round-trip (time mapping)", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(13, 0.5f);
    CHECK_THAT(node.getParameter(13), WithinAbs(0.5f, 0.001f));
}

TEST_CASE("SamplerNode: amp_hold round-trip (linear time)", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(14, 0.5f);
    CHECK_THAT(node.getParameter(14), WithinAbs(0.5f, 0.001f));
}

TEST_CASE("SamplerNode: filter_cutoff round-trip (freq mapping)", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(22, 0.5f);
    CHECK_THAT(node.getParameter(22), WithinAbs(0.5f, 0.001f));
}

TEST_CASE("SamplerNode: root_note round-trip", "[SamplerNode]") {
    SamplerNode node;
    float v = 60.0f / 127.0f;  // ~0.4724
    node.setParameter(2, v);
    CHECK_THAT(node.getParameter(2), WithinAbs(v, 0.001f));
}

TEST_CASE("SamplerNode: pitch_coarse round-trip (0.5 → 0 st)", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(8, 0.5f);
    CHECK_THAT(node.getParameter(8), WithinAbs(0.5f, 0.001f));
}

TEST_CASE("SamplerNode: pitch_fine round-trip (0.5 → 0 ct)", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(9, 0.5f);
    CHECK_THAT(node.getParameter(9), WithinAbs(0.5f, 0.001f));
}

TEST_CASE("SamplerNode: volume round-trip (quadratic taper)", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(10, 0.8f);
    CHECK_THAT(node.getParameter(10), WithinAbs(0.8f, 0.001f));
}

TEST_CASE("SamplerNode: pan round-trip (0.5 → center)", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(11, 0.5f);
    CHECK_THAT(node.getParameter(11), WithinAbs(0.5f, 0.001f));
}

TEST_CASE("SamplerNode: loop_mode round-trip", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(5, 0.333f);
    CHECK_THAT(node.getParameter(5), WithinAbs(0.333f, 0.001f));
}

TEST_CASE("SamplerNode: direction round-trip", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(7, 0.0f);
    CHECK_THAT(node.getParameter(7), WithinAbs(0.0f, 0.001f));
    node.setParameter(7, 1.0f);
    CHECK_THAT(node.getParameter(7), WithinAbs(1.0f, 0.001f));
}

TEST_CASE("SamplerNode: filter_type round-trip", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(21, 0.25f);
    CHECK_THAT(node.getParameter(21), WithinAbs(0.25f, 0.001f));
}

TEST_CASE("SamplerNode: curve round-trip (0.5 → exponential)", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(18, 0.5f);
    CHECK_THAT(node.getParameter(18), WithinAbs(0.5f, 0.001f));
}

TEST_CASE("SamplerNode: loop_crossfade round-trip", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(6, 0.5f);
    CHECK_THAT(node.getParameter(6), WithinAbs(0.5f, 0.001f));
}

TEST_CASE("SamplerNode: filter_env_amount round-trip", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(24, 0.75f);
    CHECK_THAT(node.getParameter(24), WithinAbs(0.75f, 0.001f));
}

// =============================================================================
// Parameter Edge Cases
// =============================================================================

TEST_CASE("SamplerNode: setParameter clamps to [0, 1]", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(0, -0.5f);
    CHECK_THAT(node.getParameter(0), WithinAbs(0.0f, 0.001f));
    node.setParameter(0, 1.5f);
    CHECK_THAT(node.getParameter(0), WithinAbs(1.0f, 0.001f));
}

TEST_CASE("SamplerNode: out-of-range index returns safe defaults", "[SamplerNode]") {
    SamplerNode node;
    CHECK(node.getParameter(-1) == 0.0f);
    CHECK(node.getParameter(32) == 0.0f);
    CHECK(node.getParameter(999) == 0.0f);
    CHECK(node.getParameterText(-1) == "");
    CHECK(node.getParameterText(32) == "");

    // setParameter with out-of-range index is a no-op (should not crash)
    node.setParameter(-1, 0.5f);
    node.setParameter(32, 0.5f);
}

// =============================================================================
// Parameter Display Text
// =============================================================================

TEST_CASE("SamplerNode: amp_attack display text", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(13, 0.0f);
    CHECK(node.getParameterText(13) == "1.0 ms");

    node.setParameter(13, 0.5f);
    CHECK(node.getParameterText(13) == "100.0 ms");

    node.setParameter(13, 1.0f);
    CHECK(node.getParameterText(13) == "10.0 s");
}

TEST_CASE("SamplerNode: filter_cutoff display text", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(22, 0.0f);
    CHECK(node.getParameterText(22) == "20 Hz");

    node.setParameter(22, 1.0f);
    CHECK(node.getParameterText(22) == "20.0 kHz");
}

TEST_CASE("SamplerNode: pitch_coarse display text", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(8, 0.5f);
    CHECK(node.getParameterText(8) == "0 st");

    node.setParameter(8, 0.625f);
    CHECK(node.getParameterText(8) == "+12 st");
}

TEST_CASE("SamplerNode: pan display text", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(11, 0.5f);
    CHECK(node.getParameterText(11) == "C");

    node.setParameter(11, 0.0f);
    CHECK(node.getParameterText(11) == "L100");
}

TEST_CASE("SamplerNode: loop_mode display text", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(5, 0.0f);
    CHECK(node.getParameterText(5) == "Off");

    node.setParameter(5, 0.333f);
    CHECK(node.getParameterText(5) == "Forward");
}

TEST_CASE("SamplerNode: volume display text", "[SamplerNode]") {
    SamplerNode node;
    node.setParameter(10, 0.0f);
    CHECK(node.getParameterText(10) == "-inf dB");
}

// =============================================================================
// Buffer
// =============================================================================

TEST_CASE("SamplerNode: setBuffer/getBuffer round-trip", "[SamplerNode]") {
    SamplerNode node;
    auto buf = makeConstBuffer();
    CHECK(node.getBuffer() == nullptr);
    node.setBuffer(buf.get());
    CHECK(node.getBuffer() == buf.get());
}

TEST_CASE("SamplerNode: setBuffer(nullptr) is safe", "[SamplerNode]") {
    SamplerNode node;
    auto buf = makeConstBuffer();
    node.setBuffer(buf.get());
    node.setBuffer(nullptr);
    CHECK(node.getBuffer() == nullptr);
}

// =============================================================================
// Process
// =============================================================================

TEST_CASE("SamplerNode: process with empty MIDI produces silence", "[SamplerNode]") {
    SamplerNode node;
    node.prepare(44100.0, 512);
    auto buf = makeConstBuffer(1.0f, 10000, 2);
    node.setBuffer(buf.get());

    juce::AudioBuffer<float> inAudio(2, 512);
    juce::AudioBuffer<float> outAudio(2, 512);
    juce::MidiBuffer midi;
    auto ctx = makeContext(inAudio, outAudio, midi, 512);

    node.process(ctx);
    CHECK(isSilent(outAudio, 0));
    CHECK(isSilent(outAudio, 1));
}

TEST_CASE("SamplerNode: process with noteOn produces audio output", "[SamplerNode]") {
    SamplerNode node;
    node.prepare(44100.0, 512);
    auto buf = makeConstBuffer(1.0f, 10000, 2);
    node.setBuffer(buf.get());

    juce::AudioBuffer<float> inAudio(2, 512);
    juce::AudioBuffer<float> outAudio(2, 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    auto ctx = makeContext(inAudio, outAudio, midi, 512);

    node.process(ctx);
    CHECK(!isSilent(outAudio, 0));
}

TEST_CASE("SamplerNode: process with noteOn + noteOff enters release", "[SamplerNode]") {
    SamplerNode node;
    node.prepare(44100.0, 512);
    auto buf = makeConstBuffer(1.0f, 100000, 2);
    node.setBuffer(buf.get());

    // Set a visible release time
    node.setParameter(17, 0.5f); // amp_release = 100ms

    // Block 1: noteOn
    {
        juce::AudioBuffer<float> inAudio(2, 512);
        juce::AudioBuffer<float> outAudio(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
        auto ctx = makeContext(inAudio, outAudio, midi, 512);
        node.process(ctx);
        CHECK(!isSilent(outAudio, 0));
    }

    // Block 2: noteOff
    {
        juce::AudioBuffer<float> inAudio(2, 512);
        juce::AudioBuffer<float> outAudio(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        auto ctx = makeContext(inAudio, outAudio, midi, 512);
        node.process(ctx);
        // Should still have audio (in release phase)
        CHECK(!isSilent(outAudio, 0));
    }
}

TEST_CASE("SamplerNode: sub-block splitting — noteOn at sample 100", "[SamplerNode]") {
    SamplerNode node;
    node.prepare(44100.0, 512);
    auto buf = makeConstBuffer(1.0f, 10000, 2);
    node.setBuffer(buf.get());

    juce::AudioBuffer<float> inAudio(2, 512);
    juce::AudioBuffer<float> outAudio(2, 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)127), 100);
    auto ctx = makeContext(inAudio, outAudio, midi, 512);

    node.process(ctx);

    // Samples 0..99 should be silent
    CHECK(isSilent(outAudio, 0, 0, 100));

    // Samples 100+ should have audio
    CHECK(!isSilent(outAudio, 0, 100, 412));
}

TEST_CASE("SamplerNode: noteOn with velocity 0 treated as noteOff", "[SamplerNode]") {
    SamplerNode node;
    node.prepare(44100.0, 512);
    auto buf = makeConstBuffer(1.0f, 100000, 2);
    node.setBuffer(buf.get());

    // Set very short release so voice goes idle quickly
    node.setParameter(17, 0.0f); // amp_release = 1ms

    // Block 1: noteOn
    {
        juce::AudioBuffer<float> inAudio(2, 512);
        juce::AudioBuffer<float> outAudio(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
        auto ctx = makeContext(inAudio, outAudio, midi, 512);
        node.process(ctx);
    }

    // Block 2: noteOn with velocity 0 (= noteOff)
    {
        juce::AudioBuffer<float> inAudio(2, 512);
        juce::AudioBuffer<float> outAudio(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)0), 0);
        auto ctx = makeContext(inAudio, outAudio, midi, 512);
        node.process(ctx);
    }

    // Block 3: should be silent (release finished in ~44 samples at 1ms)
    {
        juce::AudioBuffer<float> inAudio(2, 512);
        juce::AudioBuffer<float> outAudio(2, 512);
        juce::MidiBuffer midi;
        auto ctx = makeContext(inAudio, outAudio, midi, 512);
        node.process(ctx);
        CHECK(isSilent(outAudio, 0));
    }
}

TEST_CASE("SamplerNode: null buffer causes noteOn to be ignored", "[SamplerNode]") {
    SamplerNode node;
    node.prepare(44100.0, 512);
    // No buffer set

    juce::AudioBuffer<float> inAudio(2, 512);
    juce::AudioBuffer<float> outAudio(2, 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)127), 0);
    auto ctx = makeContext(inAudio, outAudio, midi, 512);

    node.process(ctx);
    CHECK(isSilent(outAudio, 0));
    CHECK(isSilent(outAudio, 1));
}

TEST_CASE("SamplerNode: output is cleared before render", "[SamplerNode]") {
    SamplerNode node;
    node.prepare(44100.0, 512);

    juce::AudioBuffer<float> inAudio(2, 512);
    juce::AudioBuffer<float> outAudio(2, 512);
    juce::MidiBuffer midi;

    // Fill output with garbage
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            outAudio.setSample(ch, i, 999.0f);

    auto ctx = makeContext(inAudio, outAudio, midi, 512);
    node.process(ctx);

    // Should be cleared (no MIDI, no buffer → silence)
    CHECK(isSilent(outAudio, 0));
    CHECK(isSilent(outAudio, 1));
}

// =============================================================================
// Integration: full lifecycle
// =============================================================================

TEST_CASE("SamplerNode: full lifecycle — prepare, buffer, noteOn, render, noteOff, idle", "[SamplerNode]") {
    SamplerNode node;
    node.prepare(44100.0, 512);

    auto buf = makeConstBuffer(0.5f, 100000, 2);
    node.setBuffer(buf.get());

    // Set very short envelope for fast idle
    node.setParameter(13, 0.0f); // amp_attack = 1ms
    node.setParameter(17, 0.0f); // amp_release = 1ms

    // Block 1: noteOn at sample 0
    {
        juce::AudioBuffer<float> inAudio(2, 512);
        juce::AudioBuffer<float> outAudio(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
        auto ctx = makeContext(inAudio, outAudio, midi, 512);
        node.process(ctx);
        CHECK(!isSilent(outAudio, 0));
    }

    // Block 2: sustaining
    {
        juce::AudioBuffer<float> inAudio(2, 512);
        juce::AudioBuffer<float> outAudio(2, 512);
        juce::MidiBuffer midi;
        auto ctx = makeContext(inAudio, outAudio, midi, 512);
        node.process(ctx);
        CHECK(!isSilent(outAudio, 0));
    }

    // Block 3: noteOff
    {
        juce::AudioBuffer<float> inAudio(2, 512);
        juce::AudioBuffer<float> outAudio(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        auto ctx = makeContext(inAudio, outAudio, midi, 512);
        node.process(ctx);
        // Release starts — may still have some audio
    }

    // Block 4+: wait for idle (release = 1ms = ~44 samples)
    {
        juce::AudioBuffer<float> inAudio(2, 512);
        juce::AudioBuffer<float> outAudio(2, 512);
        juce::MidiBuffer midi;
        auto ctx = makeContext(inAudio, outAudio, midi, 512);
        node.process(ctx);
        CHECK(isSilent(outAudio, 0));
    }
}
