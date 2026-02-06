#include "core/PluginCache.h"

namespace squeeze {

bool PluginCache::loadFromFile(const juce::File& xmlFile)
{
    if (!xmlFile.existsAsFile())
        return false;

    auto xmlString = xmlFile.loadFileAsString();
    return loadFromXml(xmlString);
}

bool PluginCache::loadFromXml(const juce::String& xmlString)
{
    auto xml = juce::parseXML(xmlString);
    if (!xml)
        return false;

    pluginList_.clear();
    pluginList_.recreateFromXml(*xml);

    // Copy types to stable storage (getTypes() may return by value)
    types_.clear();
    auto arr = pluginList_.getTypes();
    for (int i = 0; i < arr.size(); ++i)
        types_.push_back(arr[i]);

    return !types_.empty();
}

const juce::PluginDescription* PluginCache::findByName(const juce::String& name) const
{
    for (const auto& desc : types_)
    {
        if (desc.name == name)
            return &desc;
    }
    return nullptr;
}

std::vector<juce::String> PluginCache::getAvailablePluginNames() const
{
    std::vector<juce::String> names;
    for (const auto& desc : types_)
        names.push_back(desc.name);
    return names;
}

int PluginCache::getNumPlugins() const
{
    return (int)types_.size();
}

} // namespace squeeze
