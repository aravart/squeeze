#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>

namespace squeeze {

static constexpr int kMaxNodes = 256;
static constexpr int kMaxMidiNodes = 32;

struct PerfSnapshot {
    double callbackAvgUs = 0.0;
    double callbackPeakUs = 0.0;
    double cpuLoadPercent = 0.0;

    struct NodePerf {
        int nodeId = -1;
        double avgUs = 0.0;
        double peakUs = 0.0;
    };
    std::vector<NodePerf> nodes;

    int xrunCount = 0;

    struct MidiQueuePerf {
        int nodeId = -1;
        std::string deviceName;
        int fillLevel = 0;
        int peakFillLevel = 0;
        int droppedCount = 0;
    };
    std::vector<MidiQueuePerf> midi;

    double sampleRate = 0.0;
    int blockSize = 0;
    double bufferDurationUs = 0.0;
};

class PerfMonitor {
public:
    PerfMonitor();

    // --- Control thread ---

    void enable();
    void disable();
    bool isEnabled() const;

    void enableNodeProfiling();
    void disableNodeProfiling();
    bool isNodeProfilingEnabled() const;

    void prepare(double sampleRate, int blockSize);

    PerfSnapshot getSnapshot() const;

    void resetCounters();

    // --- Audio thread (all methods are RT-safe) ---

    void beginCallback();
    void endCallback();

    void beginNode(int slotIndex, int nodeId);
    void endNode(int slotIndex);

    void reportMidiQueue(int nodeId, int fillLevel, int dropped);

private:
    void publish();

    // RT accumulator (audio thread only)
    struct NodeAcc {
        int nodeId = -1;
        double sumUs = 0.0;
        double peakUs = 0.0;
    };

    struct MidiAcc {
        int nodeId = -1;
        int fillLevel = 0;
        int peakFillLevel = 0;
        int droppedCount = 0;
    };

    struct RTAccumulator {
        double callbackSumUs = 0.0;
        double callbackPeakUs = 0.0;
        int callbackCount = 0;

        std::array<NodeAcc, kMaxNodes> nodes{};
        int nodeCount = 0;

        std::array<MidiAcc, kMaxMidiNodes> midi{};
        int midiCount = 0;
    };

    // Published data (seqlock-protected, read by control thread)
    struct RTPublishedData {
        double callbackAvgUs = 0.0;
        double callbackPeakUs = 0.0;
        double cpuLoadPercent = 0.0;

        struct NodePerf {
            int nodeId = -1;
            double avgUs = 0.0;
            double peakUs = 0.0;
        };
        std::array<NodePerf, kMaxNodes> nodes{};
        int nodeCount = 0;

        struct MidiPerf {
            int nodeId = -1;
            int fillLevel = 0;
            int peakFillLevel = 0;
            int droppedCount = 0;
        };
        std::array<MidiPerf, kMaxMidiNodes> midi{};
        int midiCount = 0;

        double sampleRate = 0.0;
        int blockSize = 0;
        double bufferDurationUs = 0.0;
    };

    std::atomic<int> enabled_{0};
    std::atomic<int> nodeProfilingEnabled_{0};

    // Audio-thread state
    RTAccumulator acc_{};
    std::chrono::steady_clock::time_point callbackStart_{};
    std::array<std::chrono::steady_clock::time_point, kMaxNodes> nodeStarts_{};
    int publishThreshold_ = 0;
    double bufferDurationUs_ = 0.0;
    double sampleRate_ = 0.0;
    int blockSize_ = 0;

    // Cumulative xrun count (atomic for cross-thread access)
    std::atomic<int> xrunCount_{0};

    // SeqLock: single published data buffer
    mutable std::atomic<unsigned> sequence_{0};
    RTPublishedData published_{};
};

} // namespace squeeze
