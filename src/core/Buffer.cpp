#include "core/Buffer.h"
#include "core/Logger.h"

namespace squeeze {

std::unique_ptr<Buffer> Buffer::loadFromFile(
    const std::string& filePath,
    juce::AudioFormatManager& formatManager,
    std::string& errorMessage)
{
    juce::File file(filePath);
    if (!file.existsAsFile())
    {
        errorMessage = "File not found: " + filePath;
        return nullptr;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file));
    if (!reader)
    {
        errorMessage = "Unsupported or corrupted file: " + filePath;
        return nullptr;
    }

    auto buffer = std::unique_ptr<Buffer>(new Buffer());
    buffer->sampleRate_ = reader->sampleRate;
    buffer->filePath_ = filePath;

    // Use filename as name
    buffer->name_ = file.getFileName().toStdString();

    int numChannels = static_cast<int>(reader->numChannels);
    int lengthInSamples = static_cast<int>(reader->lengthInSamples);

    buffer->data_.setSize(numChannels, lengthInSamples);
    if (!reader->read(&buffer->data_, 0, lengthInSamples, 0, true, true))
    {
        errorMessage = "Failed to read audio data from: " + filePath;
        return nullptr;
    }

    // File-loaded buffers are fully written
    buffer->writePosition.store(lengthInSamples);

    SQ_LOG("Buffer::loadFromFile: '%s' %d ch, %d samples, %.0f Hz",
           filePath.c_str(), numChannels, lengthInSamples, reader->sampleRate);

    return buffer;
}

std::unique_ptr<Buffer> Buffer::createEmpty(
    int numChannels, int lengthInSamples, double sampleRate,
    const std::string& name)
{
    if (numChannels < 1 || lengthInSamples < 1 || sampleRate <= 0.0)
        return nullptr;

    auto buffer = std::unique_ptr<Buffer>(new Buffer());
    buffer->sampleRate_ = sampleRate;
    buffer->name_ = name;
    buffer->data_.setSize(numChannels, lengthInSamples);
    buffer->data_.clear();
    // writePosition starts at 0 for empty buffers (default)

    SQ_LOG("Buffer::createEmpty: '%s' %d ch, %d samples, %.0f Hz",
           name.c_str(), numChannels, lengthInSamples, sampleRate);

    return buffer;
}

const juce::AudioBuffer<float>& Buffer::getAudioData() const { return data_; }
juce::AudioBuffer<float>& Buffer::getAudioData() { return data_; }

const float* Buffer::getReadPointer(int channel) const
{
    return data_.getReadPointer(channel);
}

float* Buffer::getWritePointer(int channel)
{
    return data_.getWritePointer(channel);
}

int Buffer::getNumChannels() const { return data_.getNumChannels(); }
int Buffer::getLengthInSamples() const { return data_.getNumSamples(); }
double Buffer::getSampleRate() const { return sampleRate_; }

double Buffer::getLengthInSeconds() const
{
    if (sampleRate_ <= 0.0) return 0.0;
    return static_cast<double>(data_.getNumSamples()) / sampleRate_;
}

const std::string& Buffer::getName() const { return name_; }
const std::string& Buffer::getFilePath() const { return filePath_; }

void Buffer::clear()
{
    data_.clear();
    writePosition.store(0);
}

void Buffer::resize(int numChannels, int newLengthInSamples)
{
    // JUCE setSize with keepExistingContent=true, clearExtraSpace=true
    data_.setSize(numChannels, newLengthInSamples, true, true);
}

} // namespace squeeze
