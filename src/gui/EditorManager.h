#pragma once

#include "gui/PluginEditorWindow.h"

#include <map>
#include <memory>
#include <string>
#include <functional>

namespace squeeze {

class Engine;

class EditorManager {
public:
    bool open(Engine& engine, int procHandle, std::string& error);
    bool close(int procHandle, std::string& error);
    void closeAll();
    bool hasEditor(int procHandle) const;

private:
    static bool runOnMessageThread(std::function<void()> fn);

    std::map<int, std::unique_ptr<PluginEditorWindow>> windows_;
};

} // namespace squeeze
