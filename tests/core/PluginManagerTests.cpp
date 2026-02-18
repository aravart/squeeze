#include <catch2/catch_test_macros.hpp>
#include "core/PluginManager.h"

#include <string>

using namespace squeeze;

// ═══════════════════════════════════════════════════════════════════
// Sample XML for testing (mimics JUCE KnownPluginList format)
// ═══════════════════════════════════════════════════════════════════

static const char* kValidXml = R"(
<KNOWNPLUGINS>
  <PLUGIN name="Synth A" descriptiveName="Synth A"
          format="VST3" category="Instrument" manufacturer="TestCo"
          version="1.0" file="/path/to/SynthA.vst3"
          uid="1234" isInstrument="1"
          numInputs="0" numOutputs="2"
          pluginFormatName="VST3"/>
  <PLUGIN name="Effect B" descriptiveName="Effect B"
          format="VST3" category="Effect" manufacturer="TestCo"
          version="1.0" file="/path/to/EffectB.vst3"
          uid="5678" isInstrument="0"
          numInputs="2" numOutputs="2"
          pluginFormatName="VST3"/>
  <PLUGIN name="Compressor C" descriptiveName="Compressor C"
          format="VST3" category="Effect" manufacturer="TestCo"
          version="1.0" file="/path/to/CompressorC.vst3"
          uid="9012" isInstrument="0"
          numInputs="2" numOutputs="2"
          pluginFormatName="VST3"/>
</KNOWNPLUGINS>
)";

// ═══════════════════════════════════════════════════════════════════
// Initial state
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PluginManager getNumPlugins returns 0 before loading")
{
    PluginManager pm;
    CHECK(pm.getNumPlugins() == 0);
}

TEST_CASE("PluginManager getAvailablePlugins returns empty before loading")
{
    PluginManager pm;
    CHECK(pm.getAvailablePlugins().empty());
}

// ═══════════════════════════════════════════════════════════════════
// Cache loading — loadCacheFromString
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PluginManager loadCacheFromString with valid XML succeeds")
{
    PluginManager pm;
    std::string error;
    REQUIRE(pm.loadCacheFromString(kValidXml, error));
    CHECK(pm.getNumPlugins() == 3);
}

TEST_CASE("PluginManager loadCacheFromString with empty string returns false")
{
    PluginManager pm;
    std::string error;
    REQUIRE_FALSE(pm.loadCacheFromString("", error));
    CHECK_FALSE(error.empty());
    CHECK(pm.getNumPlugins() == 0);
}

TEST_CASE("PluginManager loadCacheFromString with malformed XML returns false")
{
    PluginManager pm;
    std::string error;
    REQUIRE_FALSE(pm.loadCacheFromString("<not closed", error));
    CHECK_FALSE(error.empty());
    CHECK(pm.getNumPlugins() == 0);
}

TEST_CASE("PluginManager loadCacheFromString with valid XML but no plugins returns false")
{
    PluginManager pm;
    std::string error;
    REQUIRE_FALSE(pm.loadCacheFromString("<KNOWNPLUGINS></KNOWNPLUGINS>", error));
    CHECK_FALSE(error.empty());
    CHECK(pm.getNumPlugins() == 0);
}

// ═══════════════════════════════════════════════════════════════════
// Cache loading — loadCache (file)
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PluginManager loadCache with nonexistent file returns false")
{
    PluginManager pm;
    std::string error;
    REQUIRE_FALSE(pm.loadCache("/no/such/file.xml", error));
    CHECK_FALSE(error.empty());
    CHECK(pm.getNumPlugins() == 0);
}

// ═══════════════════════════════════════════════════════════════════
// Multiple loads replace previous data
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PluginManager multiple loads replace previous data")
{
    PluginManager pm;
    std::string error;

    REQUIRE(pm.loadCacheFromString(kValidXml, error));
    CHECK(pm.getNumPlugins() == 3);

    // Load a smaller cache — should replace
    const char* smallXml = R"(
    <KNOWNPLUGINS>
      <PLUGIN name="Only One" descriptiveName="Only One"
              format="VST3" category="Effect" manufacturer="TestCo"
              version="1.0" file="/path/to/OnlyOne.vst3"
              uid="1111" isInstrument="0"
              numInputs="2" numOutputs="2"
              pluginFormatName="VST3"/>
    </KNOWNPLUGINS>
    )";

    REQUIRE(pm.loadCacheFromString(smallXml, error));
    CHECK(pm.getNumPlugins() == 1);
}

// ═══════════════════════════════════════════════════════════════════
// Lookup — findByName
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PluginManager findByName returns correct description")
{
    PluginManager pm;
    std::string error;
    REQUIRE(pm.loadCacheFromString(kValidXml, error));

    auto* desc = pm.findByName("Effect B");
    REQUIRE(desc != nullptr);
    CHECK(desc->name.toStdString() == "Effect B");
}

TEST_CASE("PluginManager findByName returns nullptr for unknown name")
{
    PluginManager pm;
    std::string error;
    REQUIRE(pm.loadCacheFromString(kValidXml, error));

    CHECK(pm.findByName("Nonexistent Plugin") == nullptr);
}

TEST_CASE("PluginManager findByName is case-sensitive")
{
    PluginManager pm;
    std::string error;
    REQUIRE(pm.loadCacheFromString(kValidXml, error));

    CHECK(pm.findByName("synth a") == nullptr);
    CHECK(pm.findByName("Synth A") != nullptr);
}

// ═══════════════════════════════════════════════════════════════════
// Lookup — getAvailablePlugins
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PluginManager getAvailablePlugins returns sorted names")
{
    PluginManager pm;
    std::string error;
    REQUIRE(pm.loadCacheFromString(kValidXml, error));

    auto names = pm.getAvailablePlugins();
    REQUIRE(names.size() == 3);
    CHECK(names[0] == "Compressor C");
    CHECK(names[1] == "Effect B");
    CHECK(names[2] == "Synth A");
}

// ═══════════════════════════════════════════════════════════════════
// Instantiation — error paths
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("PluginManager createProcessor with unknown name returns nullptr")
{
    PluginManager pm;
    std::string error;
    REQUIRE(pm.loadCacheFromString(kValidXml, error));

    auto proc = pm.createProcessor("Nonexistent", 44100.0, 512, error);
    CHECK(proc == nullptr);
    CHECK_FALSE(error.empty());
}

TEST_CASE("PluginManager createProcessor with sampleRate 0 returns nullptr")
{
    PluginManager pm;
    std::string error;
    REQUIRE(pm.loadCacheFromString(kValidXml, error));

    auto proc = pm.createProcessor("Synth A", 0.0, 512, error);
    CHECK(proc == nullptr);
    CHECK_FALSE(error.empty());
}

TEST_CASE("PluginManager createProcessor with blockSize 0 returns nullptr")
{
    PluginManager pm;
    std::string error;
    REQUIRE(pm.loadCacheFromString(kValidXml, error));

    auto proc = pm.createProcessor("Synth A", 44100.0, 0, error);
    CHECK(proc == nullptr);
    CHECK_FALSE(error.empty());
}

TEST_CASE("PluginManager createProcessor with negative sampleRate returns nullptr")
{
    PluginManager pm;
    std::string error;
    REQUIRE(pm.loadCacheFromString(kValidXml, error));

    auto proc = pm.createProcessor("Synth A", -1.0, 512, error);
    CHECK(proc == nullptr);
    CHECK_FALSE(error.empty());
}
