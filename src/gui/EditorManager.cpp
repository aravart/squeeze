#include "gui/EditorManager.h"
#include "core/Engine.h"
#include "core/Logger.h"
#include "core/PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace squeeze {

bool EditorManager::open(Engine& engine, int procHandle, std::string& error)
{
    SQ_DEBUG("EditorManager::open: procHandle=%d", procHandle);

    if (windows_.count(procHandle))
    {
        error = "Editor already open for processor " + std::to_string(procHandle);
        SQ_WARN("EditorManager::open: %s", error.c_str());
        return false;
    }

    Processor* proc = engine.getProcessor(procHandle);
    if (!proc)
    {
        error = "Processor " + std::to_string(procHandle) + " not found";
        SQ_WARN("EditorManager::open: %s", error.c_str());
        return false;
    }

    auto* pluginProc = dynamic_cast<PluginProcessor*>(proc);
    if (!pluginProc)
    {
        error = "Processor " + std::to_string(procHandle) + " is not a plugin";
        SQ_WARN("EditorManager::open: %s", error.c_str());
        return false;
    }

    auto* juceProcessor = pluginProc->getJuceProcessor();
    if (!juceProcessor || !juceProcessor->hasEditor())
    {
        error = "Plugin has no editor";
        SQ_WARN("EditorManager::open: %s", error.c_str());
        return false;
    }

    std::string errorMsg;
    bool success = false;

    bool dispatched = runOnMessageThread([&]() {
        auto* editor = juceProcessor->createEditorIfNeeded();
        if (!editor)
        {
            errorMsg = "Failed to create editor";
            return;
        }

#if JUCE_MAC
        juce::Process::setDockIconVisible(true);
#endif

        auto name = pluginProc->getPluginName();
        auto window = std::make_unique<PluginEditorWindow>(
            juce::String(name), editor, procHandle,
            [this](int id) { windows_.erase(id); });

        windows_[procHandle] = std::move(window);
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

    SQ_INFO("EditorManager::open: opened editor for proc %d", procHandle);
    return true;
}

bool EditorManager::close(int procHandle, std::string& error)
{
    SQ_DEBUG("EditorManager::close: procHandle=%d", procHandle);

    auto it = windows_.find(procHandle);
    if (it == windows_.end())
    {
        error = "No editor open for processor " + std::to_string(procHandle);
        SQ_WARN("EditorManager::close: %s", error.c_str());
        return false;
    }

    if (!runOnMessageThread([&]() { windows_.erase(it); }))
    {
        error = "GUI unavailable (timeout)";
        SQ_WARN("EditorManager::close: %s", error.c_str());
        return false;
    }

    SQ_INFO("EditorManager::close: closed editor for proc %d", procHandle);
    return true;
}

void EditorManager::closeAll()
{
    SQ_DEBUG("EditorManager::closeAll: closing %zu editor(s)", windows_.size());
    windows_.clear();
}

bool EditorManager::hasEditor(int procHandle) const
{
    return windows_.count(procHandle) > 0;
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
