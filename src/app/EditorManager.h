#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "gui/PluginEditorWindow.h"

#include <map>
#include <memory>
#include <tuple>

namespace squeeze {

class Engine;

class EditorManager {
public:
    std::tuple<sol::object, sol::object> open(sol::state_view lua, Engine& engine, int nodeId);
    std::tuple<sol::object, sol::object> close(sol::state_view lua, int nodeId);
    void closeAll();

private:
    static bool runOnMessageThread(std::function<void()> fn);

    std::map<int, std::unique_ptr<PluginEditorWindow>> windows_;
};

} // namespace squeeze
