#include "core/PerfMonitor.h"
#include "core/Logger.h"

namespace squeeze {

PerfMonitor::PerfMonitor() = default;

void PerfMonitor::enable()    { enabled_.store(1, std::memory_order_relaxed); }
void PerfMonitor::disable()   { enabled_.store(0, std::memory_order_relaxed); }
bool PerfMonitor::isEnabled() const { return enabled_.load(std::memory_order_relaxed) != 0; }

void PerfMonitor::enableNodeProfiling()    { nodeProfilingEnabled_.store(1, std::memory_order_relaxed); }
void PerfMonitor::disableNodeProfiling()   { nodeProfilingEnabled_.store(0, std::memory_order_relaxed); }
bool PerfMonitor::isNodeProfilingEnabled() const { return nodeProfilingEnabled_.load(std::memory_order_relaxed) != 0; }

void PerfMonitor::prepare(double sampleRate, int blockSize)
{
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;
    bufferDurationUs_ = (blockSize > 0 && sampleRate > 0.0)
        ? (blockSize / sampleRate) * 1e6
        : 0.0;
    publishThreshold_ = (sampleRate > 0.0 && blockSize > 0)
        ? static_cast<int>(sampleRate / blockSize / 10)
        : 1;
    if (publishThreshold_ < 1)
        publishThreshold_ = 1;
}

PerfSnapshot PerfMonitor::getSnapshot() const
{
    PerfSnapshot snap;

    if (!isEnabled())
        return snap;

    // SeqLock read
    RTPublishedData data;
    unsigned s1, s2;
    do {
        s1 = sequence_.load(std::memory_order_acquire);
        if (s1 & 1) continue;  // write in progress, retry
        data = published_;
        s2 = sequence_.load(std::memory_order_acquire);
    } while (s1 != s2);

    snap.callbackAvgUs = data.callbackAvgUs;
    snap.callbackPeakUs = data.callbackPeakUs;
    snap.cpuLoadPercent = data.cpuLoadPercent;

    for (int i = 0; i < data.nodeCount; ++i)
        snap.nodes.push_back({data.nodes[i].nodeId,
                              data.nodes[i].avgUs,
                              data.nodes[i].peakUs});

    // Xrun count is read directly from atomic (always up-to-date)
    snap.xrunCount = xrunCount_.load(std::memory_order_relaxed);

    for (int i = 0; i < data.midiCount; ++i)
    {
        PerfSnapshot::MidiQueuePerf mp;
        mp.nodeId = data.midi[i].nodeId;
        mp.fillLevel = data.midi[i].fillLevel;
        mp.peakFillLevel = data.midi[i].peakFillLevel;
        mp.droppedCount = data.midi[i].droppedCount;
        snap.midi.push_back(mp);
    }

    snap.sampleRate = data.sampleRate;
    snap.blockSize = data.blockSize;
    snap.bufferDurationUs = data.bufferDurationUs;

    return snap;
}

void PerfMonitor::resetCounters()
{
    xrunCount_.store(0, std::memory_order_relaxed);
}

// --- Audio thread ---

void PerfMonitor::beginCallback()
{
    if (!enabled_.load(std::memory_order_relaxed))
        return;

    callbackStart_ = std::chrono::steady_clock::now();
}

void PerfMonitor::endCallback()
{
    if (!enabled_.load(std::memory_order_relaxed))
        return;

    auto now = std::chrono::steady_clock::now();
    double durationUs = std::chrono::duration<double, std::micro>(
        now - callbackStart_).count();

    acc_.callbackSumUs += durationUs;
    if (durationUs > acc_.callbackPeakUs)
        acc_.callbackPeakUs = durationUs;
    acc_.callbackCount++;

    // Xrun detection
    if (bufferDurationUs_ > 0.0 && durationUs > bufferDurationUs_)
    {
        int total = xrunCount_.fetch_add(1, std::memory_order_relaxed) + 1;
        SQ_LOG_RT_WARN("xrun: %.0fus (budget %.0fus), total %d",
                       durationUs, bufferDurationUs_, total);
    }

    // Publish if window elapsed
    if (acc_.callbackCount >= publishThreshold_)
        publish();
}

void PerfMonitor::beginNode(int slotIndex, int nodeId)
{
    if (!nodeProfilingEnabled_.load(std::memory_order_relaxed))
        return;
    if (slotIndex < 0 || slotIndex >= kMaxNodes)
        return;

    nodeStarts_[slotIndex] = std::chrono::steady_clock::now();

    // Expand accumulator tracking if needed
    if (slotIndex >= acc_.nodeCount)
    {
        for (int i = acc_.nodeCount; i <= slotIndex; ++i)
        {
            acc_.nodes[i].nodeId = -1;
            acc_.nodes[i].sumUs = 0.0;
            acc_.nodes[i].peakUs = 0.0;
        }
        acc_.nodeCount = slotIndex + 1;
    }
    acc_.nodes[slotIndex].nodeId = nodeId;
}

void PerfMonitor::endNode(int slotIndex)
{
    if (!nodeProfilingEnabled_.load(std::memory_order_relaxed))
        return;
    if (slotIndex < 0 || slotIndex >= kMaxNodes)
        return;

    auto now = std::chrono::steady_clock::now();
    double durationUs = std::chrono::duration<double, std::micro>(
        now - nodeStarts_[slotIndex]).count();

    acc_.nodes[slotIndex].sumUs += durationUs;
    if (durationUs > acc_.nodes[slotIndex].peakUs)
        acc_.nodes[slotIndex].peakUs = durationUs;
}

void PerfMonitor::reportMidiQueue(int nodeId, int fillLevel, int dropped)
{
    if (!enabled_.load(std::memory_order_relaxed))
        return;

    // Find or allocate a slot
    int idx = -1;
    for (int i = 0; i < acc_.midiCount; ++i)
    {
        if (acc_.midi[i].nodeId == nodeId)
        {
            idx = i;
            break;
        }
    }
    if (idx < 0)
    {
        if (acc_.midiCount >= kMaxMidiNodes)
            return;
        idx = acc_.midiCount++;
        acc_.midi[idx].nodeId = nodeId;
        acc_.midi[idx].peakFillLevel = 0;
    }

    acc_.midi[idx].fillLevel = fillLevel;
    if (fillLevel > acc_.midi[idx].peakFillLevel)
        acc_.midi[idx].peakFillLevel = fillLevel;
    acc_.midi[idx].droppedCount = dropped;
}

void PerfMonitor::publish()
{
    // SeqLock write: increment to odd (write in progress)
    unsigned seq = sequence_.load(std::memory_order_relaxed);
    sequence_.store(seq + 1, std::memory_order_release);

    // Write published data
    auto& data = published_;

    double avg = (acc_.callbackCount > 0)
        ? acc_.callbackSumUs / acc_.callbackCount
        : 0.0;
    data.callbackAvgUs = avg;
    data.callbackPeakUs = acc_.callbackPeakUs;
    data.cpuLoadPercent = (bufferDurationUs_ > 0.0)
        ? (avg / bufferDurationUs_) * 100.0
        : 0.0;

    // Per-node data
    data.nodeCount = acc_.nodeCount;
    for (int i = 0; i < acc_.nodeCount; ++i)
    {
        data.nodes[i].nodeId = acc_.nodes[i].nodeId;
        data.nodes[i].avgUs = (acc_.callbackCount > 0)
            ? acc_.nodes[i].sumUs / acc_.callbackCount
            : 0.0;
        data.nodes[i].peakUs = acc_.nodes[i].peakUs;
    }

    // MIDI data
    data.midiCount = acc_.midiCount;
    for (int i = 0; i < acc_.midiCount; ++i)
    {
        data.midi[i].nodeId = acc_.midi[i].nodeId;
        data.midi[i].fillLevel = acc_.midi[i].fillLevel;
        data.midi[i].peakFillLevel = acc_.midi[i].peakFillLevel;
        data.midi[i].droppedCount = acc_.midi[i].droppedCount;
    }

    data.sampleRate = sampleRate_;
    data.blockSize = blockSize_;
    data.bufferDurationUs = bufferDurationUs_;

    // SeqLock write: increment to even (write complete)
    sequence_.store(seq + 2, std::memory_order_release);

    // Reset accumulator for next window (preserve xrunCount — it's atomic/cumulative)
    acc_.callbackSumUs = 0.0;
    acc_.callbackPeakUs = 0.0;
    acc_.callbackCount = 0;
    acc_.nodeCount = 0;
    acc_.midiCount = 0;
}

} // namespace squeeze
