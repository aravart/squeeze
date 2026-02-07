#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/PluginCache.h"
#include "core/PluginNode.h"

using namespace squeeze;
using Catch::Matchers::WithinAbs;

// ============================================================
// Test audio processor (plain AudioProcessor subclass for testing)
// ============================================================

class TestProcessor : public juce::AudioProcessor {
public:
    TestProcessor(int numIn, int numOut, bool midi)
        : AudioProcessor(BusesProperties()
            .withInput("Input", juce::AudioChannelSet::canonicalChannelSet(std::max(numIn, 1)), numIn > 0)
            .withOutput("Output", juce::AudioChannelSet::canonicalChannelSet(std::max(numOut, 1)), numOut > 0))
        , acceptsMidi_(midi), numIn_(numIn), numOut_(numOut)
    {
        addParameter(new juce::AudioParameterFloat(
            juce::ParameterID{"gain", 1}, "Gain",
            juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
        addParameter(new juce::AudioParameterFloat(
            juce::ParameterID{"mix", 1}, "Mix",
            juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    }

    const juce::String getName() const override { return "TestPlugin"; }

    void prepareToPlay(double sr, int bs) override {
        preparedSampleRate = sr;
        preparedBlockSize = bs;
    }

    void releaseResources() override { released = true; }

    void processBlock(juce::AudioBuffer<float>& audio,
                      juce::MidiBuffer& midi) override
    {
        processBlockCalled = true;
        lastNumSamples = audio.getNumSamples();
        lastMidiCount = 0;
        for (const auto& m : midi) {
            (void)m;
            lastMidiCount++;
        }
        // Simple processing: multiply audio by the gain parameter value
        float gainVal = getParameters()[0]->getValue();
        audio.applyGain(gainVal);
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    bool acceptsMidi() const override { return acceptsMidi_; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Test inspection state
    double preparedSampleRate = 0;
    int preparedBlockSize = 0;
    bool released = false;
    bool processBlockCalled = false;
    int lastNumSamples = 0;
    int lastMidiCount = 0;

private:
    bool acceptsMidi_;
    int numIn_;
    int numOut_;
};

// ============================================================
// PluginCache tests
// ============================================================

static const char* testCacheXml = R"(
<?xml version="1.0" encoding="UTF-8"?>
<KNOWNPLUGINS>
  <PLUGIN name="TestSynth" format="VST3" category="Instrument|Synth"
          manufacturer="TestCo" version="1.0.0"
          file="/path/to/TestSynth.vst3"
          uniqueId="aabbccdd" isInstrument="1"
          fileTime="0" infoUpdateTime="0"
          numInputs="0" numOutputs="2"
          isShell="0" hasARAExtension="0" uid="11223344"/>
  <PLUGIN name="TestEffect" format="VST3" category="Fx|Delay"
          manufacturer="TestCo" version="2.0.0"
          file="/path/to/TestEffect.vst3"
          uniqueId="eeff0011" isInstrument="0"
          fileTime="0" infoUpdateTime="0"
          numInputs="2" numOutputs="2"
          isShell="0" hasARAExtension="0" uid="55667788"/>
  <PLUGIN name="TestMultiOut" format="VST3" category="Instrument|Synth"
          manufacturer="TestCo" version="1.0.0"
          file="/path/to/TestMultiOut.vst3"
          uniqueId="22334455" isInstrument="1"
          fileTime="0" infoUpdateTime="0"
          numInputs="0" numOutputs="8"
          isShell="0" hasARAExtension="0" uid="99aabbcc"/>
</KNOWNPLUGINS>
)";

TEST_CASE("PluginCache loads plugins from valid XML string")
{
    PluginCache cache;
    REQUIRE(cache.loadFromXml(testCacheXml));
    REQUIRE(cache.getNumPlugins() == 3);
}

TEST_CASE("PluginCache findByName returns correct plugin")
{
    PluginCache cache;
    cache.loadFromXml(testCacheXml);

    auto* desc = cache.findByName("TestSynth");
    REQUIRE(desc != nullptr);
    REQUIRE(desc->name == "TestSynth");
    REQUIRE(desc->pluginFormatName == "VST3");
    REQUIRE(desc->numInputChannels == 0);
    REQUIRE(desc->numOutputChannels == 2);
    REQUIRE(desc->isInstrument == true);
}

TEST_CASE("PluginCache findByName returns effect plugin")
{
    PluginCache cache;
    cache.loadFromXml(testCacheXml);

    auto* desc = cache.findByName("TestEffect");
    REQUIRE(desc != nullptr);
    REQUIRE(desc->name == "TestEffect");
    REQUIRE(desc->numInputChannels == 2);
    REQUIRE(desc->numOutputChannels == 2);
    REQUIRE(desc->isInstrument == false);
}

TEST_CASE("PluginCache findByName returns nullptr for unknown plugin")
{
    PluginCache cache;
    cache.loadFromXml(testCacheXml);

    REQUIRE(cache.findByName("NonExistent") == nullptr);
}

TEST_CASE("PluginCache getAvailablePluginNames returns all names")
{
    PluginCache cache;
    cache.loadFromXml(testCacheXml);

    auto names = cache.getAvailablePluginNames();
    REQUIRE(names.size() == 3);

    bool hasTestSynth = false, hasTestEffect = false, hasTestMultiOut = false;
    for (const auto& name : names) {
        if (name == "TestSynth") hasTestSynth = true;
        if (name == "TestEffect") hasTestEffect = true;
        if (name == "TestMultiOut") hasTestMultiOut = true;
    }
    REQUIRE(hasTestSynth);
    REQUIRE(hasTestEffect);
    REQUIRE(hasTestMultiOut);
}

TEST_CASE("PluginCache loadFromXml returns false for empty string")
{
    PluginCache cache;
    REQUIRE_FALSE(cache.loadFromXml(""));
    REQUIRE(cache.getNumPlugins() == 0);
}

TEST_CASE("PluginCache loadFromXml returns false for malformed XML")
{
    PluginCache cache;
    REQUIRE_FALSE(cache.loadFromXml("<not-a-plugin-list>bad</not-a-plugin-list>"));
    REQUIRE(cache.getNumPlugins() == 0);
}

TEST_CASE("PluginCache loadFromFile returns false for non-existent file")
{
    PluginCache cache;
    REQUIRE_FALSE(cache.loadFromFile(juce::File("/nonexistent/path/plugins.xml")));
    REQUIRE(cache.getNumPlugins() == 0);
}

TEST_CASE("PluginCache multiple loads replace previous data")
{
    PluginCache cache;
    cache.loadFromXml(testCacheXml);
    REQUIRE(cache.getNumPlugins() == 3);

    const char* smallXml = R"(
    <?xml version="1.0" encoding="UTF-8"?>
    <KNOWNPLUGINS>
      <PLUGIN name="OnlyOne" format="VST3" category="Instrument|Synth"
              manufacturer="Test" version="1.0"
              file="/path/to/OnlyOne.vst3"
              uniqueId="abcdef01" isInstrument="1"
              fileTime="0" infoUpdateTime="0"
              numInputs="0" numOutputs="2"
              isShell="0" hasARAExtension="0" uid="12345678"/>
    </KNOWNPLUGINS>
    )";

    cache.loadFromXml(smallXml);
    REQUIRE(cache.getNumPlugins() == 1);
    REQUIRE(cache.findByName("TestSynth") == nullptr);
    REQUIRE(cache.findByName("OnlyOne") != nullptr);
}

TEST_CASE("PluginCache empty after default construction")
{
    PluginCache cache;
    REQUIRE(cache.getNumPlugins() == 0);
    REQUIRE(cache.getAvailablePluginNames().empty());
    REQUIRE(cache.findByName("anything") == nullptr);
}

// ============================================================
// PluginNode port declaration tests
// ============================================================

TEST_CASE("PluginNode instrument has MIDI input and audio output ports")
{
    auto* raw = new TestProcessor(0, 2, true);
    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 0, 2, true);

    auto inputs = node->getInputPorts();
    auto outputs = node->getOutputPorts();

    // Instrument: MIDI input only (no audio input)
    REQUIRE(inputs.size() == 1);
    REQUIRE(inputs[0].signalType == SignalType::midi);
    REQUIRE(inputs[0].name == "midi_in");

    // Audio output + MIDI output
    REQUIRE(outputs.size() == 2);
    bool hasAudioOut = false, hasMidiOut = false;
    for (const auto& p : outputs) {
        if (p.signalType == SignalType::audio) {
            hasAudioOut = true;
            REQUIRE(p.channels == 2);
            REQUIRE(p.name == "out");
        }
        if (p.signalType == SignalType::midi) {
            hasMidiOut = true;
            REQUIRE(p.name == "midi_out");
        }
    }
    REQUIRE(hasAudioOut);
    REQUIRE(hasMidiOut);
}

TEST_CASE("PluginNode effect has audio input and audio output ports")
{
    auto* raw = new TestProcessor(2, 2, false);
    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 2, 2, false);

    auto inputs = node->getInputPorts();
    auto outputs = node->getOutputPorts();

    // Effect without MIDI: audio input only
    REQUIRE(inputs.size() == 1);
    REQUIRE(inputs[0].signalType == SignalType::audio);
    REQUIRE(inputs[0].name == "in");
    REQUIRE(inputs[0].channels == 2);

    // Audio output + MIDI output
    REQUIRE(outputs.size() == 2);
}

TEST_CASE("PluginNode effect with MIDI has both audio and MIDI input ports")
{
    auto* raw = new TestProcessor(2, 2, true);
    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 2, 2, true);

    auto inputs = node->getInputPorts();

    REQUIRE(inputs.size() == 2);
    bool hasAudioIn = false, hasMidiIn = false;
    for (const auto& p : inputs) {
        if (p.signalType == SignalType::audio) hasAudioIn = true;
        if (p.signalType == SignalType::midi) hasMidiIn = true;
    }
    REQUIRE(hasAudioIn);
    REQUIRE(hasMidiIn);
}

TEST_CASE("PluginNode all configurations have MIDI output port")
{
    // Instrument
    {
        auto* raw = new TestProcessor(0, 2, true);
        auto node = std::make_unique<PluginNode>(
            std::unique_ptr<juce::AudioProcessor>(raw), 0, 2, true);
        bool hasMidiOut = false;
        for (const auto& p : node->getOutputPorts())
            if (p.signalType == SignalType::midi) hasMidiOut = true;
        REQUIRE(hasMidiOut);
    }
    // Effect without MIDI
    {
        auto* raw = new TestProcessor(2, 2, false);
        auto node = std::make_unique<PluginNode>(
            std::unique_ptr<juce::AudioProcessor>(raw), 2, 2, false);
        bool hasMidiOut = false;
        for (const auto& p : node->getOutputPorts())
            if (p.signalType == SignalType::midi) hasMidiOut = true;
        REQUIRE(hasMidiOut);
    }
}

// ============================================================
// PluginNode lifecycle tests
// ============================================================

TEST_CASE("PluginNode prepare forwards to plugin instance")
{
    auto* raw = new TestProcessor(0, 2, true);
    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 0, 2, true);

