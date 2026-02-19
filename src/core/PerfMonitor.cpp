#include "core/PerfMonitor.h"
#include "core/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace squeeze {

PerfMonitor::PerfMonitor() = default;

// ═══════════════════════════════════════════════════════════════════
// Control thread — enable / disable
// ═══════════════════════════════════════════════════════════════════

void PerfMonitor::enable()
{
    enabled_.store(1, std::memory_order_relaxed);
    SQ_DEBUG("PerfMonitor: enabled");
}

void PerfMonitor::disable()
{
    enabled_.store(0, std::memory_order_relaxed);
    SQ_DEBUG("PerfMonitor: disabled");
}

bool PerfMonitor::isEnabled() const
{
    return enabled_.load(std::memory_order_relaxed) != 0;
}

void PerfMonitor::enableSlotProfiling()
{
    slotProfilingEnabled_.store(1, std::memory_order_relaxed);
    SQ_DEBUG("PerfMonitor: slot profiling enabled");
}

void PerfMonitor::disableSlotProfiling()
{
    slotProfilingEnabled_.store(0, std::memory_order_relaxed);
    SQ_DEBUG("PerfMonitor: slot profiling disabled");
}

bool PerfMonitor::isSlotProfilingEnabled() const
{
    return slotProfilingEnabled_.load(std::memory_order_relaxed) != 0;
}

// ═══════════════════════════════════════════════════════════════════
// Control thread — prepare
// ═══════════════════════════════════════════════════════════════════

void PerfMonitor::prepare(double sampleRate, int blockSize)
{
    budgetUs_ = (sampleRate > 0.0)
        ? (static_cast<double>(blockSize) / sampleRate * 1e6)
        : 0.0;

    // Window length: ~100ms worth of callbacks, minimum 1
    if (sampleRate > 0.0 && blockSize > 0)
        windowLength_ = std::max(1, static_cast<int>(sampleRate / blockSize / 10.0));
    else
        windowLength_ = 1;

    // Update published system context (set once, read by getSnapshot)
    published_.sampleRate = sampleRate;
    published_.blockSize = blockSize;
    published_.bufferDurationUs = budgetUs_;

    accumulator_.reset();
    prepared_ = true;

    SQ_DEBUG("PerfMonitor: prepared sr=%.0f bs=%d budget=%.0fus window=%d",
             sampleRate, blockSize, budgetUs_, windowLength_);
}

// ═══════════════════════════════════════════════════════════════════
// Control thread — snapshot
// ═══════════════════════════════════════════════════════════════════

PerfSnapshot PerfMonitor::getSnapshot() const
{
    PerfSnapshot snap;

    if (!isEnabled())
        return snap;

    // Seqlock read
    RTPublishedData copy;
    uint32_t s1, s2;
    do {
        s1 = sequence_.load(std::memory_order_acquire);
        if (s1 & 1u)
            continue; // write in progress
        std::memcpy(&copy, &published_, sizeof(RTPublishedData));
        s2 = sequence_.load(std::memory_order_acquire);
    } while (s1 != s2);

    snap.callbackAvgUs = copy.callbackAvgUs;
    snap.callbackPeakUs = copy.callbackPeakUs;
    snap.cpuLoadPercent = copy.cpuLoadPercent;
    snap.sampleRate = copy.sampleRate;
    snap.blockSize = copy.blockSize;
    snap.bufferDurationUs = copy.bufferDurationUs;

    // Cumulative counters (independent atomics, always current)
    snap.xrunCount = xrunCount_.load(std::memory_order_relaxed);
    snap.callbackCount = callbackCount_.load(std::memory_order_relaxed);

    // Slot data
    for (int i = 0; i < copy.slotCount; ++i)
    {
        snap.slots.push_back({copy.slots[i].handle,
                              copy.slots[i].avgUs,
                              copy.slots[i].peakUs});
    }

    return snap;
}

// ═══════════════════════════════════════════════════════════════════
// Control thread — reset / threshold
// ═══════════════════════════════════════════════════════════════════

void PerfMonitor::resetCounters()
{
    xrunCount_.store(0, std::memory_order_relaxed);
    callbackCount_.store(0, std::memory_order_relaxed);
    SQ_DEBUG("PerfMonitor: counters reset");
}

void PerfMonitor::setXrunThreshold(double factor)
{
    float clamped = static_cast<float>(std::clamp(factor, 0.1, 2.0));
    xrunThreshold_.store(clamped, std::memory_order_relaxed);
    SQ_DEBUG("PerfMonitor: xrun threshold set to %.2f", clamped);
}

