#include <juce_audio_processors/juce_audio_processors.h>

#include "app/EditorManager.h"
#include "core/Engine.h"
#include "core/PluginNode.h"

namespace squeeze {

std::tuple<sol::object, sol::object> EditorManager::open(
    sol::state_view lua, Engine& engine, int nodeId)
{
    if (windows_.count(nodeId))
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

    bool dispatched = runOnMessageThread([&]() {
        auto* editor = processor->createEditorIfNeeded();
        if (!editor)
        {
            errorMsg = "Failed to create editor";
            return;
        }

        auto name = engine.getNodeName(nodeId);
        auto window = std::make_unique<PluginEditorWindow>(
            juce::String(name), editor, nodeId,
            [this](int id) { windows_.erase(id); });

        windows_[nodeId] = std::move(window);
        success = true;
    });

    if (!dispatched)
        return {sol::lua_nil, sol::make_object(lua, "GUI unavailable (timeout)")};

    if (!success)
        return {sol::lua_nil, sol::make_object(lua, errorMsg)};

    return {sol::make_object(lua, true), sol::lua_nil};
}

std::tuple<sol::object, sol::object> EditorManager::close(
    sol::state_view lua, int nodeId)
{
    auto it = windows_.find(nodeId);
    if (it == windows_.end())
        return {sol::lua_nil, sol::make_object(lua,
            "No editor open for node " + std::to_string(nodeId))};

    if (!runOnMessageThread([&]() { windows_.erase(it); }))
        return {sol::lua_nil, sol::make_object(lua, "GUI unavailable (timeout)")};

    return {sol::make_object(lua, true), sol::lua_nil};
}

void EditorManager::closeAll()
{
    windows_.clear();
}

bool EditorManager::runOnMessageThread(std::function<void()> fn)
{
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        fn();
        return true;
    }

    juce::WaitableEvent done;
    juce::MessageManager::callAsync([&]() {
        fn();
        done.signal();
    });
    return done.wait(5000);
}

} // namespace squeeze