    node->prepare(48000.0, 256);

    REQUIRE(raw->preparedSampleRate == 48000.0);
    REQUIRE(raw->preparedBlockSize == 256);
}

TEST_CASE("PluginNode release forwards to plugin instance")
{
    auto* raw = new TestProcessor(0, 2, true);
    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 0, 2, true);

    REQUIRE_FALSE(raw->released);
    node->release();
    REQUIRE(raw->released);
}

// ============================================================
// PluginNode process tests
// ============================================================

TEST_CASE("PluginNode process for instrument generates audio output")
{
    auto* raw = new TestProcessor(0, 2, true);
    // Set gain parameter to 1.0 so audio passes through unmodified
    raw->getParameters()[0]->setValue(1.0f);

    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 0, 2, true);
    node->prepare(44100.0, 64);

    juce::AudioBuffer<float> inputAudio(2, 64);
    juce::AudioBuffer<float> outputAudio(2, 64);
    juce::MidiBuffer inputMidi, outputMidi;

    // Fill output with 1.0 to verify plugin clears and processes it
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            outputAudio.setSample(ch, i, 1.0f);

    inputAudio.clear();

    ProcessContext ctx{inputAudio, outputAudio, inputMidi, outputMidi, 64};
    node->process(ctx);

    REQUIRE(raw->processBlockCalled);
    // For instrument, output is cleared first, then processBlock applies gain
    // 0.0 * 1.0 = 0.0
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            REQUIRE_THAT(outputAudio.getSample(ch, i), WithinAbs(0.0f, 1e-6));
}

