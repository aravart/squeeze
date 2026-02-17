#pragma once

#include "core/Node.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <string>
#include <vector>

namespace squeeze {

/// Manages plugin cache loading and plugin instantiation.
/// Loads JUCE KnownPluginList XML caches and creates PluginNode instances.
/// Has no Engine dependency — returns std::unique_ptr<Node>.
class PluginManager {
public:
    PluginManager();
    ~PluginManager();

    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    // --- Cache loading ---
    bool loadCache(const std::string& xmlPath, std::string& error);
    bool loadCacheFromString(const std::string& xmlString, std::string& error);

    // --- Lookup ---
    const juce::PluginDescription* findByName(const std::string& name) const;
    std::vector<std::string> getAvailablePlugins() const;
    int getNumPlugins() const;

    // --- Instantiation ---
    std::unique_ptr<Node> createNode(const std::string& name,
                                     double sampleRate, int blockSize,
                                     std::string& error);

private:
    juce::AudioPluginFormatManager formatManager_;
    std::vector<juce::PluginDescription> descriptions_;
};

} // namespace squeeze
