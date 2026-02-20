#pragma once

#include "core/Buffer.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace squeeze {

class BufferLibrary {
public:
    BufferLibrary();
    ~BufferLibrary();

    BufferLibrary(const BufferLibrary&) = delete;
    BufferLibrary& operator=(const BufferLibrary&) = delete;

    // --- Buffer creation ---
    int loadBuffer(const std::string& filePath, std::string& error);
    int createBuffer(int numChannels, int lengthInSamples, double sampleRate,
                     const std::string& name, std::string& error);

    // --- Buffer removal ---
    std::unique_ptr<Buffer> removeBuffer(int id);

    // --- Queries ---
    Buffer* getBuffer(int id) const;
    std::string getBufferName(int id) const;
    std::vector<std::pair<int, std::string>> getBuffers() const;
    int getNumBuffers() const;

private:
    juce::AudioFormatManager formatManager_;
    struct BufferEntry {
        std::unique_ptr<Buffer> buffer;
        std::string name;
    };
    std::unordered_map<int, BufferEntry> buffers_;
    int nextId_ = 1;
};

} // namespace squeeze
