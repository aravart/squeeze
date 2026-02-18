#include "core/Bus.h"
#include "core/Logger.h"

#include <algorithm>
#include <cmath>

namespace squeeze {

Bus::Bus(const std::string& name, bool isMaster)
    : name_(name), master_(isMaster)
{
    SQ_DEBUG("Bus created: name=%s master=%d", name_.c_str(), (int)master_);
}

Bus::~Bus()
{
    SQ_DEBUG("Bus destroyed: name=%s", name_.c_str());
}

void Bus::prepare(double sampleRate, int blockSize)
{
    SQ_DEBUG("Bus::prepare: name=%s sr=%.0f bs=%d", name_.c_str(), sampleRate, blockSize);
    chain_.prepare(sampleRate, blockSize);
}

void Bus::release()
{
    SQ_DEBUG("Bus::release: name=%s", name_.c_str());
    chain_.release();
}

const std::string& Bus::getName() const { return name_; }
int Bus::getHandle() const { return handle_; }
void Bus::setHandle(int h) { handle_ = h; }
bool Bus::isMaster() const { return master_; }

Chain& Bus::getChain() { return chain_; }
const Chain& Bus::getChain() const { return chain_; }

void Bus::setGain(float linear)
{
    if (linear < 0.0f) linear = 0.0f;
    gain_.store(linear, std::memory_order_relaxed);
}

float Bus::getGain() const
{
    return gain_.load(std::memory_order_relaxed);
}

void Bus::setPan(float pan)
{
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    pan_.store(pan, std::memory_order_relaxed);
}

float Bus::getPan() const
{
    return pan_.load(std::memory_order_relaxed);
}

void Bus::routeTo(Bus* bus)
{
    if (master_) {
        SQ_WARN("Bus::routeTo: Master bus cannot route to another bus");
        return;
    }
    if (!bus) {
        SQ_WARN("Bus::routeTo: null bus, ignoring");
        return;
    }
    SQ_DEBUG("Bus::routeTo: name=%s -> %s", name_.c_str(), bus->getName().c_str());
    outputBus_ = bus;
}

Bus* Bus::getOutputBus() const { return outputBus_; }

int Bus::addSend(Bus* bus, float levelDb, SendTap tap)
{
    if (!bus) {
        SQ_WARN("Bus::addSend: null bus");
        return -1;
    }
    int id = nextSendId_++;
    sends_.push_back({bus, levelDb, tap, id});
    SQ_DEBUG("Bus::addSend: name=%s sendId=%d level=%.1f tap=%s",
             name_.c_str(), id, levelDb,
             tap == SendTap::preFader ? "pre" : "post");
    return id;
}

bool Bus::removeSend(int sendId)
{
    auto it = std::find_if(sends_.begin(), sends_.end(),
                           [sendId](const Send& s) { return s.id == sendId; });
    if (it == sends_.end()) {
        SQ_DEBUG("Bus::removeSend: sendId=%d not found", sendId);
        return false;
    }
    sends_.erase(it);
    SQ_DEBUG("Bus::removeSend: name=%s sendId=%d removed", name_.c_str(), sendId);
    return true;
}

void Bus::setSendLevel(int sendId, float levelDb)
{
    for (auto& s : sends_) {
        if (s.id == sendId) {
            s.levelDb = levelDb;
            SQ_DEBUG("Bus::setSendLevel: sendId=%d level=%.1f", sendId, levelDb);
            return;
        }
    }
    SQ_DEBUG("Bus::setSendLevel: sendId=%d not found", sendId);
}

void Bus::setSendTap(int sendId, SendTap tap)
{
    for (auto& s : sends_) {
        if (s.id == sendId) {
            s.tap = tap;
            SQ_DEBUG("Bus::setSendTap: sendId=%d tap=%s",
                     sendId, tap == SendTap::preFader ? "pre" : "post");
            return;
        }
    }
    SQ_DEBUG("Bus::setSendTap: sendId=%d not found", sendId);
}

std::vector<Send> Bus::getSends() const { return sends_; }

void Bus::setBypassed(bool bypassed)
{
    bypassed_.store(bypassed, std::memory_order_relaxed);
}

bool Bus::isBypassed() const
{
    return bypassed_.load(std::memory_order_relaxed);
}

float Bus::getPeak() const
{
    return peak_.load(std::memory_order_relaxed);
}

float Bus::getRMS() const
{
    return rms_.load(std::memory_order_relaxed);
}

void Bus::updateMetering(const juce::AudioBuffer<float>& buffer, int numSamples)
{
    float peak = 0.0f;
    float sumSq = 0.0f;
    int totalSamples = 0;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const float* data = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i) {
            float s = std::fabs(data[i]);
            if (s > peak) peak = s;
            sumSq += data[i] * data[i];
        }
        totalSamples += numSamples;
    }

    peak_.store(peak, std::memory_order_relaxed);
    rms_.store(totalSamples > 0 ? std::sqrt(sumSq / (float)totalSamples) : 0.0f,
               std::memory_order_relaxed);
}

void Bus::resetMetering()
{
    peak_.store(0.0f, std::memory_order_relaxed);
    rms_.store(0.0f, std::memory_order_relaxed);
}

int Bus::getLatencySamples() const
{
    return chain_.getLatencySamples();
}

} // namespace squeeze
