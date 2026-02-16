#pragma once

#include "core/SPSCQueue.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace squeeze {

struct MidiEvent {
    uint8_t data[3];
    int size; // 1, 2, or 3 bytes
};

struct MidiRoute {
    int id;
    std::string deviceName;
    int nodeId;
    int channelFilter; // 0 = all channels, 1-16 = specific
    int noteFilter;    // -1 = all notes, 0-127 = specific
};

class MidiRouter {
public:
    MidiRouter();
    ~MidiRouter();

    MidiRouter(const MidiRouter&) = delete;
    MidiRouter& operator=(const MidiRouter&) = delete;

    // --- Device queue management (control thread) ---
    bool createDeviceQueue(const std::string& deviceName, std::string& error);
    void removeDeviceQueue(const std::string& deviceName);
    bool hasDeviceQueue(const std::string& deviceName) const;

    // --- Routing (control thread) ---
    int addRoute(const std::string& deviceName, int nodeId,
                 int channelFilter, int noteFilter, std::string& error);
    bool removeRoute(int routeId);
    bool removeRoutesForNode(int nodeId);
    bool removeRoutesForDevice(const std::string& deviceName);
    std::vector<MidiRoute> getRoutes() const;

    void commit();

    // --- MIDI input (MIDI callback thread) ---
    bool pushMidiEvent(const std::string& deviceName, const MidiEvent& event);

    // --- Audio thread ---
    void dispatch(const std::unordered_map<int, juce::MidiBuffer*>& nodeBuffers,
                  int numSamples);

    // --- Monitoring (any thread, atomic reads) ---
    int getQueueFillLevel(const std::string& deviceName) const;
    int getDroppedCount(const std::string& deviceName) const;
    void resetDroppedCounts();

private:
    struct DeviceState {
        SPSCQueue<MidiEvent, 1024> queue;
        std::atomic<int> droppedCount{0};
    };

    std::unordered_map<std::string, std::unique_ptr<DeviceState>> devices_;

    std::vector<MidiRoute> stagedRoutes_;
    int nextRouteId_ = 1;

    // RT-safe routing table published via atomic pointer
    struct RoutingTable {
        struct DeviceRef {
            SPSCQueue<MidiEvent, 1024>* queue;
        };
        struct RouteEntry {
            int deviceIndex;
            int nodeId;
            int channelFilter;
            int noteFilter;
        };
        std::vector<DeviceRef> deviceRefs;
        std::vector<RouteEntry> entries;
    };

    std::atomic<RoutingTable*> activeTable_{nullptr};
    RoutingTable* pendingGarbage_ = nullptr;

    // Deferred deletion for removed devices (2-commit safety)
    std::vector<std::unique_ptr<DeviceState>> retiredDevices_;
    std::vector<std::unique_ptr<DeviceState>> previousRetiredDevices_;

    static bool matchesFilter(const MidiEvent& event,
                              const RoutingTable::RouteEntry& route);
};

} // namespace squeeze
