#include "core/BufferLibrary.h"
#include "core/Logger.h"

#include <algorithm>

namespace squeeze {

BufferLibrary::BufferLibrary()
{
    formatManager_.registerBasicFormats();
    SQ_INFO("BufferLibrary: initialized with %d audio formats",
            formatManager_.getNumKnownFormats());
}

BufferLibrary::~BufferLibrary()
{
    SQ_DEBUG("BufferLibrary: destroying with %d buffers",
             static_cast<int>(buffers_.size()));
}

int BufferLibrary::loadBuffer(const std::string& filePath, std::string& error)
{
    juce::File file(filePath);
    if (!file.existsAsFile())
    {
        error = "File not found: " + filePath;
        SQ_WARN("BufferLibrary::loadBuffer: %s", error.c_str());
        return -1;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager_.createReaderFor(file));
    if (!reader)
    {
        error = "Unsupported or corrupted audio file: " + filePath;
        SQ_WARN("BufferLibrary::loadBuffer: %s", error.c_str());
        return -1;
    }

    auto numChannels = static_cast<int>(reader->numChannels);
    auto numSamples = static_cast<int>(reader->lengthInSamples);
    double sampleRate = reader->sampleRate;

    juce::AudioBuffer<float> data(numChannels, numSamples);
    if (!reader->read(&data, 0, numSamples, 0, true, true))
    {
        error = "Failed to read audio data from: " + filePath;
        SQ_WARN("BufferLibrary::loadBuffer: %s", error.c_str());
        return -1;
    }

    std::string name = file.getFileNameWithoutExtension().toStdString();
    auto buf = Buffer::createFromData(std::move(data), sampleRate, name, filePath);
    if (!buf)
    {
        error = "Failed to create buffer from file: " + filePath;
        SQ_WARN("BufferLibrary::loadBuffer: %s", error.c_str());
        return -1;
    }

    int id = nextId_++;
    SQ_INFO("BufferLibrary::loadBuffer: id=%d, name=%s, ch=%d, len=%d, sr=%.1f, path=%s",
            id, name.c_str(), buf->getNumChannels(), buf->getLengthInSamples(),
            buf->getSampleRate(), filePath.c_str());
    buffers_[id] = {std::move(buf), name};
    return id;
}

int BufferLibrary::createBuffer(int numChannels, int lengthInSamples, double sampleRate,
                                const std::string& name, std::string& error)
{
    auto buf = Buffer::createEmpty(numChannels, lengthInSamples, sampleRate, name);
    if (!buf)
    {
        error = "Invalid buffer parameters";
        SQ_WARN("BufferLibrary::createBuffer: invalid params (ch=%d, len=%d, sr=%.1f)",
                numChannels, lengthInSamples, sampleRate);
        return -1;
    }

    int id = nextId_++;
    SQ_INFO("BufferLibrary::createBuffer: id=%d, name=%s, ch=%d, len=%d, sr=%.1f",
            id, name.c_str(), numChannels, lengthInSamples, sampleRate);
    buffers_[id] = {std::move(buf), name};
    return id;
}

std::unique_ptr<Buffer> BufferLibrary::removeBuffer(int id)
{
    auto it = buffers_.find(id);
    if (it == buffers_.end())
    {
        SQ_DEBUG("BufferLibrary::removeBuffer: id=%d not found", id);
        return nullptr;
    }

    auto buf = std::move(it->second.buffer);
    SQ_INFO("BufferLibrary::removeBuffer: id=%d, name=%s", id, it->second.name.c_str());
    buffers_.erase(it);
    return buf;
}

Buffer* BufferLibrary::getBuffer(int id) const
{
    auto it = buffers_.find(id);
    if (it == buffers_.end())
        return nullptr;
    return it->second.buffer.get();
}

std::string BufferLibrary::getBufferName(int id) const
{
    auto it = buffers_.find(id);
    if (it == buffers_.end())
        return "";
    return it->second.name;
}

std::vector<std::pair<int, std::string>> BufferLibrary::getBuffers() const
{
    std::vector<std::pair<int, std::string>> result;
    result.reserve(buffers_.size());
    for (const auto& [id, entry] : buffers_)
        result.emplace_back(id, entry.name);

    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    SQ_DEBUG("BufferLibrary::getBuffers: returning %d entries",
             static_cast<int>(result.size()));
    return result;
}

int BufferLibrary::getNumBuffers() const
{
    return static_cast<int>(buffers_.size());
}

} // namespace squeeze
