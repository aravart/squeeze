#pragma once

#include "core/Engine.h"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <memory>
#include <string>

namespace squeeze {

class LuaBindings {
public:
    explicit LuaBindings(Engine& engine);

    /// Register the "sq" table and all API functions into the Lua state.
    void bind(sol::state& lua);

    /// Add a pre-built node (for testing without real plugins).
    /// Returns the graph node ID.
    int addTestNode(std::unique_ptr<Node> node, const std::string& name = "test");

private:
    // Lua API implementations (thin delegates to engine_)
    sol::table luaListPlugins(sol::state_view lua);
    std::tuple<sol::object, sol::object> luaPluginInfo(sol::state_view lua, const std::string& name);
    std::tuple<sol::object, sol::object> luaAddPlugin(sol::state_view lua, const std::string& name);
    std::tuple<sol::object, sol::object> luaRemoveNode(sol::state_view lua, int id);
    std::tuple<sol::object, sol::object> luaConnect(sol::state_view lua,
        int srcId, const std::string& srcPort, int dstId, const std::string& dstPort,
        int midiChannel);
    std::tuple<sol::object, sol::object> luaDisconnect(sol::state_view lua, int connId);
    void luaUpdate();
    void luaStart(sol::optional<double> sr, sol::optional<int> bs);
    void luaStop();
    std::tuple<sol::object, sol::object> luaSetParam(sol::state_view lua,
        int nodeId, const std::string& name, float value);
    std::tuple<sol::object, sol::object> luaGetParam(sol::state_view lua,
        int nodeId, const std::string& name);
    std::tuple<sol::object, sol::object> luaParams(sol::state_view lua, int nodeId);
    std::tuple<sol::object, sol::object> luaParamInfo(sol::state_view lua, int nodeId);
    std::tuple<sol::object, sol::object> luaParamText(sol::state_view lua,
        int nodeId, sol::object nameOrIndex);
    std::tuple<sol::object, sol::object> luaSetParamI(sol::state_view lua,
        int nodeId, int index, float value);
    std::tuple<sol::object, sol::object> luaGetParamI(sol::state_view lua,
        int nodeId, int index);
    sol::table luaNodes(sol::state_view lua);
    std::tuple<sol::object, sol::object> luaPorts(sol::state_view lua, int nodeId);
    sol::table luaConnections(sol::state_view lua);
    sol::table luaListMidiInputs(sol::state_view lua);
    std::tuple<sol::object, sol::object> luaAddMidiInput(sol::state_view lua, const std::string& name);
    sol::table luaRefreshMidiInputs(sol::state_view lua);

    // Performance monitoring API
    sol::table luaPerf(sol::state_view lua);
    void luaPerfNodes(bool enable);
    void luaPerfReset();

    // Sampler API
    std::tuple<sol::object, sol::object> luaAddSampler(sol::state_view lua,
        const std::string& name, int maxVoices);
    std::tuple<sol::object, sol::object> luaSetSamplerBuffer(sol::state_view lua,
        int nodeId, int bufferId);

    // Buffer API
    std::tuple<sol::object, sol::object> luaLoadBuffer(sol::state_view lua, const std::string& filePath);
    std::tuple<sol::object, sol::object> luaCreateBuffer(sol::state_view lua,
        int numChannels, int lengthInSamples, double sampleRate, const std::string& name);
    std::tuple<sol::object, sol::object> luaRemoveBuffer(sol::state_view lua, int id);
    sol::table luaBuffers(sol::state_view lua);
    std::tuple<sol::object, sol::object> luaBufferInfo(sol::state_view lua, int id);

    Engine& engine_;
};

} // namespace squeeze
