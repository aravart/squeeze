#pragma once

#include "core/SPSCQueue.h"
#include "core/Logger.h"
#include <cstdint>

namespace squeeze {

struct Command {
    enum class Type {
        swapSnapshot,
        transportPlay,
        transportStop,
        transportPause,
        setTempo,
        setTimeSignature,
        seekSamples,
        seekBeats,
        setLoopPoints,
        setLooping
    };

    Type type;

    void* ptr = nullptr;
    double doubleValue1 = 0.0;
    double doubleValue2 = 0.0;
    int64_t int64Value = 0;
    int intValue1 = 0;
    int intValue2 = 0;
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
        return {p, [](void* raw) { delete static_cast<T*>(raw); }};
    }
};

inline const char* commandTypeName(Command::Type type)
{
    switch (type) {
        case Command::Type::swapSnapshot:     return "swapSnapshot";
        case Command::Type::transportPlay:    return "transportPlay";
        case Command::Type::transportStop:    return "transportStop";
        case Command::Type::transportPause:   return "transportPause";
        case Command::Type::setTempo:         return "setTempo";
        case Command::Type::setTimeSignature: return "setTimeSignature";
        case Command::Type::seekSamples:      return "seekSamples";
        case Command::Type::seekBeats:        return "seekBeats";
        case Command::Type::setLoopPoints:    return "setLoopPoints";
        case Command::Type::setLooping:       return "setLooping";
    }
    return "unknown";
}

class CommandQueue {
public:
    CommandQueue() = default;
    ~CommandQueue() = default;

    CommandQueue(const CommandQueue&) = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;

    // --- Control thread ---
    bool sendCommand(const Command& cmd)
    {
        if (!commandQueue_.tryPush(cmd)) {
            SQ_WARN("CommandQueue: command queue full, dropping %s", commandTypeName(cmd.type));
            return false;
        }
        SQ_DEBUG("CommandQueue: sent %s", commandTypeName(cmd.type));
        return true;
    }

    // --- Audio thread ---
    template<typename Handler>
    int processPending(Handler&& handler)
    {
        int count = 0;
        Command cmd;
        while (commandQueue_.tryPop(cmd)) {
            handler(cmd);
            ++count;
        }
        return count;
    }

    bool sendGarbage(const GarbageItem& item)
    {
        if (!garbageQueue_.tryPush(item)) {
            SQ_WARN_RT("CommandQueue: garbage queue full, item leaked");
            return false;
        }
        return true;
    }

    // --- Control thread ---
    int collectGarbage()
    {
        int count = 0;
        GarbageItem item;
        while (garbageQueue_.tryPop(item)) {
            item.destroy();
            ++count;
        }
        return count;
    }

private:
    static constexpr int kCommandCapacity = 256;
    static constexpr int kGarbageCapacity = 256;

    SPSCQueue<Command, kCommandCapacity> commandQueue_;
    SPSCQueue<GarbageItem, kGarbageCapacity> garbageQueue_;
};

} // namespace squeeze
