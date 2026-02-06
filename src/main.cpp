#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "core/LuaBindings.h"
#include "core/Engine.h"
#include "core/Logger.h"
#include "core/PluginNode.h"
#include "core/Scheduler.h"
#include "gui/PluginEditorWindow.h"

extern "C" {
#include <linenoise.h>
}

#include <atomic>
#include <csignal>
#include <map>

using namespace squeeze;

static std::atomic<bool> running{true};

static void signalHandler(int)
{
    running.store(false);
}

// ============================================================
// REPL printing helpers (unchanged)
// ============================================================

static void printValue(sol::state_view lua, const sol::object& val, int indent = 0);

static void printTable(sol::state_view lua, const sol::table& tbl, int indent)
{
    bool isArray = true;
    int count = 0;
    for (auto& kv : tbl) {
        ++count;
        if (kv.first.get_type() != sol::type::number)
            { isArray = false; break; }
    }
    if (count == 0) { std::cout << "{}"; return; }

    if (isArray) {
        for (int i = 1; i <= count; ++i) {
            if (tbl[i].get_type() == sol::type::lua_nil)
                { isArray = false; break; }
        }
    }

    std::string pad(indent + 2, ' ');
    std::cout << "{" << std::endl;

    if (isArray) {
        for (int i = 1; i <= count; ++i) {
            std::cout << pad;
            printValue(lua, tbl[i], indent + 2);
            if (i < count) std::cout << ",";
            std::cout << std::endl;
        }
    } else {
        int idx = 0;
        for (auto& kv : tbl) {
            std::cout << pad;
            if (kv.first.is<std::string>())
                std::cout << kv.first.as<std::string>();
            else
                std::cout << "[" << kv.first.as<double>() << "]";
            std::cout << " = ";
            printValue(lua, kv.second, indent + 2);
            if (++idx < count) std::cout << ",";
            std::cout << std::endl;
        }
    }

    std::cout << std::string(indent, ' ') << "}";
}

static void printValue(sol::state_view lua, const sol::object& val, int indent)
{
    switch (val.get_type()) {
        case sol::type::string:
            std::cout << "\"" << val.as<std::string>() << "\"";
            break;
        case sol::type::number:
            std::cout << val.as<double>();
            break;
        case sol::type::boolean:
            std::cout << (val.as<bool>() ? "true" : "false");
            break;
        case sol::type::table:
            printTable(lua, val.as<sol::table>(), indent);
            break;
        case sol::type::lua_nil:
            std::cout << "nil";
            break;
        default:
            std::cout << sol::type_name(lua, val.get_type());
            break;
    }
}

// ============================================================
// REPL thread — runs linenoise on a background thread
// ============================================================

class ReplThread : public juce::Thread {
public:
    ReplThread(sol::state& lua, std::atomic<bool>& runFlag)
        : Thread("REPL"), lua_(lua), running_(runFlag)
    {}

    void run() override
    {
        auto histPath = getHistoryPath();
        linenoiseHistorySetMaxLen(500);
        linenoiseHistoryLoad(histPath.c_str());

        char* raw;
        while (!threadShouldExit() && running_.load()
               && (raw = linenoise("squeeze> ")) != nullptr)
        {
            std::string line(raw);
            free(raw);

            if (line.empty())
                continue;

            if (line == "quit" || line == "exit")
            {
                running_.store(false);
                break;
            }

            linenoiseHistoryAdd(line.c_str());
            linenoiseHistorySave(histPath.c_str());

            auto exprResult = lua_.safe_script("return " + line, sol::script_pass_on_error);
            if (exprResult.valid())
            {
                sol::object val = exprResult;
                if (val.get_type() != sol::type::lua_nil)
                {
                    printValue(lua_, val);
                    std::cout << std::endl;
                }
            }
            else
            {
                auto stmtResult = lua_.safe_script(line, sol::script_pass_on_error);
                if (!stmtResult.valid())
                {
                    sol::error err = stmtResult;
                    std::cerr << err.what() << std::endl;
                }
            }

            Logger::drain();
        }

        std::cout << std::endl;
        running_.store(false);
    }

private:
    static std::string getHistoryPath()
    {
        auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
        return home.getChildFile(".squeeze_history").getFullPathName().toStdString();
    }

    sol::state& lua_;
    std::atomic<bool>& running_;
};

// ============================================================
// Editor window management
// ============================================================

static std::map<int, std::unique_ptr<PluginEditorWindow>> editorWindows;

static void closeEditorWindow(int nodeId)
{
    editorWindows.erase(nodeId);
}

