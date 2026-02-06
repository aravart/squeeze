#include "core/LuaBindings.h"

namespace squeeze {

LuaBindings::LuaBindings(Engine& engine, Scheduler& scheduler)
    : engine_(engine)
    , scheduler_(scheduler)
{
    formatManager_.addDefaultFormats();
}

bool LuaBindings::loadPluginCache(const std::string& xmlPath)
{
    return cache_.loadFromFile(juce::File(xmlPath));
}

int LuaBindings::addTestNode(std::unique_ptr<Node> node, const std::string& name)
{
    Node* raw = node.get();
    int id = graph_.addNode(raw);
    ownedNodes_[id] = std::move(node);
    nodeNames_[id] = name;
    return id;
}

Graph& LuaBindings::getGraph()
{
    return graph_;
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

    sq.set_function("stop", [this]() {
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
}

// ============================================================
// Lua API implementations
// ============================================================

sol::table LuaBindings::luaListPlugins(sol::state_view lua)
{
    sol::table result = lua.create_table();
    auto names = cache_.getAvailablePluginNames();
    for (int i = 0; i < (int)names.size(); ++i)
        result[i + 1] = names[i].toStdString();
    return result;
}

std::tuple<sol::object, sol::object> LuaBindings::luaPluginInfo(
    sol::state_view lua, const std::string& name)
{
    auto* desc = cache_.findByName(juce::String(name));
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
    auto* desc = cache_.findByName(juce::String(name));
    if (!desc)
        return {sol::lua_nil, sol::make_object(lua, "Plugin '" + name + "' not found in cache")};

    double sr = engine_.getSampleRate();
    int bs = engine_.getBlockSize();
    if (sr <= 0.0) sr = 44100.0;
    if (bs <= 0) bs = 512;

    juce::String errorMessage;
    auto pluginNode = PluginNode::create(*desc, formatManager_, sr, bs, errorMessage);
    if (!pluginNode)
        return {sol::lua_nil, sol::make_object(lua, "Failed to create plugin: " + errorMessage.toStdString())};

    Node* raw = pluginNode.get();
    int id = graph_.addNode(raw);
    ownedNodes_[id] = std::move(pluginNode);
    nodeNames_[id] = name;

    return {sol::make_object(lua, id), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaRemoveNode(
    sol::state_view lua, int id)
{
    if (ownedNodes_.find(id) == ownedNodes_.end())
        return {sol::lua_nil, sol::make_object(lua, "Node " + std::to_string(id) + " not found")};

    graph_.removeNode(id);
    ownedNodes_.erase(id);
    nodeNames_.erase(id);

    return {sol::make_object(lua, true), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaConnect(
    sol::state_view lua,
    int srcId, const std::string& srcPort,
    int dstId, const std::string& dstPort)
{
    PortAddress source{srcId, PortDirection::output, srcPort};
    PortAddress dest{dstId, PortDirection::input, dstPort};

    int connId = graph_.connect(source, dest);
    if (connId < 0)
        return {sol::lua_nil, sol::make_object(lua, graph_.getLastError())};

    return {sol::make_object(lua, connId), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaDisconnect(
    sol::state_view lua, int connId)
{
    if (!graph_.disconnect(connId))
        return {sol::lua_nil, sol::make_object(lua, "Connection " + std::to_string(connId) + " not found")};

    return {sol::make_object(lua, true), sol::lua_nil};
}

void LuaBindings::luaUpdate()
{
    engine_.updateGraph(graph_);
}

void LuaBindings::luaStart(sol::optional<double> sr, sol::optional<int> bs)
{
    double sampleRate = sr.value_or(44100.0);
    int blockSize = bs.value_or(512);
    engine_.start(sampleRate, blockSize);
}

void LuaBindings::luaStop()
{
    engine_.stop();
}

std::tuple<sol::object, sol::object> LuaBindings::luaSetParam(
    sol::state_view lua, int nodeId, const std::string& name, float value)
{
    Node* node = graph_.getNode(nodeId);
    if (!node)
        return {sol::lua_nil, sol::make_object(lua, "Node " + std::to_string(nodeId) + " not found")};

    node->setParameter(name, value);
    return {sol::make_object(lua, true), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaGetParam(
    sol::state_view lua, int nodeId, const std::string& name)
{
    Node* node = graph_.getNode(nodeId);
    if (!node)
        return {sol::lua_nil, sol::make_object(lua, "Node " + std::to_string(nodeId) + " not found")};

    float val = node->getParameter(name);
    return {sol::make_object(lua, val), sol::lua_nil};
}

std::tuple<sol::object, sol::object> LuaBindings::luaParams(
    sol::state_view lua, int nodeId)
{
    Node* node = graph_.getNode(nodeId);
    if (!node)
        return {sol::lua_nil, sol::make_object(lua, "Node " + std::to_string(nodeId) + " not found")};

    auto names = node->getParameterNames();
    sol::table result = lua.create_table();
    for (int i = 0; i < (int)names.size(); ++i)
        result[i + 1] = names[i];

    return {sol::make_object(lua, result), sol::lua_nil};
}

sol::table LuaBindings::luaNodes(sol::state_view lua)
{
    sol::table result = lua.create_table();
    int idx = 1;
    for (const auto& kv : ownedNodes_)
    {
        sol::table entry = lua.create_table();
        entry["id"] = kv.first;

        auto nameIt = nodeNames_.find(kv.first);
        entry["name"] = (nameIt != nodeNames_.end()) ? nameIt->second : "unknown";

        result[idx++] = entry;
    }
    return result;
}

std::tuple<sol::object, sol::object> LuaBindings::luaPorts(
    sol::state_view lua, int nodeId)
{
    Node* node = graph_.getNode(nodeId);
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
    auto conns = graph_.getConnections();
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

} // namespace squeeze
