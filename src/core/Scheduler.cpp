#include "core/Scheduler.h"
#include "core/Logger.h"

namespace squeeze {

bool Scheduler::sendCommand(const Command& cmd)
{
    SQ_LOG("sendCommand: type=%d", (int)cmd.type);
    return commandQueue_.tryPush(cmd);
}

bool Scheduler::sendGarbage(GarbageItem item)
{
    return garbageQueue_.tryPush(item);
}

void Scheduler::collectGarbage()
{
    int count = 0;
    GarbageItem item;
    while (garbageQueue_.tryPop(item))
    {
        item.destroy();
        ++count;
    }
    if (count > 0)
        SQ_LOG("collectGarbage: %d items", count);
}

} // namespace squeeze
