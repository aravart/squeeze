#include "core/LuaBindings.h"
#include "core/Logger.h"

#include <chrono>
#include <thread>

namespace squeeze {

LuaBindings::LuaBindings(Engine& engine)
    : engine_(engine)
{
}

int LuaBindings::addTestNode(std::unique_ptr<Node> node, const std::string& name)
{
    return engine_.addNode(std::move(node), name);
}

void LuaBindings::bind(sol::state& lua)
{
    sol::table sq = lua.create_named_table("sq");

    sq.set_function("list_plugins", [this](sol::this_state s) {
        return luaListPlugins(sol::state_view(s));
    });

    sq.set_function("plugin_info", [this](sol::this_state s, const std::string& name) {
        return luaPluginInfo(sol::state_view(s), name);
    });

    sq.set_function("add_plugin", [this](sol::this_state s, const std::string& name) {
        return luaAddPlugin(sol::state_view(s), name);
    });

    sq.set_function("remove_node", [this](sol::this_state s, int id) {
        return luaRemoveNode(sol::state_view(s), id);
    });

    sq.set_function("connect", [this](sol::this_state s,
            int srcId, const std::string& srcPort,
            int dstId, const std::string& dstPort) {
        return luaConnect(sol::state_view(s), srcId, srcPort, dstId, dstPort);
    });

    sq.set_function("disconnect", [this](sol::this_state s, int connId) {
        return luaDisconnect(sol::state_view(s), connId);
    });

    sq.set_function("update", [this]() {
        luaUpdate();
    });

    sq.set_function("start", [this](sol::optional<double> sr, sol::optional<int> bs) {
        luaStart(sr, bs);
    });

    sq.set_function("engine_stop", [this]() {
        luaStop();
    });

    sq.set_function("set_param", [this](sol::this_state s,
            int nodeId, const std::string& name, float value) {
        return luaSetParam(sol::state_view(s), nodeId, name, value);
    });

    sq.set_function("get_param", [this](sol::this_state s,
            int nodeId, const std::string& name) {
        return luaGetParam(sol::state_view(s), nodeId, name);
    });

    sq.set_function("params", [this](sol::this_state s, int nodeId) {
        return luaParams(sol::state_view(s), nodeId);
    });

    sq.set_function("nodes", [this](sol::this_state s) {
        return luaNodes(sol::state_view(s));
    });

    sq.set_function("ports", [this](sol::this_state s, int nodeId) {
        return luaPorts(sol::state_view(s), nodeId);
    });

    sq.set_function("connections", [this](sol::this_state s) {
        return luaConnections(sol::state_view(s));
    });

    // MIDI routing API
    sq.set_function("list_midi_devices", [this](sol::this_state s) {
        return luaListMidiDevices(sol::state_view(s));
    });

    sq.set_function("open_midi_devices", [this](sol::this_state s) {
        return luaOpenMidiDevices(sol::state_view(s));
    });

    sq.set_function("midi_route", [this](sol::this_state s,
            const std::string& deviceName, int nodeId,
            sol::optional<sol::table> opts) {
        int channel = 0;
        if (opts)
        {
            sol::optional<int> ch = (*opts)["channel"];
            if (ch) channel = *ch;
        }
        return luaMidiRoute(sol::state_view(s), deviceName, nodeId, channel);
    });

    sq.set_function("midi_unroute", [this](sol::this_state s, int routeId) {
        return luaMidiUnroute(sol::state_view(s), routeId);
    });

    sq.set_function("midi_routes", [this](sol::this_state s) {
        return luaMidiRoutes(sol::state_view(s));
    });

    sq.set_function("add_sampler", [this](sol::this_state s,
            const std::string& name, sol::optional<int> maxVoices) {
        return luaAddSampler(sol::state_view(s), name, maxVoices.value_or(1));
    });

    sq.set_function("set_sampler_buffer", [this](sol::this_state s,
            int nodeId, int bufferId) {
        return luaSetSamplerBuffer(sol::state_view(s), nodeId, bufferId);
    });

    sq.set_function("load_buffer", [this](sol::this_state s, const std::string& filePath) {
        return luaLoadBuffer(sol::state_view(s), filePath);
    });

    sq.set_function("create_buffer", [this](sol::this_state s,
            int numChannels, int lengthInSamples, double sampleRate,
            sol::optional<std::string> name) {
        return luaCreateBuffer(sol::state_view(s), numChannels, lengthInSamples,
                               sampleRate, name.value_or(""));
    });

    sq.set_function("remove_buffer", [this](sol::this_state s, int id) {
        return luaRemoveBuffer(sol::state_view(s), id);
    });

    sq.set_function("buffers", [this](sol::this_state s) {
        return luaBuffers(sol::state_view(s));
    });

    sq.set_function("buffer_info", [this](sol::this_state s, int id) {
        return luaBufferInfo(sol::state_view(s), id);
    });

    sq.set_function("perf", [this](sol::this_state s) {
        return luaPerf(sol::state_view(s));
    });

    sq.set_function("perf_nodes", [this](bool enable) {
        luaPerfNodes(enable);
    });

    sq.set_function("perf_reset", [this]() {
        luaPerfReset();
    });

    sq.set_function("param_info", [this](sol::this_state s, int nodeId) {
        return luaParamInfo(sol::state_view(s), nodeId);
    });

    sq.set_function("param_text", [this](sol::this_state s, int nodeId, sol::object nameOrIndex) {
        return luaParamText(sol::state_view(s), nodeId, nameOrIndex);
    });

    sq.set_function("set_param_i", [this](sol::this_state s, int nodeId, int index, float value) {
        return luaSetParamI(sol::state_view(s), nodeId, index, value);
    });

    sq.set_function("get_param_i", [this](sol::this_state s, int nodeId, int index) {
        return luaGetParamI(sol::state_view(s), nodeId, index);
    });

    // Transport API
    sq.set_function("play", [this]() { luaPlay(); });
    sq.set_function("stop", [this]() { luaTransportStop(); });
    sq.set_function("pause", [this]() { luaPause(); });
    sq.set_function("set_tempo", [this](double bpm) { luaSetTempo(bpm); });
    sq.set_function("set_time_sig", [this](int num, int denom) { luaSetTimeSignature(num, denom); });
    sq.set_function("set_position_beats", [this](double beats) { luaSetPositionBeats(beats); });
    sq.set_function("set_position_samples", [this](int64_t samples) { luaSetPositionSamples(samples); });
    sq.set_function("set_loop", [this](double startBeats, double endBeats) { luaSetLoopPoints(startBeats, endBeats); });
    sq.set_function("set_looping", [this](bool enabled) { luaSetLooping(enabled); });
    sq.set_function("transport", [this](sol::this_state s) { return luaTransportInfo(sol::state_view(s)); });
}

// ============================================================
// Lua API implementations — thin delegates to Engine
// ============================================================

sol::table LuaBindings::luaListPlugins(sol::state_view lua)
{
    sol::table result = lua.create_table();
    auto names = engine_.getAvailablePluginNames();
    for (int i = 0; i < (int)names.size(); ++i)
        result[i + 1] = names[i];
    return result;
}

std::tuple<sol::object, sol::object> LuaBindings::luaPluginInfo(
    sol::state_view lua, const std::string& name)
{
    auto* desc = engine_.findPluginByName(name);
    if (!desc)
        return {sol::lua_nil, sol::make_object(lua, "Plugin '" + name + "' not found")};

    sol::table info = lua.create_table();
    info["name"] = desc->name.toStdString();
    info["format"] = desc->pluginFormatName.toStdString();
    info["inputs"] = desc->numInputChannels;
    info["outputs"] = desc->numOutputChannels;
    info["instrument"] = desc->isInstrument;

    return {sol::make_object(lua, info), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaAddPlugin(
    sol::state_view lua, const std::string& name)
{
    std::string errorMessage;
    int id = engine_.addPlugin(name, errorMessage);
    if (id < 0)
        return {sol::lua_nil, sol::make_object(lua, errorMessage)};

    return {sol::make_object(lua, id), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaRemoveNode(
    sol::state_view lua, int id)
{
    if (!engine_.removeNode(id))
        return {sol::lua_nil, sol::make_object(lua, "Node " + std::to_string(id) + " not found")};

    return {sol::make_object(lua, true), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaConnect(
    sol::state_view lua,
    int srcId, const std::string& srcPort,
    int dstId, const std::string& dstPort)
{
    std::string error;
    int connId = engine_.connect(srcId, srcPort, dstId, dstPort, error);
    if (connId < 0)
        return {sol::lua_nil, sol::make_object(lua, error)};

    return {sol::make_object(lua, connId), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaDisconnect(
    sol::state_view lua, int connId)
{
    if (!engine_.disconnect(connId))
        return {sol::lua_nil, sol::make_object(lua, "Connection " + std::to_string(connId) + " not found")};

    return {sol::make_object(lua, true), sol::lua_nil};
}

void LuaBindings::luaUpdate()
{
    SQ_LOG("luaUpdate");
    engine_.updateGraph();
}

void LuaBindings::luaStart(sol::optional<double> sr, sol::optional<int> bs)
{
    double sampleRate = sr.value_or(44100.0);
    int blockSize = bs.value_or(512);
    SQ_LOG("luaStart: sr=%.0f bs=%d", sampleRate, blockSize);
    engine_.start(sampleRate, blockSize);
}

void LuaBindings::luaStop()
{
    SQ_LOG("luaStop");
    engine_.stop();
}

std::tuple<sol::object, sol::object> LuaBindings::luaSetParam(
    sol::state_view lua, int nodeId, const std::string& name, float value)
{
    if (!engine_.setParameterByName(nodeId, name, value))
        return {sol::lua_nil, sol::make_object(lua, "Node " + std::to_string(nodeId) + " not found or unknown param")};

    return {sol::make_object(lua, true), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaGetParam(
    sol::state_view lua, int nodeId, const std::string& name)
{
    Node* node = engine_.getNode(nodeId);
    if (!node)
        return {sol::lua_nil, sol::make_object(lua, "Node " + std::to_string(nodeId) + " not found")};

    float val = engine_.getParameterByName(nodeId, name);
    return {sol::make_object(lua, val), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaParams(
    sol::state_view lua, int nodeId)
{
    Node* node = engine_.getNode(nodeId);
    if (!node)
        return {sol::lua_nil, sol::make_object(lua, "Node " + std::to_string(nodeId) + " not found")};

    auto descs = engine_.getParameterDescriptors(nodeId);
    sol::table result = lua.create_table();
    for (int i = 0; i < (int)descs.size(); ++i)
        result[i + 1] = descs[i].name;

    return {sol::make_object(lua, result), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaParamInfo(
    sol::state_view lua, int nodeId)
{
    Node* node = engine_.getNode(nodeId);
    if (!node)
        return {sol::lua_nil, sol::make_object(lua, "Node " + std::to_string(nodeId) + " not found")};

    auto descs = engine_.getParameterDescriptors(nodeId);
    sol::table result = lua.create_table();
    for (int i = 0; i < (int)descs.size(); ++i)
    {
        sol::table entry = lua.create_table();
        entry["name"] = descs[i].name;
        entry["index"] = descs[i].index;
        entry["default"] = descs[i].defaultValue;
        entry["steps"] = descs[i].numSteps;
        entry["automatable"] = descs[i].automatable;
        entry["boolean"] = descs[i].boolean;
        entry["label"] = descs[i].label;
        entry["group"] = descs[i].group;
        result[i + 1] = entry;
    }

    return {sol::make_object(lua, result), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaParamText(
    sol::state_view lua, int nodeId, sol::object nameOrIndex)
{
    Node* node = engine_.getNode(nodeId);
    if (!node)
        return {sol::lua_nil, sol::make_object(lua, "Node " + std::to_string(nodeId) + " not found")};

    int paramIndex = -1;
    if (nameOrIndex.is<int>())
    {
        paramIndex = nameOrIndex.as<int>();
    }
    else if (nameOrIndex.is<std::string>())
    {
        paramIndex = node->findParameterIndex(nameOrIndex.as<std::string>());
        if (paramIndex < 0)
            return {sol::lua_nil, sol::make_object(lua, "Unknown parameter: " + nameOrIndex.as<std::string>())};
    }
    else
    {
        return {sol::lua_nil, sol::make_object(lua, "Expected string or number")};
    }

    auto text = engine_.getParameterText(nodeId, paramIndex);
    return {sol::make_object(lua, text), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaSetParamI(
    sol::state_view lua, int nodeId, int index, float value)
{
    if (!engine_.setParameter(nodeId, index, value))
        return {sol::lua_nil, sol::make_object(lua, "Node " + std::to_string(nodeId) + " not found")};

    return {sol::make_object(lua, true), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaGetParamI(
    sol::state_view lua, int nodeId, int index)
{
    Node* node = engine_.getNode(nodeId);
    if (!node)
        return {sol::lua_nil, sol::make_object(lua, "Node " + std::to_string(nodeId) + " not found")};

    float val = engine_.getParameter(nodeId, index);
    return {sol::make_object(lua, val), sol::lua_nil};
}

sol::table LuaBindings::luaNodes(sol::state_view lua)
{
    sol::table result = lua.create_table();
    auto nodes = engine_.getNodes();
    int idx = 1;
    for (const auto& kv : nodes)
    {
        sol::table entry = lua.create_table();
        entry["id"] = kv.first;
        entry["name"] = kv.second;
        result[idx++] = entry;
    }
    return result;
}

std::tuple<sol::object, sol::object> LuaBindings::luaPorts(
    sol::state_view lua, int nodeId)
{
    Node* node = engine_.getNode(nodeId);
    if (!node)
        return {sol::lua_nil, sol::make_object(lua, "Node " + std::to_string(nodeId) + " not found")};

    sol::table result = lua.create_table();

    // Inputs
    sol::table inputs = lua.create_table();
    auto inPorts = node->getInputPorts();
    for (int i = 0; i < (int)inPorts.size(); ++i)
    {
        sol::table p = lua.create_table();
        p["name"] = inPorts[i].name;
        p["type"] = (inPorts[i].signalType == SignalType::audio) ? "audio" : "midi";
        p["channels"] = inPorts[i].channels;
        inputs[i + 1] = p;
    }
    result["inputs"] = inputs;

    // Outputs
    sol::table outputs = lua.create_table();
    auto outPorts = node->getOutputPorts();
    for (int i = 0; i < (int)outPorts.size(); ++i)
    {
        sol::table p = lua.create_table();
        p["name"] = outPorts[i].name;
        p["type"] = (outPorts[i].signalType == SignalType::audio) ? "audio" : "midi";
        p["channels"] = outPorts[i].channels;
        outputs[i + 1] = p;
    }
    result["outputs"] = outputs;

    return {sol::make_object(lua, result), sol::lua_nil};
}

sol::table LuaBindings::luaConnections(sol::state_view lua)
{
    sol::table result = lua.create_table();
    auto conns = engine_.getConnections();
    for (int i = 0; i < (int)conns.size(); ++i)
    {
        sol::table c = lua.create_table();
        c["id"] = conns[i].id;
        c["src"] = conns[i].source.nodeId;
        c["src_port"] = conns[i].source.portName;
        c["dst"] = conns[i].dest.nodeId;
        c["dst_port"] = conns[i].dest.portName;
        result[i + 1] = c;
    }
    return result;
}

// ============================================================
// MIDI routing API
// ============================================================

sol::table LuaBindings::luaListMidiDevices(sol::state_view lua)
{
    sol::table result = lua.create_table();
    auto devices = engine_.getMidiRouter().getAvailableDevices();
    for (int i = 0; i < (int)devices.size(); ++i)
        result[i + 1] = devices[i];
    return result;
}

sol::table LuaBindings::luaOpenMidiDevices(sol::state_view lua)
{
    sol::table result = lua.create_table();
    auto devices = engine_.getMidiRouter().getOpenDevices();
    for (int i = 0; i < (int)devices.size(); ++i)
        result[i + 1] = devices[i];
    return result;
}

std::tuple<sol::object, sol::object> LuaBindings::luaMidiRoute(
    sol::state_view lua, const std::string& deviceName, int nodeId, int channelFilter)
{
    auto& router = engine_.getMidiRouter();

    // Auto-open device if not already open
    if (!router.isDeviceOpen(deviceName))
    {
        std::string openError;
        if (!router.openDevice(deviceName, openError))
            return {sol::lua_nil, sol::make_object(lua, openError)};
    }

    std::string routeError;
    int routeId = router.addRoute(deviceName, nodeId, channelFilter, routeError);
    if (routeId < 0)
        return {sol::lua_nil, sol::make_object(lua, routeError)};

    router.commit();
    return {sol::make_object(lua, routeId), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaMidiUnroute(
    sol::state_view lua, int routeId)
{
    auto& router = engine_.getMidiRouter();
    if (!router.removeRoute(routeId))
        return {sol::lua_nil, sol::make_object(lua, "Route " + std::to_string(routeId) + " not found")};

    router.commit();
    return {sol::make_object(lua, true), sol::lua_nil};
}

sol::table LuaBindings::luaMidiRoutes(sol::state_view lua)
{
    sol::table result = lua.create_table();
    auto routes = engine_.getMidiRouter().getRoutes();
    for (int i = 0; i < (int)routes.size(); ++i)
    {
        sol::table r = lua.create_table();
        r["id"] = routes[i].id;
        r["device"] = routes[i].deviceName;
        r["node_id"] = routes[i].nodeId;
        r["channel"] = routes[i].channelFilter;
        result[i + 1] = r;
    }
    return result;
}

// ============================================================
// Sampler API
// ============================================================

std::tuple<sol::object, sol::object> LuaBindings::luaAddSampler(
    sol::state_view lua, const std::string& name, int maxVoices)
{
    std::string errorMessage;
    int id = engine_.addSampler(name, maxVoices, errorMessage);
    if (id < 0)
        return {sol::lua_nil, sol::make_object(lua, errorMessage)};

    return {sol::make_object(lua, id), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaSetSamplerBuffer(
    sol::state_view lua, int nodeId, int bufferId)
{
    if (!engine_.setSamplerBuffer(nodeId, bufferId))
        return {sol::lua_nil, sol::make_object(lua,
            "Invalid node " + std::to_string(nodeId) + " or buffer " + std::to_string(bufferId))};

    return {sol::make_object(lua, true), sol::lua_nil};
}

// ============================================================
// Buffer API
// ============================================================

std::tuple<sol::object, sol::object> LuaBindings::luaLoadBuffer(
    sol::state_view lua, const std::string& filePath)
{
    std::string errorMessage;
    int id = engine_.loadBuffer(filePath, errorMessage);
    if (id < 0)
        return {sol::lua_nil, sol::make_object(lua, errorMessage)};

    return {sol::make_object(lua, id), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaCreateBuffer(
    sol::state_view lua, int numChannels, int lengthInSamples,
    double sampleRate, const std::string& name)
{
    std::string errorMessage;
    int id = engine_.createBuffer(numChannels, lengthInSamples, sampleRate, name, errorMessage);
    if (id < 0)
        return {sol::lua_nil, sol::make_object(lua, errorMessage)};

    return {sol::make_object(lua, id), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaRemoveBuffer(
    sol::state_view lua, int id)
{
    if (!engine_.removeBuffer(id))
        return {sol::lua_nil, sol::make_object(lua, "Buffer " + std::to_string(id) + " not found")};

    return {sol::make_object(lua, true), sol::lua_nil};
}

sol::table LuaBindings::luaBuffers(sol::state_view lua)
{
    sol::table result = lua.create_table();
    auto bufs = engine_.getBuffers();
    int idx = 1;
    for (const auto& kv : bufs)
    {
        sol::table entry = lua.create_table();
        entry["id"] = kv.first;
        entry["name"] = kv.second;
        result[idx++] = entry;
    }
    return result;
}

std::tuple<sol::object, sol::object> LuaBindings::luaBufferInfo(
    sol::state_view lua, int id)
{
    Buffer* buf = engine_.getBuffer(id);
    if (!buf)
        return {sol::lua_nil, sol::make_object(lua, "Buffer " + std::to_string(id) + " not found")};

    sol::table info = lua.create_table();
    info["name"] = buf->getName();
    info["channels"] = buf->getNumChannels();
    info["length"] = buf->getLengthInSamples();
    info["sample_rate"] = buf->getSampleRate();
    info["file_path"] = buf->getFilePath();
    info["length_seconds"] = buf->getLengthInSeconds();

    return {sol::make_object(lua, info), sol::lua_nil};
}

// ============================================================
// Performance monitoring API
// ============================================================

sol::table LuaBindings::luaPerf(sol::state_view lua)
{
    // Auto-enable on first call so sq.perf() just works in the REPL.
    // Sleep one publish window (~100ms) so data is available immediately.
    if (!engine_.getPerfMonitor().isEnabled())
    {
        engine_.getPerfMonitor().enable();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    auto snap = engine_.getPerfSnapshot();

    sol::table result = lua.create_table();
    result["cpu"] = snap.cpuLoadPercent;
    result["callback_avg_us"] = snap.callbackAvgUs;
    result["callback_peak_us"] = snap.callbackPeakUs;
    result["xruns"] = snap.xrunCount;
    result["budget_us"] = snap.bufferDurationUs;
    result["sample_rate"] = snap.sampleRate;
    result["block_size"] = snap.blockSize;

    sol::table nodes = lua.create_table();
    for (int i = 0; i < (int)snap.nodes.size(); ++i)
    {
        sol::table n = lua.create_table();
        n["id"] = snap.nodes[i].nodeId;
        n["avg_us"] = snap.nodes[i].avgUs;
        n["peak_us"] = snap.nodes[i].peakUs;
        nodes[i + 1] = n;
    }
    result["nodes"] = nodes;

    sol::table midi = lua.create_table();
    for (int i = 0; i < (int)snap.midi.size(); ++i)
    {
        sol::table m = lua.create_table();
        m["device"] = snap.midi[i].deviceName;
        m["fill"] = snap.midi[i].fillLevel;
        m["dropped"] = snap.midi[i].droppedCount;
        midi[i + 1] = m;
    }
    result["midi"] = midi;

    return result;
}

void LuaBindings::luaPerfNodes(bool enable)
{
    if (enable)
        engine_.getPerfMonitor().enableNodeProfiling();
    else
        engine_.getPerfMonitor().disableNodeProfiling();
}

void LuaBindings::luaPerfReset()
{
    engine_.getPerfMonitor().resetCounters();
}

// ============================================================
// Transport API
// ============================================================

void LuaBindings::luaPlay()
{
    SQ_LOG("luaPlay (transport)");
    engine_.transportPlay();
}

void LuaBindings::luaTransportStop()
{
    SQ_LOG("luaTransportStop");
    engine_.transportStop();
}

void LuaBindings::luaPause()
{
    SQ_LOG("luaPause");
    engine_.transportPause();
}

void LuaBindings::luaSetTempo(double bpm)
{
    SQ_LOG("luaSetTempo: %.1f", bpm);
    engine_.transportSetTempo(bpm);
}

void LuaBindings::luaSetTimeSignature(int numerator, int denominator)
{
    SQ_LOG("luaSetTimeSignature: %d/%d", numerator, denominator);
    engine_.transportSetTimeSignature(numerator, denominator);
}

void LuaBindings::luaSetPositionBeats(double beats)
{
    SQ_LOG("luaSetPositionBeats: %.2f", beats);
    engine_.transportSetPositionInBeats(beats);
}

void LuaBindings::luaSetPositionSamples(int64_t samples)
{
    SQ_LOG("luaSetPositionSamples: %lld", (long long)samples);
    engine_.transportSetPositionInSamples(samples);
}

void LuaBindings::luaSetLoopPoints(double startBeats, double endBeats)
{
    SQ_LOG("luaSetLoopPoints: %.2f - %.2f", startBeats, endBeats);
    engine_.transportSetLoopPoints(startBeats, endBeats);
}

void LuaBindings::luaSetLooping(bool enabled)
{
    SQ_LOG("luaSetLooping: %s", enabled ? "on" : "off");
    engine_.transportSetLooping(enabled);
}

sol::table LuaBindings::luaTransportInfo(sol::state_view lua)
{
    auto& t = engine_.getTransport();

    sol::table result = lua.create_table();

    switch (t.getState())
    {
        case TransportState::stopped: result["state"] = "stopped"; break;
        case TransportState::playing: result["state"] = "playing"; break;
        case TransportState::paused:  result["state"] = "paused";  break;
    }

    result["tempo"] = t.getTempo();
    result["beats"] = t.getPositionInBeats();
    result["seconds"] = t.getPositionInSeconds();
    result["samples"] = t.getPositionInSamples();
    result["bar"] = t.getBarCount();

    auto ts = t.getTimeSignature();
    result["time_sig_num"] = ts.numerator;
    result["time_sig_denom"] = ts.denominator;

    result["looping"] = t.isLooping();
    result["loop_start"] = t.getLoopStartBeats();
    result["loop_end"] = t.getLoopEndBeats();

    return result;
}

} // namespace squeeze
