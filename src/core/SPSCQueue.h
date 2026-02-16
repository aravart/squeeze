#pragma once

#include <array>
#include <atomic>

namespace squeeze {

template<typename T, int Capacity>
class SPSCQueue {
    static_assert(Capacity > 0, "Capacity must be positive");

    std::array<T, Capacity + 1> buffer_;
    std::atomic<int> readPos_{0};
    std::atomic<int> writePos_{0};

    int next(int pos) const { return (pos + 1) % (Capacity + 1); }

public:
    bool tryPush(const T& item)
    {
        int write = writePos_.load(std::memory_order_relaxed);
        int nextWrite = next(write);
        if (nextWrite == readPos_.load(std::memory_order_acquire))
            return false;
        buffer_[write] = item;
        writePos_.store(nextWrite, std::memory_order_release);
        return true;
    }

    bool tryPop(T& item)
    {
        int read = readPos_.load(std::memory_order_relaxed);
        if (read == writePos_.load(std::memory_order_acquire))
            return false;
        item = buffer_[read];
        readPos_.store(next(read), std::memory_order_release);
        return true;
    }

    int size() const
    {
        int write = writePos_.load(std::memory_order_acquire);
        int read = readPos_.load(std::memory_order_acquire);
        int diff = write - read;
        return diff >= 0 ? diff : diff + (Capacity + 1);
    }

    bool empty() const { return size() == 0; }

    void reset()
    {
        readPos_.store(0, std::memory_order_relaxed);
        writePos_.store(0, std::memory_order_relaxed);
    }
};

} // namespace squeeze
