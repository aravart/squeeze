#include "core/ClockDispatch.h"

#include <algorithm>
#include <cmath>

namespace squeeze {

// ═══════════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════════

ClockDispatch::ClockDispatch()
{
    dispatchThread_ = std::thread([this] { dispatchLoop(); });
    SQ_INFO("ClockDispatch: created, dispatch thread started");
}

ClockDispatch::~ClockDispatch()
{
    running_.store(false, std::memory_order_release);
    semaphore_.post(); // wake the thread so it can exit
    if (dispatchThread_.joinable())
        dispatchThread_.join();
    SQ_INFO("ClockDispatch: destroyed, dispatch thread stopped");
}

// ═══════════════════════════════════════════════════════════════════
// Subscription management (control thread)
// ═══════════════════════════════════════════════════════════════════

uint32_t ClockDispatch::addClock(double resolution, double latencyMs,
                                  SqClockCallback callback, void* userData)
{
    if (resolution <= 0.0 || latencyMs < 0.0 || !callback)
    {
        SQ_WARN("ClockDispatch::addClock: invalid params res=%.4f lat=%.1f cb=%p",
                resolution, latencyMs, reinterpret_cast<void*>(callback));
        return 0;
    }

    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    uint32_t id = nextId_++;
    subscriptions_.push_back({id, resolution, latencyMs, callback, userData});
    SQ_DEBUG("ClockDispatch::addClock: id=%u res=%.4f lat=%.1f", id, resolution, latencyMs);
    return id;
}

void ClockDispatch::removeClock(uint32_t clockId)
{
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    auto it = std::find_if(subscriptions_.begin(), subscriptions_.end(),
                           [clockId](const ClockSubscription& s) { return s.id == clockId; });
    if (it != subscriptions_.end())
    {
        SQ_DEBUG("ClockDispatch::removeClock: id=%u", clockId);
        subscriptions_.erase(it);
    }
    else
    {
        SQ_DEBUG("ClockDispatch::removeClock: id=%u not found (no-op)", clockId);
    }
}

// ═══════════════════════════════════════════════════════════════════
// Audio thread (RT-safe)
// ═══════════════════════════════════════════════════════════════════

void ClockDispatch::pushBeatRange(const BeatRangeUpdate& update)
{
    if (!queue_.tryPush(update))
    {
        SQ_WARN_RT("ClockDispatch::pushBeatRange: queue overflow, dropping update");
        return;
    }
    semaphore_.post();
}

// ═══════════════════════════════════════════════════════════════════
// Control thread signals
// ═══════════════════════════════════════════════════════════════════

void ClockDispatch::prime(double startBeat, double tempo,
                           bool looping, double loopStart, double loopEnd)
{
    {
        std::lock_guard<std::mutex> lock(primeMutex_);
        primeRequest_ = {startBeat, tempo, looping, loopStart, loopEnd};
    }
    primePending_.store(true, std::memory_order_release);
    semaphore_.post();
    SQ_DEBUG("ClockDispatch::prime: beat=%.3f tempo=%.1f loop=%d [%.3f, %.3f)",
             startBeat, tempo, looping, loopStart, loopEnd);
}

void ClockDispatch::onTransportStop()
{
    stopPending_.store(true, std::memory_order_release);
    semaphore_.post();
    SQ_DEBUG("ClockDispatch::onTransportStop");
}

// ═══════════════════════════════════════════════════════════════════
// Dispatch thread
// ═══════════════════════════════════════════════════════════════════

void ClockDispatch::dispatchLoop()
{
    SQ_TRACE("ClockDispatch: dispatch thread running");

    while (running_.load(std::memory_order_acquire))
    {
        semaphore_.wait();

        if (!running_.load(std::memory_order_acquire))
            break;

        // Check transport stop — clears pending prime
        if (stopPending_.exchange(false, std::memory_order_acq_rel))
        {
            primePending_.store(false, std::memory_order_release);
            SQ_TRACE("ClockDispatch: stop handled, prime cleared");
        }

        // Check prime request
        if (primePending_.exchange(false, std::memory_order_acq_rel))
        {
            handlePrime();
        }

        // Drain beat range queue
        BeatRangeUpdate update;
        while (queue_.tryPop(update))
        {
            processUpdate(update);
        }
    }

    SQ_TRACE("ClockDispatch: dispatch thread exiting");
}

void ClockDispatch::processUpdate(const BeatRangeUpdate& update)
{
    std::lock_guard<std::mutex> lock(subscriptionMutex_);

    for (const auto& sub : subscriptions_)
    {
        double latencyBeats = sub.latencyMs * (update.tempo / 60000.0);
        double windowStart = update.oldBeat + latencyBeats;
        double windowEnd   = update.newBeat + latencyBeats;

        if (update.looping && windowEnd > update.loopEnd)
        {
            if (windowStart < update.loopEnd)
            {
                // Partial wrap: fire [windowStart, loopEnd], then [loopStart, loopStart + overflow)
                fireBoundaries(sub, windowStart, update.loopEnd);
                double overflow = windowEnd - update.loopEnd;
                fireBoundaries(sub, update.loopStart, update.loopStart + overflow);
            }
            else
            {
                // Full wrap: both endpoints past loopEnd
                double loopLen = update.loopEnd - update.loopStart;
                if (loopLen <= 0.0) continue;
                double wrappedStart = update.loopStart + std::fmod(windowStart - update.loopEnd, loopLen);
                double wrappedEnd   = update.loopStart + std::fmod(windowEnd   - update.loopEnd, loopLen);

                if (wrappedStart < wrappedEnd)
                {
                    fireBoundaries(sub, wrappedStart, wrappedEnd);
                }
                else
                {
                    // Window spans loop seam
                    fireBoundaries(sub, wrappedStart, update.loopEnd);
                    fireBoundaries(sub, update.loopStart, wrappedEnd);
                }
            }
        }
        else
        {
            fireBoundaries(sub, windowStart, windowEnd);
        }
    }
}

void ClockDispatch::fireBoundaries(const ClockSubscription& sub,
                                    double windowStart, double windowEnd)
{
    double res = sub.resolution;
    int startSlot = static_cast<int>(std::floor(windowStart / res));
    int endSlot   = static_cast<int>(std::floor(windowEnd / res));

    for (int t = startSlot + 1; t <= endSlot; ++t)
    {
        double beat = t * res;
        try
        {
            sub.callback(sub.id, beat, sub.userData);
        }
        catch (...)
        {
            SQ_WARN("ClockDispatch: clock %u callback threw at beat %.3f — skipping",
                     sub.id, beat);
        }
    }
}

void ClockDispatch::handlePrime()
{
    PrimeRequest req;
    {
        std::lock_guard<std::mutex> lock(primeMutex_);
        req = primeRequest_;
    }

    std::lock_guard<std::mutex> lock(subscriptionMutex_);

    for (const auto& sub : subscriptions_)
    {
        double latencyBeats = sub.latencyMs * (req.tempo / 60000.0);
        double primeStart = req.startBeat;
        double primeEnd   = req.startBeat + latencyBeats;

        if (primeEnd <= primeStart)
            continue; // zero latency — nothing to prime

        if (req.looping && primeEnd > req.loopEnd)
        {
            if (primeStart < req.loopEnd)
            {
                fireBoundaries(sub, primeStart, req.loopEnd);
                double overflow = primeEnd - req.loopEnd;
                fireBoundaries(sub, req.loopStart, req.loopStart + overflow);
            }
            else
            {
                double loopLen = req.loopEnd - req.loopStart;
                if (loopLen <= 0.0) continue;
                double wrappedStart = req.loopStart + std::fmod(primeStart - req.loopEnd, loopLen);
                double wrappedEnd   = req.loopStart + std::fmod(primeEnd   - req.loopEnd, loopLen);
                if (wrappedStart < wrappedEnd)
                    fireBoundaries(sub, wrappedStart, wrappedEnd);
                else
                {
                    fireBoundaries(sub, wrappedStart, req.loopEnd);
                    fireBoundaries(sub, req.loopStart, wrappedEnd);
                }
            }
        }
        else
        {
            fireBoundaries(sub, primeStart, primeEnd);
        }
    }

    SQ_TRACE("ClockDispatch: prime handled for %d subscriptions",
             static_cast<int>(subscriptions_.size()));
}

} // namespace squeeze
