#include "core/Buffer.h"

namespace squeeze {

Buffer::Buffer() = default;
Buffer::~Buffer() = default;

std::unique_ptr<Buffer> Buffer::createEmpty(
    int numChannels, int lengthInSamples, double sampleRate,
    const std::string& name)
{
    if (numChannels < 1 || lengthInSamples < 1 || sampleRate <= 0.0)
    {
        SQ_WARN("Buffer::createEmpty: invalid params (ch=%d, len=%d, sr=%.1f)",
                numChannels, lengthInSamples, sampleRate);
        return nullptr;
    }

    auto buf = std::unique_ptr<Buffer>(new Buffer());
    buf->data_.setSize(numChannels, lengthInSamples);
    buf->data_.clear();
    buf->sampleRate_ = sampleRate;
    buf->name_ = name;
    buf->writePosition.store(0, std::memory_order_relaxed);

    SQ_INFO("Buffer::createEmpty: name=%s, ch=%d, len=%d, sr=%.1f",
            name.c_str(), numChannels, lengthInSamples, sampleRate);
    return buf;
}

std::unique_ptr<Buffer> Buffer::createFromData(
    juce::AudioBuffer<float>&& data, double sampleRate,
    const std::string& name, const std::string& filePath)
{
    if (data.getNumChannels() < 1 || data.getNumSamples() < 1 || sampleRate <= 0.0)
    {
        SQ_WARN("Buffer::createFromData: invalid params (ch=%d, len=%d, sr=%.1f)",
                data.getNumChannels(), data.getNumSamples(), sampleRate);
        return nullptr;
    }

    auto buf = std::unique_ptr<Buffer>(new Buffer());
    buf->data_ = std::move(data);
    buf->sampleRate_ = sampleRate;
    buf->name_ = name;
    buf->filePath_ = filePath;
    buf->writePosition.store(buf->data_.getNumSamples(), std::memory_order_relaxed);

    SQ_INFO("Buffer::createFromData: name=%s, ch=%d, len=%d, sr=%.1f, path=%s",
            name.c_str(), buf->data_.getNumChannels(), buf->data_.getNumSamples(),
            sampleRate, filePath.c_str());
    return buf;
}

const float* Buffer::getReadPointer(int channel) const
{
    if (channel < 0 || channel >= data_.getNumChannels())
        return nullptr;
    return data_.getReadPointer(channel);
}

float* Buffer::getWritePointer(int channel)
{
    if (channel < 0 || channel >= data_.getNumChannels())
        return nullptr;
    return data_.getWritePointer(channel);
}

int Buffer::getNumChannels() const { return data_.getNumChannels(); }
int Buffer::getLengthInSamples() const { return data_.getNumSamples(); }
double Buffer::getSampleRate() const { return sampleRate_; }

double Buffer::getLengthInSeconds() const
{
    return static_cast<double>(data_.getNumSamples()) / sampleRate_;
}

const std::string& Buffer::getName() const { return name_; }
const std::string& Buffer::getFilePath() const { return filePath_; }

void Buffer::clear()
{
    SQ_DEBUG("Buffer::clear: name=%s", name_.c_str());
    data_.clear();
    writePosition.store(0, std::memory_order_release);
}

} // namespace squeeze
