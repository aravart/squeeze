#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace squeeze {

class PluginEditorWindow : public juce::DocumentWindow {
public:
    PluginEditorWindow(const juce::String& name,
                       juce::AudioProcessorEditor* editor,
                       int nodeId,
                       std::function<void(int)> onClose)
        : DocumentWindow(name, juce::Colours::darkgrey, DocumentWindow::closeButton)
        , nodeId_(nodeId)
        , onClose_(std::move(onClose))
    {
        setContentOwned(editor, true);
        setResizable(false, false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        auto cb = onClose_;
        auto id = nodeId_;
        juce::MessageManager::callAsync([cb, id]() {
            if (cb) cb(id);
        });
    }

private:
    int nodeId_;
    std::function<void(int)> onClose_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditorWindow)
};

} // namespace squeeze
