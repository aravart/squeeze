#include "core/Source.h"
#include "core/Logger.h"

#include <algorithm>

namespace squeeze {

Source::Source(const std::string& name, std::unique_ptr<Processor> generator)
    : name_(name), generator_(std::move(generator))
{
    SQ_DEBUG("Source created: name=%s", name_.c_str());
}

Source::~Source()
{
    SQ_DEBUG("Source destroyed: name=%s", name_.c_str());
}

void Source::prepare(double sampleRate, int blockSize)
{
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;
    SQ_DEBUG("Source::prepare: name=%s sr=%.0f bs=%d", name_.c_str(), sampleRate, blockSize);
    generator_->prepare(sampleRate, blockSize);
    chain_.prepare(sampleRate, blockSize);
}

void Source::release()
{
    SQ_DEBUG("Source::release: name=%s", name_.c_str());
    generator_->release();
    chain_.release();
    sampleRate_ = 0.0;
    blockSize_ = 0;
}

const std::string& Source::getName() const { return name_; }
int Source::getHandle() const { return handle_; }
void Source::setHandle(int h) { handle_ = h; }

Processor* Source::getGenerator() const { return generator_.get(); }

void Source::setGenerator(std::unique_ptr<Processor> generator)
{
    if (!generator) {
        SQ_WARN("Source::setGenerator: null processor, ignoring");
        return;
    }
    SQ_DEBUG("Source::setGenerator: name=%s, old=%s new=%s",
             name_.c_str(), generator_->getName().c_str(), generator->getName().c_str());
    if (sampleRate_ > 0.0)
        generator->prepare(sampleRate_, blockSize_);
    generator_ = std::move(generator);
}

Chain& Source::getChain() { return chain_; }
const Chain& Source::getChain() const { return chain_; }

void Source::setGain(float linear)
{
    if (linear < 0.0f) linear = 0.0f;
    gain_.store(linear, std::memory_order_relaxed);
}

float Source::getGain() const
{
    return gain_.load(std::memory_order_relaxed);
}

void Source::setPan(float pan)
{
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    pan_.store(pan, std::memory_order_relaxed);
}

float Source::getPan() const
{
    return pan_.load(std::memory_order_relaxed);
}

void Source::routeTo(Bus* bus)
{
    if (!bus) {
        SQ_WARN("Source::routeTo: null bus, ignoring");
        return;
    }
    SQ_DEBUG("Source::routeTo: name=%s bus=%p", name_.c_str(), (void*)bus);
    outputBus_ = bus;
}

Bus* Source::getOutputBus() const { return outputBus_; }

int Source::addSend(Bus* bus, float levelDb, SendTap tap)
{
    if (!bus) {
        SQ_WARN("Source::addSend: null bus");
        return -1;
    }
    int id = nextSendId_++;
    sends_.push_back({bus, levelDb, tap, id});
    SQ_DEBUG("Source::addSend: name=%s sendId=%d level=%.1f tap=%s",
             name_.c_str(), id, levelDb,
             tap == SendTap::preFader ? "pre" : "post");
    return id;
}

bool Source::removeSend(int sendId)
{
    auto it = std::find_if(sends_.begin(), sends_.end(),
                           [sendId](const Send& s) { return s.id == sendId; });
    if (it == sends_.end()) {
        SQ_DEBUG("Source::removeSend: sendId=%d not found", sendId);
        return false;
    }
    sends_.erase(it);
    SQ_DEBUG("Source::removeSend: name=%s sendId=%d removed", name_.c_str(), sendId);
    return true;
}

void Source::setSendLevel(int sendId, float levelDb)
{
    for (auto& s : sends_) {
        if (s.id == sendId) {
            s.levelDb = levelDb;
            SQ_DEBUG("Source::setSendLevel: sendId=%d level=%.1f", sendId, levelDb);
            return;
        }
    }
    SQ_DEBUG("Source::setSendLevel: sendId=%d not found", sendId);
}

void Source::setSendTap(int sendId, SendTap tap)
{
    for (auto& s : sends_) {
        if (s.id == sendId) {
            s.tap = tap;
            SQ_DEBUG("Source::setSendTap: sendId=%d tap=%s",
                     sendId, tap == SendTap::preFader ? "pre" : "post");
            return;
        }
    }
    SQ_DEBUG("Source::setSendTap: sendId=%d not found", sendId);
}

std::vector<Send> Source::getSends() const { return sends_; }

void Source::setMidiAssignment(const MidiAssignment& assignment)
{
    SQ_DEBUG("Source::setMidiAssignment: name=%s device=%s ch=%d notes=%d-%d",
             name_.c_str(), assignment.device.c_str(), assignment.channel,
             assignment.noteLow, assignment.noteHigh);
    midiAssignment_ = assignment;
}

MidiAssignment Source::getMidiAssignment() const { return midiAssignment_; }

void Source::setBypassed(bool bypassed)
{
    bypassed_.store(bypassed, std::memory_order_relaxed);
}

bool Source::isBypassed() const
{
    return bypassed_.load(std::memory_order_relaxed);
}

void Source::process(juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi)
{
    generator_->process(buffer, midi);

    auto procs = chain_.getProcessorArray();
    for (auto* p : procs)
        p->process(buffer);
}

int Source::getLatencySamples() const
{
    return generator_->getLatencySamples() + chain_.getLatencySamples();
}

} // namespace squeeze
