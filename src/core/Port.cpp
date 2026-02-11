#include "core/Port.h"

namespace squeeze {

bool isValid(const PortDescriptor& port)
{
    if (port.name.empty())
        return false;
    if (port.channels < 1)
        return false;
    if (port.signalType == SignalType::midi && port.channels != 1)
        return false;
    return true;
}

bool canConnect(const PortDescriptor& src, const PortDescriptor& dst)
{
    if (src.direction != PortDirection::output)
        return false;
    if (dst.direction != PortDirection::input)
        return false;
    if (src.signalType != dst.signalType)
        return false;
    // MIDI requires exact channel match (always 1).
    // Audio allows mismatched channel counts — the engine copies min(src, dst) channels.
    if (src.signalType == SignalType::midi && src.channels != dst.channels)
        return false;
    return true;
}

} // namespace squeeze
