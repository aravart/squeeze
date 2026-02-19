#include "core/MidiRouter.h"
#include "core/Logger.h"
#include <algorithm>

namespace squeeze {

MidiRouter::MidiRouter() = default;

MidiRouter::~MidiRouter()
{
    delete activeTable_.load(std::memory_order_relaxed);
    delete pendingGarbage_;
}

// --- Device queue management ---

bool MidiRouter::createDeviceQueue(const std::string& deviceName, std::string& error)
{
    if (devices_.count(deviceName)) {
        SQ_DEBUG("MidiRouter: device queue already exists: %s", deviceName.c_str());
        return true;
    }
    devices_[deviceName] = std::make_unique<DeviceState>();
    SQ_DEBUG("MidiRouter: created device queue: %s", deviceName.c_str());
    return true;
}

void MidiRouter::removeDeviceQueue(const std::string& deviceName)
{
    auto it = devices_.find(deviceName);
    if (it == devices_.end()) {
        SQ_DEBUG("MidiRouter: removeDeviceQueue no-op, unknown device: %s", deviceName.c_str());
        return;
    }

    removeRoutesForDevice(deviceName);
    retiredDevices_.push_back(std::move(it->second));
    devices_.erase(it);
    SQ_DEBUG("MidiRouter: removed device queue: %s", deviceName.c_str());
}

bool MidiRouter::hasDeviceQueue(const std::string& deviceName) const
{
    return devices_.count(deviceName) > 0;
}

// --- Routing ---

int MidiRouter::addRoute(const std::string& deviceName, int nodeId,
                         int channelFilter, int noteLow, int noteHigh,
                         std::string& error)
{
    if (!devices_.count(deviceName)) {
        error = "device not registered: " + deviceName;
        SQ_WARN("MidiRouter: addRoute failed — %s", error.c_str());
        return -1;
    }
    if (channelFilter < 0 || channelFilter > 16) {
        error = "invalid channel filter: " + std::to_string(channelFilter);
        SQ_WARN("MidiRouter: addRoute failed — %s", error.c_str());
        return -1;
    }
    if (noteLow < 0 || noteLow > 127 || noteHigh < 0 || noteHigh > 127 || noteLow > noteHigh) {
        error = "invalid note range: " + std::to_string(noteLow) + "-" + std::to_string(noteHigh);
        SQ_WARN("MidiRouter: addRoute failed — %s", error.c_str());
        return -1;
    }

    int id = nextRouteId_++;
    stagedRoutes_.push_back({id, deviceName, nodeId, channelFilter, noteLow, noteHigh});
    SQ_DEBUG("MidiRouter: added route %d: %s -> node %d (ch=%d, notes=%d-%d)",
             id, deviceName.c_str(), nodeId, channelFilter, noteLow, noteHigh);
    return id;
}

bool MidiRouter::removeRoute(int routeId)
{
    auto it = std::find_if(stagedRoutes_.begin(), stagedRoutes_.end(),
                           [routeId](const MidiRoute& r) { return r.id == routeId; });
    if (it == stagedRoutes_.end())
        return false;

    SQ_DEBUG("MidiRouter: removed route %d", routeId);
    stagedRoutes_.erase(it);
    return true;
}

bool MidiRouter::removeRoutesForNode(int nodeId)
{
    auto oldSize = stagedRoutes_.size();
    stagedRoutes_.erase(
        std::remove_if(stagedRoutes_.begin(), stagedRoutes_.end(),
                       [nodeId](const MidiRoute& r) { return r.nodeId == nodeId; }),
        stagedRoutes_.end());
    bool removed = stagedRoutes_.size() != oldSize;
    if (removed)
        SQ_DEBUG("MidiRouter: removed routes for node %d", nodeId);
    return removed;
}

bool MidiRouter::removeRoutesForDevice(const std::string& deviceName)
{
    auto oldSize = stagedRoutes_.size();
    stagedRoutes_.erase(
        std::remove_if(stagedRoutes_.begin(), stagedRoutes_.end(),
                       [&deviceName](const MidiRoute& r) { return r.deviceName == deviceName; }),
        stagedRoutes_.end());
    bool removed = stagedRoutes_.size() != oldSize;
    if (removed)
        SQ_DEBUG("MidiRouter: removed routes for device %s", deviceName.c_str());
    return removed;
}

std::vector<MidiRoute> MidiRouter::getRoutes() const
{
    return stagedRoutes_;
}

void MidiRouter::commit()
{
    // Build new routing table with device queue pointers
    auto* newTable = new RoutingTable();

    // Collect unique devices referenced by routes
    std::unordered_map<std::string, int> deviceIndexMap;
    for (auto& route : stagedRoutes_) {
        if (deviceIndexMap.count(route.deviceName))
            continue;
        auto it = devices_.find(route.deviceName);
        if (it == devices_.end())
            continue; // shouldn't happen — addRoute validates
        int idx = static_cast<int>(newTable->deviceRefs.size());
        deviceIndexMap[route.deviceName] = idx;
        newTable->deviceRefs.push_back({&it->second->queue});
    }

    // Build route entries
    for (auto& route : stagedRoutes_) {
        auto dimIt = deviceIndexMap.find(route.deviceName);
        if (dimIt == deviceIndexMap.end())
            continue;
        newTable->entries.push_back({dimIt->second, route.nodeId,
                                     route.channelFilter, route.noteLow, route.noteHigh});
    }

    // Garbage collect: delete the table from 2 commits ago
    delete pendingGarbage_;

    // Previous retired devices are now safe (2 commits have passed)
    previousRetiredDevices_.clear();

    // Swap active table
    pendingGarbage_ = activeTable_.exchange(newTable, std::memory_order_acq_rel);

    // Current retired devices become previous (need 1 more commit)
    previousRetiredDevices_ = std::move(retiredDevices_);

    SQ_DEBUG("MidiRouter: committed routing table (%d devices, %d routes)",
             (int)newTable->deviceRefs.size(), (int)newTable->entries.size());
}

// --- MIDI input ---

bool MidiRouter::pushMidiEvent(const std::string& deviceName, const MidiEvent& event)
{
    auto it = devices_.find(deviceName);
    if (it == devices_.end())
        return false;

    if (!it->second->queue.tryPush(event)) {
        it->second->droppedCount.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

// --- Audio thread ---

void MidiRouter::dispatch(const std::unordered_map<int, juce::MidiBuffer*>& nodeBuffers,
                          int /*numSamples*/)
{
    auto* table = activeTable_.load(std::memory_order_acquire);
    if (!table)
        return;

    // For each device, drain its queue and deliver to matching routes
    for (int i = 0; i < static_cast<int>(table->deviceRefs.size()); ++i) {
        auto* queue = table->deviceRefs[i].queue;
        MidiEvent event;
        while (queue->tryPop(event)) {
            for (auto& route : table->entries) {
                if (route.deviceIndex != i)
                    continue;
                if (!matchesFilter(event, route))
                    continue;
                auto bufIt = nodeBuffers.find(route.nodeId);
                if (bufIt != nodeBuffers.end() && bufIt->second != nullptr)
                    bufIt->second->addEvent(event.data, event.size, 0);
            }
        }
    }
}

// --- Filtering ---

bool MidiRouter::matchesFilter(const MidiEvent& event,
                                const RoutingTable::RouteEntry& route)
{
    if (event.size < 1)
        return false;

    uint8_t status = event.data[0];

    // System messages (>= 0xF0) bypass channel and note filters
    if (status >= 0xF0)
        return true;

    // Channel filter
    if (route.channelFilter != 0) {
        int channel = (status & 0x0F) + 1; // MIDI channels 1-16
        if (channel != route.channelFilter)
            return false;
    }

    // Note range filter — only applies to note-related messages
    if (!(route.noteLow == 0 && route.noteHigh == 127)) {
        uint8_t type = status & 0xF0;
        // Note Off (0x80), Note On (0x90), Polyphonic Aftertouch (0xA0)
        if (type == 0x80 || type == 0x90 || type == 0xA0) {
            if (event.size >= 2) {
                int note = event.data[1];
                if (note < route.noteLow || note > route.noteHigh)
                    return false;
            }
        }
        // Non-note messages (CC, program change, pitch bend, etc.) pass through
    }

    return true;
}

// --- Monitoring ---

int MidiRouter::getQueueFillLevel(const std::string& deviceName) const
{
    auto it = devices_.find(deviceName);
    if (it == devices_.end())
        return 0;
    return it->second->queue.size();
}

int MidiRouter::getDroppedCount(const std::string& deviceName) const
{
    auto it = devices_.find(deviceName);
    if (it == devices_.end())
        return 0;
    return it->second->droppedCount.load(std::memory_order_relaxed);
}

void MidiRouter::resetDroppedCounts()
{
    for (auto& [name, state] : devices_)
        state->droppedCount.store(0, std::memory_order_relaxed);
}

} // namespace squeeze
