#pragma once

#include <juce_core/juce_core.h>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <atomic>

namespace squeeze {

/// Pretty-print a Lua value to stdout (tables are recursively formatted).
void printValue(sol::state_view lua, const sol::object& val, int indent = 0);

/// REPL thread — runs linenoise on a background thread.
class ReplThread : public juce::Thread {
public:
    ReplThread(sol::state& lua, std::atomic<bool>& runFlag);
    void run() override;

private:
    static std::string getHistoryPath();

    sol::state& lua_;
    std::atomic<bool>& running_;
};

} // namespace squeeze