TEST_CASE("PluginNode process for effect processes input audio")
{
    auto* raw = new TestProcessor(2, 2, false);
    // Set gain parameter to 0.5
    raw->getParameters()[0]->setValue(0.5f);

    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 2, 2, false);
    node->prepare(44100.0, 64);

    juce::AudioBuffer<float> inputAudio(2, 64);
    juce::AudioBuffer<float> outputAudio(2, 64);
    juce::MidiBuffer inputMidi, outputMidi;

    // Fill input with 1.0
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            inputAudio.setSample(ch, i, 1.0f);

    outputAudio.clear();

    ProcessContext ctx{inputAudio, outputAudio, inputMidi, outputMidi, 64};
    node->process(ctx);

    REQUIRE(raw->processBlockCalled);
    // Effect: input (1.0) copied to output, then gain (0.5) applied => 0.5
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            REQUIRE_THAT(outputAudio.getSample(ch, i), WithinAbs(0.5f, 1e-6));
}

TEST_CASE("PluginNode process forwards MIDI to plugin")
{
    auto* raw = new TestProcessor(0, 2, true);
    raw->getParameters()[0]->setValue(1.0f);

    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 0, 2, true);
    node->prepare(44100.0, 64);

    juce::AudioBuffer<float> inputAudio(2, 64);
    juce::AudioBuffer<float> outputAudio(2, 64);
    juce::MidiBuffer inputMidi, outputMidi;

    // Add MIDI events
    inputMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    inputMidi.addEvent(juce::MidiMessage::noteOff(1, 60, 0.0f), 32);

    inputAudio.clear();
    outputAudio.clear();

    ProcessContext ctx{inputAudio, outputAudio, inputMidi, outputMidi, 64};
    node->process(ctx);

    // Plugin should have received the MIDI events
    REQUIRE(raw->lastMidiCount == 2);
}

