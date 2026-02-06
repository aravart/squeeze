#include "core/Scheduler.h"

namespace squeeze {

bool Scheduler::sendCommand(const Command& cmd)
{
    return commandQueue_.tryPush(cmd);
}

bool Scheduler::sendGarbage(GarbageItem item)
{
    return garbageQueue_.tryPush(item);
}

void Scheduler::collectGarbage()
{
    GarbageItem item;
    while (garbageQueue_.tryPop(item))
        item.destroy();
}

} // namespace squeeze