// Run a function on the message thread, blocking until complete.
// If already on the message thread, runs directly.
template <typename Fn>
static void runOnMessageThread(Fn&& fn)
{
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        fn();
    }
    else
    {
        juce::WaitableEvent done;
        juce::MessageManager::callAsync([&]() {
            fn();
            done.signal();
        });
        done.wait();
    }
}

static std::tuple<sol::object, sol::object> openEditor(
    sol::state_view lua, Engine& engine, int nodeId)
{
    if (editorWindows.count(nodeId))
        return {sol::lua_nil, sol::make_object(lua,
            "Editor already open for node " + std::to_string(nodeId))};

    Node* node = engine.getNode(nodeId);
    if (!node)
        return {sol::lua_nil, sol::make_object(lua,
            "Node " + std::to_string(nodeId) + " not found")};

    auto* pluginNode = dynamic_cast<PluginNode*>(node);
    if (!pluginNode)
        return {sol::lua_nil, sol::make_object(lua,
            "Node " + std::to_string(nodeId) + " is not a plugin")};

    auto* processor = pluginNode->getProcessor();
    if (!processor || !processor->hasEditor())
        return {sol::lua_nil, sol::make_object(lua,
            "Plugin has no editor")};

    // GUI work must happen on the actual message thread (macOS AppKit requirement)
    std::string errorMsg;
    bool success = false;

    runOnMessageThread([&]() {
        auto* editor = processor->createEditorIfNeeded();
        if (!editor)
        {
            errorMsg = "Failed to create editor";
            return;
        }

        auto name = engine.getNodeName(nodeId);
        auto window = std::make_unique<PluginEditorWindow>(
            juce::String(name), editor, nodeId, closeEditorWindow);

        editorWindows[nodeId] = std::move(window);
        success = true;
    });

    if (!success)
        return {sol::lua_nil, sol::make_object(lua,
            errorMsg.empty() ? "GUI unavailable" : errorMsg)};

    return {sol::make_object(lua, true), sol::lua_nil};
}

static std::tuple<sol::object, sol::object> closeEditor(
    sol::state_view lua, int nodeId)
{
    auto it = editorWindows.find(nodeId);
    if (it == editorWindows.end())
        return {sol::lua_nil, sol::make_object(lua,
            "No editor open for node " + std::to_string(nodeId))};

    runOnMessageThread([&]() {
        editorWindows.erase(it);
    });

    return {sol::make_object(lua, true), sol::lua_nil};
}

// ============================================================
// Lua bootstrap: node objects with metatables
// ============================================================

static const char* luaBootstrap = R"lua(
-- PluginNode metatable
local PluginNode = {}
PluginNode.__index = PluginNode

function PluginNode:open_editor()
    return sq.open_editor(self.id)
end

function PluginNode:close_editor()
    return sq.close_editor(self.id)
end

function PluginNode:set_param(name, value)
    return sq.set_param(self.id, name, value)
end

function PluginNode:get_param(name)
    return sq.get_param(self.id, name)
end

function PluginNode:params()
    return sq.params(self.id)
end

function PluginNode:ports()
    return sq.ports(self.id)
end

function PluginNode:remove()
    return sq.remove_node(self.id)
end

function PluginNode:__tostring()
    return "PluginNode(" .. self.id .. ", \"" .. self.name .. "\")"
end

-- MidiInputNode metatable
local MidiInputNode = {}
MidiInputNode.__index = MidiInputNode

function MidiInputNode:remove()
    return sq.remove_node(self.id)
end

function MidiInputNode:__tostring()
    return "MidiInputNode(" .. self.id .. ", \"" .. self.name .. "\")"
end

-- Wrap sq.add_plugin to return a PluginNode object
local raw_add_plugin = sq.add_plugin
sq.add_plugin = function(name)
    local id, err = raw_add_plugin(name)
    if not id then return nil, err end
    local node = { id = id, name = name }
    setmetatable(node, PluginNode)
    return node
end

-- Wrap sq.add_midi_input to return a MidiInputNode object
local raw_add_midi_input = sq.add_midi_input
sq.add_midi_input = function(name)
    local id, err = raw_add_midi_input(name)
    if not id then return nil, err end
    local node = { id = id, name = name }
    setmetatable(node, MidiInputNode)
    return node
end

-- Wrap sq.connect to accept node objects or bare IDs
local raw_connect = sq.connect
sq.connect = function(src, src_port, dst, dst_port, midi_channel)
    local src_id = type(src) == "table" and src.id or src
    local dst_id = type(dst) == "table" and dst.id or dst
    return raw_connect(src_id, src_port, dst_id, dst_port, midi_channel)
