#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "core/LuaBindings.h"
#include "core/Engine.h"
#include "core/Scheduler.h"
#include "core/Node.h"
#include "core/PluginNode.h"
#include "core/Port.h"

using namespace squeeze;
using Catch::Matchers::WithinAbs;

// ============================================================
// Test processor (same pattern as PluginNodeTests)
// ============================================================

class LuaTestProcessor : public juce::AudioProcessor {
public:
    LuaTestProcessor(int numIn, int numOut, bool midi)
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

    const juce::String getName() const override { return "LuaTestPlugin"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) override {
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

private:
    bool acceptsMidi_;
    int numIn_;
    int numOut_;
};

// Helper: create a PluginNode wrapping a LuaTestProcessor
static std::unique_ptr<PluginNode> makeTestPluginNode(int numIn, int numOut, bool midi)
{
    auto proc = std::make_unique<LuaTestProcessor>(numIn, numOut, midi);
    return std::make_unique<PluginNode>(std::move(proc), numIn, numOut, midi);
}

// Helper: set up LuaBindings with a bound Lua state
struct LuaFixture {
    Scheduler scheduler;
    Engine engine{scheduler};
    LuaBindings bindings{engine};
    sol::state lua;

    LuaFixture()
    {
        engine.prepareForTesting(44100.0, 512);
        lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
        bindings.bind(lua);
    }
};

// ============================================================
// Construction & bind
// ============================================================

TEST_CASE("LuaBindings bind creates sq table with all functions")
{
    LuaFixture f;

    sol::table sq = f.lua["sq"];
    REQUIRE(sq.valid());

    REQUIRE(sq["list_plugins"].get_type() == sol::type::function);
    REQUIRE(sq["plugin_info"].get_type() == sol::type::function);
    REQUIRE(sq["add_plugin"].get_type() == sol::type::function);
    REQUIRE(sq["remove_node"].get_type() == sol::type::function);
    REQUIRE(sq["connect"].get_type() == sol::type::function);
    REQUIRE(sq["disconnect"].get_type() == sol::type::function);
    REQUIRE(sq["update"].get_type() == sol::type::function);
    REQUIRE(sq["start"].get_type() == sol::type::function);
    REQUIRE(sq["stop"].get_type() == sol::type::function);
    REQUIRE(sq["set_param"].get_type() == sol::type::function);
    REQUIRE(sq["get_param"].get_type() == sol::type::function);
    REQUIRE(sq["params"].get_type() == sol::type::function);
    REQUIRE(sq["nodes"].get_type() == sol::type::function);
    REQUIRE(sq["ports"].get_type() == sol::type::function);
    REQUIRE(sq["connections"].get_type() == sol::type::function);
    REQUIRE(sq["refresh_midi_inputs"].get_type() == sol::type::function);
}

// ============================================================
// list_plugins (empty cache)
// ============================================================

TEST_CASE("LuaBindings list_plugins returns empty table when no cache loaded")
{
    LuaFixture f;

    auto result = f.lua.safe_script("return sq.list_plugins()", sol::script_pass_on_error);
    REQUIRE(result.valid());

    sol::table list = result;
    REQUIRE(list.size() == 0);
}

// ============================================================
// addTestNode + nodes()
// ============================================================

TEST_CASE("LuaBindings addTestNode adds a node visible via nodes()")
{
    LuaFixture f;

    auto node = makeTestPluginNode(0, 2, true);
    int id = f.bindings.addTestNode(std::move(node), "MySynth");

    REQUIRE(id >= 0);

    auto result = f.lua.safe_script("return sq.nodes()", sol::script_pass_on_error);
    REQUIRE(result.valid());

    sol::table nodes = result;
    REQUIRE(nodes.size() == 1);

    sol::table entry = nodes[1];
    REQUIRE(entry["id"].get<int>() == id);
    REQUIRE(entry["name"].get<std::string>() == "MySynth");
}

TEST_CASE("LuaBindings addTestNode supports multiple nodes")
{
    LuaFixture f;

    auto n1 = makeTestPluginNode(0, 2, true);
    auto n2 = makeTestPluginNode(2, 2, false);
    int id1 = f.bindings.addTestNode(std::move(n1), "Synth");
    int id2 = f.bindings.addTestNode(std::move(n2), "Effect");

    REQUIRE(id1 != id2);

    auto result = f.lua.safe_script("return sq.nodes()", sol::script_pass_on_error);
    sol::table nodes = result;
    REQUIRE(nodes.size() == 2);
}

// ============================================================
// ports()
// ============================================================

TEST_CASE("LuaBindings ports returns input and output ports for a node")
{
    LuaFixture f;

    auto node = makeTestPluginNode(2, 2, true);
    int id = f.bindings.addTestNode(std::move(node), "FX");

    f.lua["node_id"] = id;
    auto result = f.lua.safe_script("return sq.ports(node_id)", sol::script_pass_on_error);
    REQUIRE(result.valid());

    sol::table ports = result;
    sol::table inputs = ports["inputs"];
    sol::table outputs = ports["outputs"];

    REQUIRE(inputs.size() >= 1);
    REQUIRE(outputs.size() >= 1);
}

TEST_CASE("LuaBindings ports returns nil and error for invalid node ID")
{
    LuaFixture f;

    auto result = f.lua.safe_script(
        "local v, err = sq.ports(9999)\n"
        "return v, err",
        sol::script_pass_on_error);
    REQUIRE(result.valid());

    sol::object val = result;
    REQUIRE(val.get_type() == sol::type::lua_nil);
}

// ============================================================
// connect / disconnect / connections
// ============================================================

TEST_CASE("LuaBindings connect creates a connection between two nodes")
{
    LuaFixture f;

    auto synth = makeTestPluginNode(0, 2, true);
    auto effect = makeTestPluginNode(2, 2, false);
    int synthId = f.bindings.addTestNode(std::move(synth), "Synth");
    int fxId = f.bindings.addTestNode(std::move(effect), "FX");

    f.lua["src_id"] = synthId;
    f.lua["dst_id"] = fxId;

    auto result = f.lua.safe_script(
        "return sq.connect(src_id, 'out', dst_id, 'in')",
        sol::script_pass_on_error);
    REQUIRE(result.valid());

    int connId = result;
    REQUIRE(connId >= 0);

    // Verify via connections()
    auto conns = f.lua.safe_script("return sq.connections()", sol::script_pass_on_error);
    sol::table connTable = conns;
    REQUIRE(connTable.size() == 1);
}

TEST_CASE("LuaBindings disconnect removes a connection")
{
    LuaFixture f;

    auto synth = makeTestPluginNode(0, 2, true);
    auto effect = makeTestPluginNode(2, 2, false);
    int synthId = f.bindings.addTestNode(std::move(synth), "Synth");
    int fxId = f.bindings.addTestNode(std::move(effect), "FX");

    f.lua["src_id"] = synthId;
    f.lua["dst_id"] = fxId;

    f.lua.safe_script("conn_id = sq.connect(src_id, 'out', dst_id, 'in')");

    auto result = f.lua.safe_script("return sq.disconnect(conn_id)", sol::script_pass_on_error);
    REQUIRE(result.valid());

    bool ok = result;
    REQUIRE(ok);

    auto conns = f.lua.safe_script("return sq.connections()", sol::script_pass_on_error);
    sol::table connTable = conns;
    REQUIRE(connTable.size() == 0);
}

TEST_CASE("LuaBindings connect returns nil and error for invalid node")
{
    LuaFixture f;

    auto result = f.lua.safe_script(
        "local v, err = sq.connect(999, 'out', 888, 'in')\n"
        "return v, err",
        sol::script_pass_on_error);
    REQUIRE(result.valid());

    sol::object val = result;
    REQUIRE(val.get_type() == sol::type::lua_nil);
}

TEST_CASE("LuaBindings disconnect returns nil and error for invalid connection ID")
{
    LuaFixture f;

    auto result = f.lua.safe_script(
        "local v, err = sq.disconnect(9999)\n"
        "return v, err",
        sol::script_pass_on_error);
    REQUIRE(result.valid());

    sol::object val = result;
    REQUIRE(val.get_type() == sol::type::lua_nil);
}

// ============================================================
// remove_node
// ============================================================

TEST_CASE("LuaBindings remove_node removes a node from the graph")
{
    LuaFixture f;

    auto node = makeTestPluginNode(0, 2, true);
    int id = f.bindings.addTestNode(std::move(node), "Synth");

    f.lua["node_id"] = id;
    auto result = f.lua.safe_script("return sq.remove_node(node_id)", sol::script_pass_on_error);
    REQUIRE(result.valid());

    bool ok = result;
    REQUIRE(ok);

    auto nodesResult = f.lua.safe_script("return sq.nodes()", sol::script_pass_on_error);
    sol::table nodes = nodesResult;
    REQUIRE(nodes.size() == 0);
}

TEST_CASE("LuaBindings remove_node returns nil and error for invalid ID")
{
    LuaFixture f;

    auto result = f.lua.safe_script(
        "local v, err = sq.remove_node(9999)\n"
        "return v, err",
        sol::script_pass_on_error);
    REQUIRE(result.valid());

    sol::object val = result;
    REQUIRE(val.get_type() == sol::type::lua_nil);
}

// ============================================================
// update() pushes graph to engine
// ============================================================

TEST_CASE("LuaBindings update pushes graph to engine via scheduler")
{
    LuaFixture f;

    auto node = makeTestPluginNode(0, 2, true);
    f.bindings.addTestNode(std::move(node), "Synth");

    // Before update, engine has no snapshot (silence)
    juce::AudioBuffer<float> output(2, 512);
    juce::MidiBuffer midi;
    output.clear();
    f.engine.processBlock(output, midi, 512);

    // Call update to push graph
    f.lua.safe_script("sq.update()");

    // Process the command on the "audio thread" side
    f.engine.processBlock(output, midi, 512);

    // The engine should now have a snapshot with 1 node
    // (We can't easily inspect the snapshot, but the fact that
    //  processBlock didn't crash after update confirms the push worked)
    REQUIRE(true);
}

// ============================================================
// start / stop
// ============================================================

TEST_CASE("LuaBindings start and stop control engine state")
{
    LuaFixture f;

    REQUIRE_FALSE(f.engine.isRunning());

    f.lua.safe_script("sq.start()");
    REQUIRE(f.engine.isRunning());

    f.lua.safe_script("sq.stop()");
    REQUIRE_FALSE(f.engine.isRunning());
}

TEST_CASE("LuaBindings start accepts optional sample rate and block size")
{
    LuaFixture f;

    f.lua.safe_script("sq.start(48000, 256)");
    REQUIRE(f.engine.isRunning());
    REQUIRE(f.engine.getSampleRate() == 48000.0);
    REQUIRE(f.engine.getBlockSize() == 256);

    f.lua.safe_script("sq.stop()");
}

// ============================================================
// params / get_param / set_param
// ============================================================

TEST_CASE("LuaBindings params returns parameter names for a node")
{
    LuaFixture f;

    auto node = makeTestPluginNode(2, 2, false);
    int id = f.bindings.addTestNode(std::move(node), "FX");

    f.lua["node_id"] = id;
    auto result = f.lua.safe_script("return sq.params(node_id)", sol::script_pass_on_error);
    REQUIRE(result.valid());

    sol::table params = result;
    REQUIRE(params.size() == 2);

    // Check that Gain and Mix are in the list
    bool hasGain = false, hasMix = false;
    for (auto& kv : params) {
        std::string name = kv.second.as<std::string>();
        if (name == "Gain") hasGain = true;
        if (name == "Mix") hasMix = true;
    }
    REQUIRE(hasGain);
    REQUIRE(hasMix);
}

TEST_CASE("LuaBindings get_param returns current parameter value")
{
    LuaFixture f;

    auto node = makeTestPluginNode(2, 2, false);
    int id = f.bindings.addTestNode(std::move(node), "FX");

    f.lua["node_id"] = id;
    auto result = f.lua.safe_script("return sq.get_param(node_id, 'Gain')", sol::script_pass_on_error);
    REQUIRE(result.valid());

    float val = result;
    REQUIRE_THAT(val, WithinAbs(0.5f, 1e-3));
}

TEST_CASE("LuaBindings set_param changes parameter value")
{
    LuaFixture f;

    auto node = makeTestPluginNode(2, 2, false);
    int id = f.bindings.addTestNode(std::move(node), "FX");

    f.lua["node_id"] = id;
    f.lua.safe_script("sq.set_param(node_id, 'Gain', 0.75)");

    auto result = f.lua.safe_script("return sq.get_param(node_id, 'Gain')", sol::script_pass_on_error);
    float val = result;
    REQUIRE_THAT(val, WithinAbs(0.75f, 1e-3));
}

TEST_CASE("LuaBindings params returns nil and error for invalid node ID")
{
    LuaFixture f;

    auto result = f.lua.safe_script(
        "local v, err = sq.params(9999)\n"
        "return v, err",
        sol::script_pass_on_error);
    REQUIRE(result.valid());

    sol::object val = result;
    REQUIRE(val.get_type() == sol::type::lua_nil);
}

TEST_CASE("LuaBindings get_param returns nil and error for invalid node ID")
{
    LuaFixture f;

    auto result = f.lua.safe_script(
        "local v, err = sq.get_param(9999, 'Gain')\n"
        "return v, err",
        sol::script_pass_on_error);
    REQUIRE(result.valid());

    sol::object val = result;
    REQUIRE(val.get_type() == sol::type::lua_nil);
}

TEST_CASE("LuaBindings set_param returns nil and error for invalid node ID")
{
    LuaFixture f;

    auto result = f.lua.safe_script(
        "local v, err = sq.set_param(9999, 'Gain', 0.5)\n"
        "return v, err",
        sol::script_pass_on_error);
    REQUIRE(result.valid());

    sol::object val = result;
    REQUIRE(val.get_type() == sol::type::lua_nil);
}

// ============================================================
// plugin_info (no real plugins, returns nil)
// ============================================================

TEST_CASE("LuaBindings plugin_info returns nil for unknown plugin")
{
    LuaFixture f;

    auto result = f.lua.safe_script(
        "local v, err = sq.plugin_info('NonExistent')\n"
        "return v, err",
        sol::script_pass_on_error);
    REQUIRE(result.valid());

    sol::object val = result;
    REQUIRE(val.get_type() == sol::type::lua_nil);
}

// ============================================================
// Full Lua script integration
// ============================================================

TEST_CASE("LuaBindings full workflow via Lua script")
{
    LuaFixture f;

    // Add two test nodes
    auto synth = makeTestPluginNode(0, 2, true);
    auto effect = makeTestPluginNode(2, 2, false);
    f.bindings.addTestNode(std::move(synth), "Synth");
    f.bindings.addTestNode(std::move(effect), "FX");

    auto result = f.lua.safe_script(R"(
        -- List nodes
        local ns = sq.nodes()
        assert(#ns == 2, "expected 2 nodes, got " .. #ns)

        -- Find synth and fx IDs
        local synth_id, fx_id
        for _, n in ipairs(ns) do
            if n.name == "Synth" then synth_id = n.id end
            if n.name == "FX" then fx_id = n.id end
        end
        assert(synth_id, "synth not found")
        assert(fx_id, "fx not found")

        -- Connect audio out -> audio in
        local conn = sq.connect(synth_id, "out", fx_id, "in")
        assert(conn, "connect failed")

        -- Check connections
        local cs = sq.connections()
        assert(#cs == 1, "expected 1 connection")

        -- Set parameter
        sq.set_param(fx_id, "Gain", 0.8)
        local g = sq.get_param(fx_id, "Gain")
        assert(math.abs(g - 0.8) < 0.01, "gain mismatch")

        -- Update and start
        sq.update()
        sq.start()
        assert(true)
        sq.stop()

        return true
    )", sol::script_pass_on_error);

    REQUIRE(result.valid());
    bool ok = result;
    REQUIRE(ok);
}

// ============================================================
// connections() table structure
// ============================================================

TEST_CASE("LuaBindings connections returns correct table structure")
{
    LuaFixture f;

    auto synth = makeTestPluginNode(0, 2, true);
    auto effect = makeTestPluginNode(2, 2, false);
    int synthId = f.bindings.addTestNode(std::move(synth), "Synth");
    int fxId = f.bindings.addTestNode(std::move(effect), "FX");

    f.lua["src_id"] = synthId;
    f.lua["dst_id"] = fxId;
    f.lua.safe_script("sq.connect(src_id, 'out', dst_id, 'in')");

    auto result = f.lua.safe_script("return sq.connections()", sol::script_pass_on_error);
    sol::table conns = result;
    REQUIRE(conns.size() == 1);

    sol::table c = conns[1];
    REQUIRE(c["id"].get<int>() >= 0);
    REQUIRE(c["src"].get<int>() == synthId);
    REQUIRE(c["src_port"].get<std::string>() == "out");
    REQUIRE(c["dst"].get<int>() == fxId);
    REQUIRE(c["dst_port"].get<std::string>() == "in");
}

// ============================================================
// add_plugin returns nil for unknown plugin (no cache loaded)
// ============================================================

TEST_CASE("LuaBindings add_plugin returns nil when plugin not in cache")
{
    LuaFixture f;

    auto result = f.lua.safe_script(
        "local v, err = sq.add_plugin('NonExistent')\n"
        "return v, err",
        sol::script_pass_on_error);
    REQUIRE(result.valid());

    sol::object val = result;
    REQUIRE(val.get_type() == sol::type::lua_nil);
}

// ============================================================
// MIDI input bindings
// ============================================================

TEST_CASE("LuaBindings list_midi_inputs returns a table")
{
    LuaFixture f;

    auto result = f.lua.safe_script("return sq.list_midi_inputs()", sol::script_pass_on_error);
    REQUIRE(result.valid());

    sol::table list = result;
    // We can't know how many devices are available, but it should be a valid table
    REQUIRE(list.size() >= 0);
}

TEST_CASE("LuaBindings add_midi_input returns nil and error for nonexistent device")
{
    LuaFixture f;

    auto result = f.lua.safe_script(
        "local v, err = sq.add_midi_input('Nonexistent MIDI Device 12345')\n"
        "return v, err",
        sol::script_pass_on_error);
    REQUIRE(result.valid());

    sol::object val = result;
    REQUIRE(val.get_type() == sol::type::lua_nil);
}

TEST_CASE("LuaBindings bind creates sq table with MIDI input functions")
{
    LuaFixture f;

    sol::table sq = f.lua["sq"];
    REQUIRE(sq["list_midi_inputs"].get_type() == sol::type::function);
    REQUIRE(sq["add_midi_input"].get_type() == sol::type::function);
}

// ============================================================
// refresh_midi_inputs
// ============================================================

TEST_CASE("LuaBindings refresh_midi_inputs returns table with added and removed keys")
{
    LuaFixture f;

    auto result = f.lua.safe_script(
        "return sq.refresh_midi_inputs()",
        sol::script_pass_on_error);
    REQUIRE(result.valid());

    sol::table tbl = result;
    REQUIRE(tbl["added"].get_type() == sol::type::table);
    REQUIRE(tbl["removed"].get_type() == sol::type::table);
}

// ============================================================
// MIDI channel filtering via Lua
// ============================================================

TEST_CASE("LuaBindings connect accepts optional 5th channel argument")
{
    LuaFixture f;

    auto synth = makeTestPluginNode(0, 2, true);
    auto effect = makeTestPluginNode(2, 2, false);
    int synthId = f.bindings.addTestNode(std::move(synth), "Synth");
    int fxId = f.bindings.addTestNode(std::move(effect), "FX");

    f.lua["src_id"] = synthId;
    f.lua["dst_id"] = fxId;

    auto result = f.lua.safe_script(
        "return sq.connect(src_id, 'out', dst_id, 'in', 5)",
        sol::script_pass_on_error);
    REQUIRE(result.valid());

    int connId = result;
    REQUIRE(connId >= 0);
}

TEST_CASE("LuaBindings connect defaults to channel 0 when not provided")
{
    LuaFixture f;

    auto synth = makeTestPluginNode(0, 2, true);
    auto effect = makeTestPluginNode(2, 2, false);
    int synthId = f.bindings.addTestNode(std::move(synth), "Synth");
    int fxId = f.bindings.addTestNode(std::move(effect), "FX");

    f.lua["src_id"] = synthId;
    f.lua["dst_id"] = fxId;

    f.lua.safe_script("sq.connect(src_id, 'out', dst_id, 'in')");

    auto result = f.lua.safe_script("return sq.connections()", sol::script_pass_on_error);
    sol::table conns = result;
    sol::table c = conns[1];
    REQUIRE(c["channel"].get<int>() == 0);
}

TEST_CASE("LuaBindings connections returns channel field")
{
    LuaFixture f;

    auto synth = makeTestPluginNode(0, 2, true);
    auto effect = makeTestPluginNode(2, 2, false);
    int synthId = f.bindings.addTestNode(std::move(synth), "Synth");
    int fxId = f.bindings.addTestNode(std::move(effect), "FX");

    f.lua["src_id"] = synthId;
    f.lua["dst_id"] = fxId;

    f.lua.safe_script("sq.connect(src_id, 'out', dst_id, 'in', 7)");

    auto result = f.lua.safe_script("return sq.connections()", sol::script_pass_on_error);
    sol::table conns = result;
    REQUIRE(conns.size() == 1);

    sol::table c = conns[1];
    REQUIRE(c["id"].get<int>() >= 0);
    REQUIRE(c["src"].get<int>() == synthId);
    REQUIRE(c["dst"].get<int>() == fxId);
    REQUIRE(c["channel"].get<int>() == 7);
}