// ============================================================
// PluginNode parameter tests
// ============================================================

TEST_CASE("PluginNode getParameterDescriptors returns plugin parameter descriptors")
{
    auto* raw = new TestProcessor(2, 2, false);
    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 2, 2, false);

    auto descs = node->getParameterDescriptors();
    REQUIRE(descs.size() == 2);

    bool hasGain = false, hasMix = false;
    for (const auto& d : descs) {
        if (d.name == "Gain") hasGain = true;
        if (d.name == "Mix") hasMix = true;
    }
    REQUIRE(hasGain);
    REQUIRE(hasMix);
}

TEST_CASE("PluginNode getParameter by index returns normalized value")
{
    auto* raw = new TestProcessor(2, 2, false);
    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 2, 2, false);

    // Default gain is 0.5 (index 0)
    float val = node->getParameter(0);
    REQUIRE_THAT(val, WithinAbs(0.5f, 1e-3));
}

TEST_CASE("PluginNode setParameter by index sets normalized value")
{
    auto* raw = new TestProcessor(2, 2, false);
    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 2, 2, false);

    node->setParameter(0, 0.75f);

    float val = node->getParameter(0);
    REQUIRE_THAT(val, WithinAbs(0.75f, 1e-3));
}

TEST_CASE("PluginNode getParameterByName returns correct value")
{
    auto* raw = new TestProcessor(2, 2, false);
    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 2, 2, false);

    REQUIRE_THAT(node->getParameterByName("Gain"), WithinAbs(0.5f, 1e-3));
}

TEST_CASE("PluginNode getParameterByName with unknown name returns 0.0")
{
    auto* raw = new TestProcessor(2, 2, false);
    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 2, 2, false);

    REQUIRE_THAT(node->getParameterByName("nonexistent"), WithinAbs(0.0f, 1e-6));
}

TEST_CASE("PluginNode setParameterByName with unknown name returns false")
{
    auto* raw = new TestProcessor(2, 2, false);
    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 2, 2, false);

    REQUIRE_FALSE(node->setParameterByName("nonexistent", 0.5f));

    // Existing parameters remain unchanged
    REQUIRE_THAT(node->getParameterByName("Gain"), WithinAbs(0.5f, 1e-3));
}

TEST_CASE("PluginNode findParameterIndex returns correct index")
{
    auto* raw = new TestProcessor(2, 2, false);
    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 2, 2, false);

    REQUIRE(node->findParameterIndex("Gain") == 0);
    REQUIRE(node->findParameterIndex("Mix") == 1);
    REQUIRE(node->findParameterIndex("nonexistent") == -1);
}

TEST_CASE("PluginNode getParameterText returns display text")
{
    auto* raw = new TestProcessor(2, 2, false);
    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 2, 2, false);

    auto text = node->getParameterText(0);
    REQUIRE_FALSE(text.empty());
}

TEST_CASE("PluginNode getParameterText returns empty for out-of-range index")
{
    auto* raw = new TestProcessor(2, 2, false);
    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 2, 2, false);

    REQUIRE(node->getParameterText(99).empty());
}

TEST_CASE("PluginNode getName returns plugin name")
{
    auto* raw = new TestProcessor(0, 2, true);
    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 0, 2, true);

    REQUIRE(node->getName() == "TestPlugin");
}

TEST_CASE("PluginNode getProcessor returns the wrapped processor")
{
    auto* raw = new TestProcessor(0, 2, true);
    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 0, 2, true);

    REQUIRE(node->getProcessor() == raw);
}

TEST_CASE("PluginNode getPluginInstance returns nullptr for non-plugin processor")
{
    auto* raw = new TestProcessor(0, 2, true);
    auto node = std::make_unique<PluginNode>(
        std::unique_ptr<juce::AudioProcessor>(raw), 0, 2, true);

    // TestProcessor is not an AudioPluginInstance, so should return nullptr
    REQUIRE(node->getPluginInstance() == nullptr);
}
