#pragma once

#include "core/Processor.h"

#include <memory>
#include <vector>

namespace squeeze {

class Chain {
public:
    Chain();
    ~Chain();

    // Non-copyable, non-movable
    Chain(const Chain&) = delete;
    Chain& operator=(const Chain&) = delete;

    // --- Lifecycle (control thread) ---
    void prepare(double sampleRate, int blockSize);
    void release();

    // --- Structural modification (control thread only) ---
    void append(std::unique_ptr<Processor> p);
    void insert(int index, std::unique_ptr<Processor> p);
    std::unique_ptr<Processor> remove(int index);
    void move(int fromIndex, int toIndex);
    void clear();

    // --- Query ---
    int size() const;
    Processor* at(int index) const;
    Processor* findByHandle(int handle) const;
    int indexOf(Processor* p) const;

    // --- Latency ---
    int getLatencySamples() const;

    // --- Snapshot support ---
    std::vector<Processor*> getProcessorArray() const;

private:
    std::vector<std::unique_ptr<Processor>> processors_;
    double sampleRate_ = 0.0;
    int blockSize_ = 0;
};

} // namespace squeeze
