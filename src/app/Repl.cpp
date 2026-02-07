#include "app/Repl.h"
#include "core/Logger.h"

extern "C" {
#include <linenoise.h>
}

#include <algorithm>
#include <iostream>
#include <vector>

namespace squeeze {

// Forward declaration for mutual recursion
static void printTable(sol::state_view lua, const sol::table& tbl, int indent);

void printValue(sol::state_view lua, const sol::object& val, int indent)
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
        // Collect keys and sort them for stable output
        std::vector<std::pair<std::string, sol::object>> entries;
        for (auto& kv : tbl) {
            std::string key;
            if (kv.first.is<std::string>())
                key = kv.first.as<std::string>();
            else
                key = "[" + std::to_string(kv.first.as<double>()) + "]";
            entries.push_back({key, kv.second});
        }
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        for (int i = 0; i < (int)entries.size(); ++i) {
            std::cout << pad << entries[i].first << " = ";
            printValue(lua, entries[i].second, indent + 2);
            if (i + 1 < (int)entries.size()) std::cout << ",";
            std::cout << std::endl;
        }
    }

    std::cout << std::string(indent, ' ') << "}";
}

// --- ReplThread ---

ReplThread::ReplThread(sol::state& lua, std::atomic<bool>& runFlag)
    : Thread("REPL"), lua_(lua), running_(runFlag)
{}

void ReplThread::run()
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

std::string ReplThread::getHistoryPath()
{
    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    return home.getChildFile(".squeeze_history").getFullPathName().toStdString();
}

} // namespace squeeze
