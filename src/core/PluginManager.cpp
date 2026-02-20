#include "core/PluginManager.h"
#include "core/Logger.h"
#include "core/PluginProcessor.h"

#include <algorithm>

namespace squeeze {

PluginManager::PluginManager()
{
    formatManager_.addDefaultFormats();
    SQ_INFO("PluginManager: created with %d formats",
            formatManager_.getNumFormats());
}

PluginManager::~PluginManager()
{
    SQ_INFO("PluginManager: destroyed (%d plugins loaded)",
            static_cast<int>(descriptions_.size()));
}

// ═══════════════════════════════════════════════════════════════════
// Cache loading
// ═══════════════════════════════════════════════════════════════════

bool PluginManager::loadCache(const std::string& xmlPath, std::string& error)
{
    SQ_DEBUG("PluginManager::loadCache: path=%s", xmlPath.c_str());

    juce::File file(xmlPath);
    if (!file.existsAsFile())
    {
        error = "File not found: " + xmlPath;
        SQ_WARN("PluginManager::loadCache: %s", error.c_str());
        descriptions_.clear();
        return false;
    }

    auto xmlString = file.loadFileAsString();
    if (xmlString.isEmpty())
    {
        error = "Empty file: " + xmlPath;
        SQ_WARN("PluginManager::loadCache: %s", error.c_str());
        descriptions_.clear();
        return false;
    }

    return loadCacheFromString(xmlString.toStdString(), error);
}

bool PluginManager::loadCacheFromString(const std::string& xmlString, std::string& error)
{
    SQ_DEBUG("PluginManager::loadCacheFromString: %d bytes",
             static_cast<int>(xmlString.size()));

    if (xmlString.empty())
    {
        error = "Empty XML string";
        SQ_WARN("PluginManager::loadCacheFromString: empty string");
        descriptions_.clear();
        return false;
    }

    auto xml = juce::parseXML(xmlString);
    if (!xml)
    {
        error = "Failed to parse XML";
        SQ_WARN("PluginManager::loadCacheFromString: XML parse failed");
        descriptions_.clear();
        return false;
    }

    juce::KnownPluginList pluginList;
    pluginList.recreateFromXml(*xml);

    auto types = pluginList.getTypes();
    descriptions_.clear();
    descriptions_.reserve(static_cast<size_t>(types.size()));
    for (const auto& desc : types)
        descriptions_.push_back(desc);

    if (descriptions_.empty())
    {
        error = "No plugins found in XML";
        SQ_WARN("PluginManager::loadCacheFromString: no plugins found");
        return false;
    }

    SQ_INFO("PluginManager::loadCacheFromString: loaded %d plugins",
            static_cast<int>(descriptions_.size()));
    return true;
}

// ═══════════════════════════════════════════════════════════════════
// Lookup
// ═══════════════════════════════════════════════════════════════════

const juce::PluginDescription* PluginManager::findByName(const std::string& name) const
{
    for (const auto& desc : descriptions_)
    {
        if (desc.name.toStdString() == name)
            return &desc;
    }
    return nullptr;
}

std::vector<std::string> PluginManager::getAvailablePlugins() const
{
    std::vector<std::string> names;
    names.reserve(descriptions_.size());
    for (const auto& desc : descriptions_)
        names.push_back(desc.name.toStdString());

    std::sort(names.begin(), names.end());
    return names;
}

std::vector<PluginInfo> PluginManager::getPluginInfos() const
{
    std::vector<PluginInfo> infos;
    infos.reserve(descriptions_.size());
    for (const auto& desc : descriptions_)
    {
        infos.push_back({
            desc.name.toStdString(),
            desc.manufacturerName.toStdString(),
            desc.category.toStdString(),
            desc.version.toStdString(),
            desc.isInstrument,
            desc.numInputChannels,
            desc.numOutputChannels
        });
    }

    std::sort(infos.begin(), infos.end(),
              [](const PluginInfo& a, const PluginInfo& b) { return a.name < b.name; });
    return infos;
}

int PluginManager::getNumPlugins() const
{
    return static_cast<int>(descriptions_.size());
}

// ═══════════════════════════════════════════════════════════════════
// Instantiation
// ═══════════════════════════════════════════════════════════════════

std::unique_ptr<Processor> PluginManager::createProcessor(const std::string& name,
                                                           double sampleRate, int blockSize,
                                                           std::string& error)
{
    SQ_DEBUG("PluginManager::createProcessor: name=%s sr=%f bs=%d",
             name.c_str(), sampleRate, blockSize);

    if (sampleRate <= 0.0)
    {
        error = "Invalid sample rate: " + std::to_string(sampleRate);
        SQ_WARN("PluginManager::createProcessor: %s", error.c_str());
        return nullptr;
    }

    if (blockSize <= 0)
    {
        error = "Invalid block size: " + std::to_string(blockSize);
        SQ_WARN("PluginManager::createProcessor: %s", error.c_str());
        return nullptr;
    }

    const auto* desc = findByName(name);
    if (!desc)
    {
        error = "Plugin not found: " + name;
        SQ_WARN("PluginManager::createProcessor: %s", error.c_str());
        return nullptr;
    }

    juce::String errorMsg;
    auto processor = formatManager_.createPluginInstance(
        *desc, sampleRate, blockSize, errorMsg);

    if (!processor)
    {
        error = "Failed to create plugin '" + name + "': " + errorMsg.toStdString();
        SQ_WARN("PluginManager::createProcessor: %s", error.c_str());
        return nullptr;
    }

    SQ_INFO("PluginManager::createProcessor: created '%s' (in=%d out=%d midi=%s)",
            name.c_str(), desc->numInputChannels, desc->numOutputChannels,
            desc->isInstrument ? "yes" : "no");

    return std::make_unique<PluginProcessor>(
        std::move(processor),
        desc->numInputChannels,
        desc->numOutputChannels,
        desc->isInstrument);
}

} // namespace squeeze
