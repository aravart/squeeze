#include <juce_core/juce_core.h>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <atomic>
#include <csignal>

static std::atomic<bool> running{true};

static void signalHandler(int)
{
    running.store(false);
}

static void runRepl(sol::state& lua)
{
    std::cout << "squeeze> " << std::flush;
    std::string line;
    while (running.load() && std::getline(std::cin, line))
    {
        if (line == "quit" || line == "exit")
        {
            running.store(false);
            break;
        }

        // Try as expression first (so "1+1" prints the result)
        auto exprResult = lua.safe_script("return " + line, sol::script_pass_on_error);
        if (exprResult.valid())
        {
            sol::object val = exprResult;
            if (val.get_type() != sol::type::lua_nil)
            {
                if (val.is<std::string>())
                    std::cout << val.as<std::string>() << std::endl;
                else if (val.is<double>())
                    std::cout << val.as<double>() << std::endl;
                else if (val.is<bool>())
                    std::cout << (val.as<bool>() ? "true" : "false") << std::endl;
                else
                    std::cout << sol::type_name(lua, val.get_type()) << std::endl;
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

        std::cout << "squeeze> " << std::flush;
    }

    std::cout << std::endl;
}

static void runEngine()
{
    std::cout << "Engine running. Press Ctrl+C to stop." << std::endl;
    while (running.load())
        juce::Thread::sleep(100);
}

static void printUsage()
{
    std::cout << "Usage: squeeze [options] [script.lua]\n"
              << "  -i          Interactive mode (REPL)\n"
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
    bool interactive = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "-i")
            interactive = true;
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

    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math,
                       sol::lib::table, sol::lib::io, sol::lib::os);

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
            return 1;
        }
    }

    if (interactive)
        runRepl(lua);
    else
        runEngine();

    std::cout << "Goodbye." << std::endl;
    return 0;
}