double PerfMonitor::getXrunThreshold() const
{
    return static_cast<double>(xrunThreshold_.load(std::memory_order_relaxed));
}

// ═══════════════════════════════════════════════════════════════════
// Audio thread — beginBlock / endBlock
// ═══════════════════════════════════════════════════════════════════

void PerfMonitor::beginBlock()
{
    if (!enabled_.load(std::memory_order_relaxed))
        return;

    blockStartTime_ = Clock::now();
}

void PerfMonitor::endBlock()
{
    if (!enabled_.load(std::memory_order_relaxed))
        return;

    auto endTime = Clock::now();
    double durationUs = std::chrono::duration<double, std::micro>(
        endTime - blockStartTime_).count();

    // Accumulate
    accumulator_.callbackSumUs += durationUs;
    if (durationUs > accumulator_.callbackPeakUs)
        accumulator_.callbackPeakUs = durationUs;
    accumulator_.windowCount++;

    // Cumulative callback count
    callbackCount_.fetch_add(1, std::memory_order_relaxed);

    // Xrun detection
    if (prepared_ && budgetUs_ > 0.0)
    {
        float threshold = xrunThreshold_.load(std::memory_order_relaxed);
        double limit = budgetUs_ * static_cast<double>(threshold);
        if (durationUs > limit)
        {
            int count = xrunCount_.fetch_add(1, std::memory_order_relaxed) + 1;
            SQ_WARN_RT("xrun: %.0fus (budget %.0fus, threshold %.0f%%), total %d",
                        durationUs, budgetUs_, static_cast<double>(threshold) * 100.0, count);
        }
    }

    // Publish if window elapsed
    if (accumulator_.windowCount >= windowLength_)
        publish();
}

// ═══════════════════════════════════════════════════════════════════
// Audio thread — beginSlot / endSlot
// ═══════════════════════════════════════════════════════════════════

void PerfMonitor::beginSlot(int slotIndex, int handle)
{
    if (!enabled_.load(std::memory_order_relaxed))
        return;
    if (!slotProfilingEnabled_.load(std::memory_order_relaxed))
        return;
    if (slotIndex < 0 || slotIndex >= kMaxSlots)
        return;

    slotStartTimes_[slotIndex] = Clock::now();

    // Track slot count and handle
    if (slotIndex >= accumulator_.slotCount)
        accumulator_.slotCount = slotIndex + 1;
    accumulator_.slots[slotIndex].handle = handle;
}

void PerfMonitor::endSlot(int slotIndex)
{
    if (!enabled_.load(std::memory_order_relaxed))
        return;
    if (!slotProfilingEnabled_.load(std::memory_order_relaxed))
        return;
    if (slotIndex < 0 || slotIndex >= kMaxSlots)
        return;

    auto endTime = Clock::now();
    double durationUs = std::chrono::duration<double, std::micro>(
        endTime - slotStartTimes_[slotIndex]).count();

    accumulator_.slots[slotIndex].sumUs += durationUs;
    if (durationUs > accumulator_.slots[slotIndex].peakUs)
        accumulator_.slots[slotIndex].peakUs = durationUs;
}

// ═══════════════════════════════════════════════════════════════════
// Audio thread — publish (behind seqlock)
// ═══════════════════════════════════════════════════════════════════

void PerfMonitor::publish()
{
    int wc = accumulator_.windowCount;
    if (wc <= 0) return;

    double avgUs = accumulator_.callbackSumUs / wc;

    // Begin seqlock write
    uint32_t seq = sequence_.load(std::memory_order_relaxed);
    sequence_.store(seq + 1, std::memory_order_release); // odd = writing

    published_.callbackAvgUs = avgUs;
    published_.callbackPeakUs = accumulator_.callbackPeakUs;
    published_.cpuLoadPercent = (budgetUs_ > 0.0) ? (avgUs / budgetUs_ * 100.0) : 0.0;

    // Slot data
    if (slotProfilingEnabled_.load(std::memory_order_relaxed))
    {
        published_.slotCount = accumulator_.slotCount;
        for (int i = 0; i < accumulator_.slotCount; ++i)
        {
            published_.slots[i].handle = accumulator_.slots[i].handle;
            published_.slots[i].avgUs = accumulator_.slots[i].sumUs / wc;
            published_.slots[i].peakUs = accumulator_.slots[i].peakUs;
        }
    }
    else
    {
        published_.slotCount = 0;
    }

    // End seqlock write
    sequence_.store(seq + 2, std::memory_order_release); // even = done

    // Reset accumulator for next window
    accumulator_.reset();
}

} // namespace squeeze
