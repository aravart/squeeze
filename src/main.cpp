#include <juce_core/juce_core.h>

#include "core/LuaBindings.h"
#include "core/Engine.h"
#include "core/Logger.h"
#include "core/Scheduler.h"

extern "C" {
#include <linenoise.h>
}

#include <atomic>
#include <csignal>

using namespace squeeze;

static std::atomic<bool> running{true};

static void signalHandler(int)
{
    running.store(false);
}

static void printValue(sol::state_view lua, const sol::object& val, int indent = 0);

static void printTable(sol::state_view lua, const sol::table& tbl, int indent)
{
    // Check if it's an array (sequential integer keys starting at 1)
    bool isArray = true;
    int count = 0;
    for (auto& kv : tbl) {
        ++count;
        if (kv.first.get_type() != sol::type::number)
            { isArray = false; break; }
    }
    if (count == 0) { std::cout << "{}"; return; }

    // Check sequential keys
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

static std::string getHistoryPath()
{
    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    return home.getChildFile(".squeeze_history").getFullPathName().toStdString();
}

static void runRepl(sol::state& lua)
{
    auto histPath = getHistoryPath();
    linenoiseHistorySetMaxLen(500);
    linenoiseHistoryLoad(histPath.c_str());

    char* raw;
    while (running.load() && (raw = linenoise("squeeze> ")) != nullptr)
    {
        std::string line(raw);
        free(raw);

        if (line.empty())
            continue;

        if (line == "quit" || line == "exit")
        {
            running.store(false);
            break;
        }

        linenoiseHistoryAdd(line.c_str());
        linenoiseHistorySave(histPath.c_str());

        // Try as expression first (so "1+1" prints the result)
        auto exprResult = lua.safe_script("return " + line, sol::script_pass_on_error);
        if (exprResult.valid())
        {
            sol::object val = exprResult;
            if (val.get_type() != sol::type::lua_nil)
            {
                printValue(lua, val);
                std::cout << std::endl;
            }
        }
        else
        {
            // Try as statement
            auto stmtResult = lua.safe_script(line, sol::script_pass_on_error);
            if (!stmtResult.valid())
            {
                sol::error err = stmtResult;
                std::cerr << err.what() << std::endl;
            }
        }

        Logger::drain();
    }

    std::cout << std::endl;
}

static void printUsage()
{
    std::cout << "Usage: squeeze [options] [script.lua]\n"
              << "  -i          Interactive mode (REPL)\n"
              << "  -c FILE     Plugin cache XML file\n"
              << "  -d          Enable debug logging to stderr\n"
              << "  -h, --help  Show this help\n"
              << "\n"
              << "With no arguments, runs the engine until Ctrl+C.\n"
              << "With -i, opens the REPL instead.\n";
}

int main(int argc, char* argv[])
{
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::string scriptPath;
    std::string cachePath;
    bool interactive = false;
    bool debug = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "-i")
            interactive = true;
        else if (arg == "-d" || arg == "--debug")
            debug = true;
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

    if (debug)
    {
        Logger::enable();
        SQ_LOG("debug logging enabled");
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

    std::cout << "Squeeze 0.1.0 | "
              << juce::SystemStats::getJUCEVersion()
              << " | " << (std::string)lua["_VERSION"]
              << std::endl;

    if (!scriptPath.empty())
    {
        auto result = lua.safe_script_file(scriptPath, sol::script_pass_on_error);
        if (!result.valid())
        {
            sol::error err = result;
            std::cerr << "Error: " << err.what() << std::endl;
            engine.stop();
            scheduler.collectGarbage();
            return 1;
        }
    }

    if (interactive)
        runRepl(lua);
    else if (scriptPath.empty())
    {
        std::cout << "Engine running. Press Ctrl+C to stop." << std::endl;
        while (running.load())
        {
            Logger::drain();
            juce::Thread::sleep(100);
        }
    }

    engine.stop();
    Logger::drain();
    scheduler.collectGarbage();

    std::cout << "Goodbye." << std::endl;
    return 0;
}
