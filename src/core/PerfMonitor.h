#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <vector>

namespace squeeze {

// ═══════════════════════════════════════════════════════════════════
// PerfSnapshot — control-thread-readable performance data
// ═══════════════════════════════════════════════════════════════════

struct PerfSnapshot {
    double callbackAvgUs = 0.0;
    double callbackPeakUs = 0.0;
    double cpuLoadPercent = 0.0;

    int xrunCount = 0;
    int64_t callbackCount = 0;

    struct SlotPerf {
        int handle = -1;
        double avgUs = 0.0;
        double peakUs = 0.0;
    };
    std::vector<SlotPerf> slots;

    double sampleRate = 0.0;
    int blockSize = 0;
    double bufferDurationUs = 0.0;
};

// ═══════════════════════════════════════════════════════════════════
// PerfMonitor — RT-safe audio thread instrumentation
// ═══════════════════════════════════════════════════════════════════

class PerfMonitor {
public:
    static constexpr int kMaxSlots = 256;

    PerfMonitor();

    // --- Control thread ---
    void enable();
    void disable();
    bool isEnabled() const;

    void enableSlotProfiling();
    void disableSlotProfiling();
    bool isSlotProfilingEnabled() const;

    void prepare(double sampleRate, int blockSize);

    PerfSnapshot getSnapshot() const;
    void resetCounters();

    void setXrunThreshold(double factor);
    double getXrunThreshold() const;

    // --- Audio thread (RT-safe) ---
    void beginBlock();
    void endBlock();
    void beginSlot(int slotIndex, int handle);
    void endSlot(int slotIndex);

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    // --- RT accumulator (audio thread only) ---
    struct SlotAcc {
        int handle = -1;
        double sumUs = 0.0;
        double peakUs = 0.0;
    };

    struct RTAccumulator {
        double callbackSumUs = 0.0;
        double callbackPeakUs = 0.0;
        int windowCount = 0;

        std::array<SlotAcc, kMaxSlots> slots{};
        int slotCount = 0;

        void reset()
        {
            callbackSumUs = 0.0;
            callbackPeakUs = 0.0;
            windowCount = 0;
            for (int i = 0; i < slotCount; ++i)
            {
                slots[i].handle = -1;
                slots[i].sumUs = 0.0;
                slots[i].peakUs = 0.0;
            }
            slotCount = 0;
        }
    };

    // --- Published data (behind seqlock) ---
    struct SlotData {
        int handle = -1;
        double avgUs = 0.0;
        double peakUs = 0.0;
    };

    struct RTPublishedData {
        double callbackAvgUs = 0.0;
        double callbackPeakUs = 0.0;
        double cpuLoadPercent = 0.0;

        std::array<SlotData, kMaxSlots> slots{};
        int slotCount = 0;

        double sampleRate = 0.0;
        int blockSize = 0;
        double bufferDurationUs = 0.0;
    };

    // --- Atomic flags (any thread) ---
    std::atomic<int> enabled_{0};
    std::atomic<int> slotProfilingEnabled_{0};
    std::atomic<float> xrunThreshold_{1.0f};

    // --- Cumulative counters (audio writes, control reads) ---
    std::atomic<int> xrunCount_{0};
    std::atomic<int64_t> callbackCount_{0};

    // --- Seqlock ---
    std::atomic<uint32_t> sequence_{0};
    RTPublishedData published_;

    // --- Audio thread state (not shared) ---
    RTAccumulator accumulator_;
    TimePoint blockStartTime_;
    std::array<TimePoint, kMaxSlots> slotStartTimes_{};
    int windowLength_ = 1; // callbacks per publish window
    double budgetUs_ = 0.0;
    bool prepared_ = false;

    void publish();
};

} // namespace squeeze
