#pragma once

#include "core/SPSCQueue.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace squeeze {

struct MidiEvent {
    uint8_t data[3];
    int size;           // 1, 2, or 3
};

struct MidiRoute {
    int id;
    std::string deviceName;
    int nodeId;
    int channelFilter;  // 0 = all, 1-16 = specific channel
};

class MidiRouter : public juce::MidiInputCallback {
public:
    MidiRouter();
    ~MidiRouter();

    // --- Control thread (caller must hold Engine::controlMutex_) ---

    // Device management
    std::vector<std::string> getAvailableDevices() const;
    bool openDevice(const std::string& deviceName, std::string& error);
    void closeDevice(const std::string& deviceName);
    bool isDeviceOpen(const std::string& deviceName) const;
    std::vector<std::string> getOpenDevices() const;

    // Routing
    int addRoute(const std::string& deviceName, int nodeId,
                 int channelFilter, std::string& error);
    bool removeRoute(int routeId);
    bool removeRoutesForNode(int nodeId);
    std::vector<MidiRoute> getRoutes() const;

    // Publish updated routing table to audio thread
    void commit();

    // --- Audio thread ---

    // Drains all device queues and writes into destination MidiBuffers.
    // nodeBuffers: map from nodeId to the MidiBuffer* that node will read.
    void dispatchMidi(const std::unordered_map<int, juce::MidiBuffer*>& nodeBuffers);

    // --- MIDI thread (juce::MidiInputCallback) ---
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

    // --- Monitoring (RT-safe reads) ---
    struct DeviceStats {
        std::string deviceName;
        int fillLevel;
        int droppedCount;
    };
    std::vector<DeviceStats> getDeviceStats() const;

private:
    struct OpenDevice {
        std::string name;
        std::string identifier;
        std::unique_ptr<juce::MidiInput> device;
        SPSCQueue<MidiEvent, 1024> queue;
        std::atomic<int> droppedCount{0};
    };

    // Control-thread state
    std::vector<std::unique_ptr<OpenDevice>> devices_;
    std::vector<MidiRoute> routes_;
    int nextRouteId_ = 0;

    // Audio-thread routing table (atomic pointer swap)
    struct RoutingTable {
        struct Entry {
            int deviceIndex;    // index into the devices_ vector at commit time
            int nodeId;
            int channelFilter;
        };
        std::vector<Entry> entries;
        // Snapshot of device pointers at commit time (for queue access)
        struct DeviceRef {
            SPSCQueue<MidiEvent, 1024>* queue;
            std::atomic<int>* droppedCount;
            std::string name;
        };
        std::vector<DeviceRef> deviceRefs;
    };

    std::atomic<RoutingTable*> activeTable_{nullptr};
    std::vector<std::unique_ptr<RoutingTable>> pendingTableDeletions_;

    // Scratch buffer for per-device drain (audio thread)
    juce::MidiBuffer deviceDrainBuffer_;

    OpenDevice* findDevice(const std::string& name);
    const OpenDevice* findDevice(const std::string& name) const;
};

} // namespace squeeze
