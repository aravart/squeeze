#pragma once

#include "core/SPSCQueue.h"

namespace squeeze {

class Node;

struct Command {
    enum class Type { swapGraph, setParameter };
    Type type = Type::swapGraph;
    void* ptr = nullptr;
    Node* node = nullptr;
    int paramIndex = 0;
    float paramValue = 0.0f;
};

struct GarbageItem {
    void* ptr = nullptr;
    void (*deleter)(void*) = nullptr;

    void destroy()
    {
        if (ptr && deleter)
            deleter(ptr);
        ptr = nullptr;
    }

    template<typename T>
    static GarbageItem wrap(T* p)
    {
        return {
            static_cast<void*>(p),
            [](void* raw) { delete static_cast<T*>(raw); }
        };
    }
};

class Scheduler {
    static constexpr int CommandQueueSize = 256;
    static constexpr int GarbageQueueSize = 256;

    SPSCQueue<Command, CommandQueueSize> commandQueue_;
    SPSCQueue<GarbageItem, GarbageQueueSize> garbageQueue_;

public:
    // Control thread
    bool sendCommand(const Command& cmd);
    void collectGarbage();

    // Audio thread
    template<typename Handler>
    int processPending(Handler&& handler)
    {
        int count = 0;
        Command cmd;
        while (commandQueue_.tryPop(cmd))
        {
            handler(cmd);
            ++count;
        }
        return count;
    }

    bool sendGarbage(GarbageItem item);
};

} // namespace squeeze
