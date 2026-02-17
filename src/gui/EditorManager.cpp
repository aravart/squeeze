#include "gui/EditorManager.h"
#include "core/Engine.h"
#include "core/Logger.h"
#include "core/PluginNode.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace squeeze {

bool EditorManager::open(Engine& engine, int nodeId, std::string& error)
{
    SQ_DEBUG("EditorManager::open: nodeId=%d", nodeId);

    if (windows_.count(nodeId))
    {
        error = "Editor already open for node " + std::to_string(nodeId);
        SQ_WARN("EditorManager::open: %s", error.c_str());
        return false;
    }

    Node* node = engine.getNode(nodeId);
    if (!node)
    {
        error = "Node " + std::to_string(nodeId) + " not found";
        SQ_WARN("EditorManager::open: %s", error.c_str());
        return false;
    }

    auto* pluginNode = dynamic_cast<PluginNode*>(node);
    if (!pluginNode)
    {
        error = "Node " + std::to_string(nodeId) + " is not a plugin";
        SQ_WARN("EditorManager::open: %s", error.c_str());
        return false;
    }

    auto* processor = pluginNode->getProcessor();
    if (!processor || !processor->hasEditor())
    {
        error = "Plugin has no editor";
        SQ_WARN("EditorManager::open: %s", error.c_str());
        return false;
    }

    std::string errorMsg;
    bool success = false;

    bool dispatched = runOnMessageThread([&]() {
        auto* editor = processor->createEditorIfNeeded();
        if (!editor)
        {
            errorMsg = "Failed to create editor";
            return;
        }

#if JUCE_MAC
        juce::Process::setDockIconVisible(true);
#endif

        auto name = engine.getNodeName(nodeId);
        auto window = std::make_unique<PluginEditorWindow>(
            juce::String(name), editor, nodeId,
            [this](int id) { windows_.erase(id); });

        windows_[nodeId] = std::move(window);
        success = true;
    });

    if (!dispatched)
    {
        error = "GUI unavailable (timeout)";
        SQ_WARN("EditorManager::open: %s", error.c_str());
        return false;
    }

    if (!success)
    {
        error = errorMsg;
        SQ_WARN("EditorManager::open: %s", error.c_str());
        return false;
    }

    SQ_INFO("EditorManager::open: opened editor for node %d", nodeId);
    return true;
}

bool EditorManager::close(int nodeId, std::string& error)
{
    SQ_DEBUG("EditorManager::close: nodeId=%d", nodeId);

    auto it = windows_.find(nodeId);
    if (it == windows_.end())
    {
        error = "No editor open for node " + std::to_string(nodeId);
        SQ_WARN("EditorManager::close: %s", error.c_str());
        return false;
    }

    if (!runOnMessageThread([&]() { windows_.erase(it); }))
    {
        error = "GUI unavailable (timeout)";
        SQ_WARN("EditorManager::close: %s", error.c_str());
        return false;
    }

    SQ_INFO("EditorManager::close: closed editor for node %d", nodeId);
    return true;
}

void EditorManager::closeAll()
{
    SQ_DEBUG("EditorManager::closeAll: closing %zu editor(s)", windows_.size());
    windows_.clear();
}

bool EditorManager::hasEditor(int nodeId) const
{
    return windows_.count(nodeId) > 0;
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
