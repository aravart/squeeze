#include "core/MidiRouter.h"
#include "core/Logger.h"

namespace squeeze {

MidiRouter::MidiRouter() = default;

MidiRouter::~MidiRouter()
{
    // Close all devices
    for (auto& dev : devices_)
    {
        if (dev->device)
        {
            dev->device->stop();
            SQ_LOG("MidiRouter: closed device '%s'", dev->name.c_str());
        }
    }

    // Clean up routing tables
    delete activeTable_.load(std::memory_order_relaxed);
}

// ============================================================
// Device management (control thread)
// ============================================================

std::vector<std::string> MidiRouter::getAvailableDevices() const
{
    std::vector<std::string> result;
    auto devices = juce::MidiInput::getAvailableDevices();
    for (const auto& dev : devices)
        result.push_back(dev.name.toStdString());
    return result;
}

MidiRouter::OpenDevice* MidiRouter::findDevice(const std::string& name)
{
    for (auto& dev : devices_)
        if (dev->name == name)
            return dev.get();
    return nullptr;
}

const MidiRouter::OpenDevice* MidiRouter::findDevice(const std::string& name) const
{
    for (auto& dev : devices_)
        if (dev->name == name)
            return dev.get();
    return nullptr;
}

bool MidiRouter::openDevice(const std::string& deviceName, std::string& error)
{
    // Already open?
    if (findDevice(deviceName))
        return true;

    auto available = juce::MidiInput::getAvailableDevices();
    for (const auto& dev : available)
    {
        if (dev.name.toStdString() == deviceName)
        {
            auto openDev = std::make_unique<OpenDevice>();
            openDev->name = deviceName;
            openDev->identifier = dev.identifier.toStdString();
            openDev->device = juce::MidiInput::openDevice(dev.identifier, this);

            if (!openDev->device)
            {
                error = "Failed to open MIDI device '" + deviceName + "'";
                return false;
            }

            openDev->device->start();
            SQ_LOG("MidiRouter: opened device '%s'", deviceName.c_str());
            devices_.push_back(std::move(openDev));
            return true;
        }
    }

    error = "MIDI device '" + deviceName + "' not found";
    return false;
}

void MidiRouter::closeDevice(const std::string& deviceName)
{
    // Remove all routes for this device first
    routes_.erase(
        std::remove_if(routes_.begin(), routes_.end(),
            [&](const MidiRoute& r) { return r.deviceName == deviceName; }),
        routes_.end());

    for (auto it = devices_.begin(); it != devices_.end(); ++it)
    {
        if ((*it)->name == deviceName)
        {
            if ((*it)->device)
            {
                (*it)->device->stop();
                SQ_LOG("MidiRouter: closed device '%s'", deviceName.c_str());
            }
            devices_.erase(it);
            return;
        }
    }
}

bool MidiRouter::isDeviceOpen(const std::string& deviceName) const
{
    return findDevice(deviceName) != nullptr;
}

std::vector<std::string> MidiRouter::getOpenDevices() const
{
    std::vector<std::string> result;
    for (auto& dev : devices_)
        result.push_back(dev->name);
    return result;
}

// ============================================================
// Routing (control thread)
// ============================================================

int MidiRouter::addRoute(const std::string& deviceName, int nodeId,
                         int channelFilter, std::string& error)
{
    if (!findDevice(deviceName))
    {
        error = "Device '" + deviceName + "' is not open";
        return -1;
    }

    int id = nextRouteId_++;
    routes_.push_back({id, deviceName, nodeId, channelFilter});
    SQ_LOG("MidiRouter: route %d: '%s' -> node %d (ch=%d)",
           id, deviceName.c_str(), nodeId, channelFilter);
    return id;
}

bool MidiRouter::removeRoute(int routeId)
{
    for (auto it = routes_.begin(); it != routes_.end(); ++it)
    {
        if (it->id == routeId)
        {
            SQ_LOG("MidiRouter: removed route %d", routeId);
            routes_.erase(it);
            return true;
        }
    }
    return false;
}

bool MidiRouter::removeRoutesForNode(int nodeId)
{
    auto newEnd = std::remove_if(routes_.begin(), routes_.end(),
        [nodeId](const MidiRoute& r) { return r.nodeId == nodeId; });
    bool removed = (newEnd != routes_.end());
    routes_.erase(newEnd, routes_.end());
    return removed;
}

std::vector<MidiRoute> MidiRouter::getRoutes() const
{
    return routes_;
}

// ============================================================
// Commit (control thread -> audio thread)
// ============================================================

void MidiRouter::commit()
{
    auto* table = new RoutingTable();

    // Build device refs
    for (auto& dev : devices_)
    {
        table->deviceRefs.push_back({
            &dev->queue,
            &dev->droppedCount,
            dev->name
        });
    }

    // Build a name-to-index map for device lookup
    std::unordered_map<std::string, int> nameToIndex;
    for (int i = 0; i < (int)devices_.size(); ++i)
        nameToIndex[devices_[i]->name] = i;

    // Build route entries
    for (const auto& route : routes_)
    {
        auto it = nameToIndex.find(route.deviceName);
        if (it != nameToIndex.end())
        {
            table->entries.push_back({
                it->second,
                route.nodeId,
                route.channelFilter
            });
        }
    }

    // Atomic swap
    auto* old = activeTable_.exchange(table, std::memory_order_acq_rel);
    if (old)
        pendingTableDeletions_.emplace_back(old);

    SQ_LOG("MidiRouter: committed %d routes, %d devices",
           (int)table->entries.size(), (int)table->deviceRefs.size());
}

// ============================================================
// Audio thread dispatch
// ============================================================

void MidiRouter::dispatchMidi(
    const std::unordered_map<int, juce::MidiBuffer*>& nodeBuffers)
{
    auto* table = activeTable_.load(std::memory_order_acquire);
    if (!table || table->entries.empty())
        return;

    // Drain each device queue into a temporary buffer, then distribute
    int numDevices = (int)table->deviceRefs.size();

    // We process one device at a time, drain into scratch, then distribute
    for (int devIdx = 0; devIdx < numDevices; ++devIdx)
    {
        auto& devRef = table->deviceRefs[devIdx];
        deviceDrainBuffer_.clear();

        MidiEvent event;
        while (devRef.queue->tryPop(event))
            deviceDrainBuffer_.addEvent(event.data, event.size, 0);

        if (deviceDrainBuffer_.isEmpty())
            continue;

        // Distribute to all routes from this device
        for (const auto& entry : table->entries)
        {
            if (entry.deviceIndex != devIdx)
                continue;

            auto it = nodeBuffers.find(entry.nodeId);
            if (it == nodeBuffers.end())
                continue;

            juce::MidiBuffer* dest = it->second;

            if (entry.channelFilter == 0)
            {
                // No filtering — add all events
                for (const auto metadata : deviceDrainBuffer_)
                    dest->addEvent(metadata.getMessage(), metadata.samplePosition);
            }
            else
            {
                // Channel filter
                for (const auto metadata : deviceDrainBuffer_)
                {
                    auto msg = metadata.getMessage();
                    if (msg.getChannel() == 0 || msg.getChannel() == entry.channelFilter)
                        dest->addEvent(msg, metadata.samplePosition);
                }
            }
        }
    }
}

// ============================================================
// MIDI thread callback
// ============================================================

void MidiRouter::handleIncomingMidiMessage(juce::MidiInput* source,
                                            const juce::MidiMessage& message)
{
    if (message.isSysEx())
        return;

    int size = message.getRawDataSize();
    if (size < 1 || size > 3)
        return;

    MidiEvent event;
    event.size = size;
    auto* raw = message.getRawData();
    for (int i = 0; i < size; ++i)
        event.data[i] = raw[i];

    // Find which device this came from by matching the MidiInput pointer
    for (auto& dev : devices_)
    {
        if (dev->device.get() == source)
        {
            if (!dev->queue.tryPush(event))
                dev->droppedCount.fetch_add(1, std::memory_order_relaxed);

            if (message.isNoteOn())
                SQ_LOG_RT_TRACE("MIDI [%s] note-on  ch=%d note=%d vel=%d",
                          dev->name.c_str(), message.getChannel(),
                          message.getNoteNumber(), (int)message.getVelocity());
            else if (message.isNoteOff())
                SQ_LOG_RT_TRACE("MIDI [%s] note-off ch=%d note=%d",
                          dev->name.c_str(), message.getChannel(),
                          message.getNoteNumber());
            else if (message.isController())
                SQ_LOG_RT_TRACE("MIDI [%s] cc       ch=%d cc=%d val=%d",
                          dev->name.c_str(), message.getChannel(),
                          message.getControllerNumber(), message.getControllerValue());
            else if (message.isPitchWheel())
                SQ_LOG_RT_TRACE("MIDI [%s] pitch    ch=%d val=%d",
                          dev->name.c_str(), message.getChannel(),
                          message.getPitchWheelValue());
            else if (message.isChannelPressure())
                SQ_LOG_RT_TRACE("MIDI [%s] pressure ch=%d val=%d",
                          dev->name.c_str(), message.getChannel(),
                          message.getChannelPressureValue());
            else if (message.isProgramChange())
                SQ_LOG_RT_TRACE("MIDI [%s] pgm      ch=%d pgm=%d",
                          dev->name.c_str(), message.getChannel(),
                          message.getProgramChangeNumber());
            else
                SQ_LOG_RT_TRACE("MIDI [%s] status=0x%02x size=%d",
                          dev->name.c_str(), (unsigned)event.data[0], size);
            return;
        }
    }
}

// ============================================================
// Monitoring
// ============================================================

std::vector<MidiRouter::DeviceStats> MidiRouter::getDeviceStats() const
{
    std::vector<DeviceStats> result;
    for (auto& dev : devices_)
    {
        result.push_back({
            dev->name,
            dev->queue.size(),
            dev->droppedCount.load(std::memory_order_relaxed)
        });
    }
    return result;
}

} // namespace squeeze
