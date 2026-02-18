#include "core/Chain.h"
#include "core/Logger.h"

#include <algorithm>

namespace squeeze {

Chain::Chain()
{
    SQ_DEBUG("Chain created");
}

Chain::~Chain()
{
    SQ_DEBUG("Chain destroyed, size=%d", (int)processors_.size());
}

void Chain::prepare(double sampleRate, int blockSize)
{
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;
    SQ_DEBUG("Chain::prepare: sr=%.0f bs=%d, forwarding to %d processors",
             sampleRate, blockSize, (int)processors_.size());
    for (auto& p : processors_)
        p->prepare(sampleRate, blockSize);
}

void Chain::release()
{
    SQ_DEBUG("Chain::release: forwarding to %d processors", (int)processors_.size());
    for (auto& p : processors_)
        p->release();
    sampleRate_ = 0.0;
    blockSize_ = 0;
}

void Chain::append(std::unique_ptr<Processor> p)
{
    if (!p) {
        SQ_WARN("Chain::append: null processor");
        return;
    }
    SQ_DEBUG("Chain::append: name=%s, new size=%d", p->getName().c_str(), (int)processors_.size() + 1);
    if (sampleRate_ > 0.0)
        p->prepare(sampleRate_, blockSize_);
    processors_.push_back(std::move(p));
}

void Chain::insert(int index, std::unique_ptr<Processor> p)
{
    if (!p) {
        SQ_WARN("Chain::insert: null processor");
        return;
    }

    // Clamp to valid range
    int sz = (int)processors_.size();
    if (index < 0) index = 0;
    if (index > sz) index = sz;

    SQ_DEBUG("Chain::insert: name=%s at index=%d, new size=%d",
             p->getName().c_str(), index, sz + 1);
    if (sampleRate_ > 0.0)
        p->prepare(sampleRate_, blockSize_);
    processors_.insert(processors_.begin() + index, std::move(p));
}

std::unique_ptr<Processor> Chain::remove(int index)
{
    if (index < 0 || index >= (int)processors_.size()) {
        SQ_DEBUG("Chain::remove: index=%d out of range (size=%d)", index, (int)processors_.size());
        return nullptr;
    }

    auto p = std::move(processors_[index]);
    processors_.erase(processors_.begin() + index);
    SQ_DEBUG("Chain::remove: name=%s from index=%d, new size=%d",
             p->getName().c_str(), index, (int)processors_.size());
    return p;
}

void Chain::move(int fromIndex, int toIndex)
{
    int sz = (int)processors_.size();
    if (fromIndex < 0 || fromIndex >= sz || toIndex < 0 || toIndex >= sz) {
        SQ_DEBUG("Chain::move: out of range from=%d to=%d (size=%d)", fromIndex, toIndex, sz);
        return;
    }
    if (fromIndex == toIndex)
        return;

    SQ_DEBUG("Chain::move: %d -> %d (name=%s)", fromIndex, toIndex,
             processors_[fromIndex]->getName().c_str());

    auto p = std::move(processors_[fromIndex]);
    processors_.erase(processors_.begin() + fromIndex);
    processors_.insert(processors_.begin() + toIndex, std::move(p));
}

void Chain::clear()
{
    SQ_DEBUG("Chain::clear: destroying %d processors", (int)processors_.size());
    processors_.clear();
}

int Chain::size() const
{
    return (int)processors_.size();
}

Processor* Chain::at(int index) const
{
    if (index < 0 || index >= (int)processors_.size())
        return nullptr;
    return processors_[index].get();
}

Processor* Chain::findByHandle(int handle) const
{
    for (auto& p : processors_) {
        if (p->getHandle() == handle)
            return p.get();
    }
    return nullptr;
}

int Chain::indexOf(Processor* p) const
{
    if (!p) return -1;
    for (int i = 0; i < (int)processors_.size(); ++i) {
        if (processors_[i].get() == p)
            return i;
    }
    return -1;
}

int Chain::getLatencySamples() const
{
    int total = 0;
    for (auto& p : processors_)
        total += p->getLatencySamples();
    return total;
}

std::vector<Processor*> Chain::getProcessorArray() const
{
    std::vector<Processor*> arr;
    arr.reserve(processors_.size());
    for (auto& p : processors_)
        arr.push_back(p.get());
    return arr;
}

} // namespace squeeze