end
)lua";

// ============================================================
// Usage / main
// ============================================================

static void printUsage()
{
    std::cout << "Usage: squeeze [options] [script.lua]\n"
              << "  -i          Interactive mode (REPL)\n"
              << "  -c FILE     Plugin cache XML file\n"
              << "  -d          Enable debug logging to stderr\n"
              << "  -dd         Enable trace logging (includes per-message MIDI)\n"
              << "  -h, --help  Show this help\n"
              << "\n"
              << "With no arguments, runs the engine until Ctrl+C.\n"
              << "With -i, opens the REPL instead.\n";
}

int main(int argc, char* argv[])
{
    // GUI initialisation (creates MessageManager, enables windows)
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Process::setDockIconVisible(true);

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::string scriptPath;
    std::string cachePath;
    bool interactive = false;
    LogLevel logLevel = LogLevel::off;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "-i")
            interactive = true;
        else if (arg == "-dd")
            logLevel = LogLevel::trace;
        else if (arg == "-d" || arg == "--debug")
            logLevel = std::max(logLevel, LogLevel::debug);
        else if (arg == "-c" && i + 1 < argc)
            cachePath = argv[++i];
        else if (arg == "-h" || arg == "--help")
        {
            printUsage();
            return 0;
        }
        else if (arg[0] == '-')
        {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage();
            return 1;
        }
        else
            scriptPath = arg;
    }

    if (logLevel != LogLevel::off)
    {
        Logger::setLevel(logLevel);
        SQ_LOG("debug logging enabled (level %d)", static_cast<int>(logLevel));
    }

    // Create engine components
    Scheduler scheduler;
    Engine engine(scheduler);

    // Load plugin cache into engine
    if (cachePath.empty())
        cachePath = "plugin-cache.xml";

    if (engine.loadPluginCache(cachePath))
        std::cout << "Plugin cache loaded: " << cachePath << std::endl;
    else if (cachePath != "plugin-cache.xml")
        std::cerr << "Warning: failed to load plugin cache: " << cachePath << std::endl;

    // Auto-load all available MIDI inputs
    engine.autoLoadMidiInputs();

    // Create Lua bindings and register the sq API
    LuaBindings bindings(engine);

    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math,
                       sol::lib::table, sol::lib::io, sol::lib::os);

    bindings.bind(lua);

    // Register editor functions on the sq table (app-layer, not in squeeze_lua)
    sol::table sq = lua["sq"];
    sq.set_function("open_editor", [&lua, &engine](sol::this_state, int nodeId) {
        return openEditor(sol::state_view(lua), engine, nodeId);
    });
    sq.set_function("close_editor", [&lua](sol::this_state, int nodeId) {
        return closeEditor(sol::state_view(lua), nodeId);
    });

    // Run Lua bootstrap to set up node object metatables and wrappers
    auto bootstrapResult = lua.safe_script(luaBootstrap, sol::script_pass_on_error);
    if (!bootstrapResult.valid())
    {
        sol::error err = bootstrapResult;
        std::cerr << "Bootstrap error: " << err.what() << std::endl;
        return 1;
    }

    std::cout << "Squeeze 0.1.0 | "
              << juce::SystemStats::getJUCEVersion()
              << " | " << (std::string)lua["_VERSION"]
              << std::endl;

    // Run script if provided
    if (!scriptPath.empty())
    {
        auto result = lua.safe_script_file(scriptPath, sol::script_pass_on_error);
        if (!result.valid())
        {
            sol::error err = result;
            std::cerr << "Error: " << err.what() << std::endl;
            editorWindows.clear();
            engine.stop();
            scheduler.collectGarbage();
            return 1;
        }
    }

    // Start REPL thread if interactive
    std::unique_ptr<ReplThread> replThread;
    if (interactive)
    {
        replThread = std::make_unique<ReplThread>(lua, running);
        replThread->startThread();
    }
    else if (scriptPath.empty())
    {
        std::cout << "Engine running. Press Ctrl+C to stop." << std::endl;
    }

    // Main thread message loop (pumps GUI events + drains logger)
    while (running.load())
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        Logger::drain();
    }

    // Shutdown
    if (replThread)
    {
        replThread->signalThreadShouldExit();
        replThread->waitForThreadToExit(2000);
    }

    editorWindows.clear();
    engine.stop();
    Logger::drain();
    scheduler.collectGarbage();

    std::cout << "Goodbye." << std::endl;
    return 0;
}
