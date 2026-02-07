#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "core/LuaBindings.h"
#include "core/Engine.h"
#include "core/Logger.h"
#include "core/Scheduler.h"
#include "app/Repl.h"
#include "app/EditorManager.h"
#include "app/LuaBootstrap.h"

#include <atomic>
#include <csignal>

using namespace squeeze;

static std::atomic<bool> running{true};

static void signalHandler(int)
{
    running.store(false);
}

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
    EditorManager editors;
    sol::table sq = lua["sq"];
    sq.set_function("open_editor", [&lua, &engine, &editors](sol::this_state, int nodeId) {
        return editors.open(sol::state_view(lua), engine, nodeId);
    });
    sq.set_function("close_editor", [&lua, &editors](sol::this_state, int nodeId) {
        return editors.close(sol::state_view(lua), nodeId);
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
            editors.closeAll();
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

    editors.closeAll();
    engine.stop();
    Logger::drain();
    scheduler.collectGarbage();

    std::cout << "Goodbye." << std::endl;
    return 0;
}
