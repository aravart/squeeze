#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <string>
#include <vector>

namespace squeeze {

class PluginCache {
public:
    bool loadFromFile(const juce::File& xmlFile);
    bool loadFromXml(const juce::String& xmlString);

    const juce::PluginDescription* findByName(const juce::String& name) const;
    std::vector<juce::String> getAvailablePluginNames() const;
    int getNumPlugins() const;

private:
    juce::KnownPluginList pluginList_;
    std::vector<juce::PluginDescription> types_;  // stable storage for pointer returns
};

} // namespace squeeze
