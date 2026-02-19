#pragma once

#include "core/Logger.h"
#include "core/Semaphore.h"
#include "core/SPSCQueue.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace squeeze {

using SqClockCallback = void(*)(uint32_t clockId, double beat, void* userData);

struct BeatRangeUpdate {
    double oldBeat;
    double newBeat;
    double tempo;
    bool   looping;
    double loopStart;
    double loopEnd;
};

struct ClockSubscription {
    uint32_t id;
    double   resolution;
    double   latencyMs;
    SqClockCallback callback;
    void*    userData;
};

class ClockDispatch {
public:
    ClockDispatch();
    ~ClockDispatch();

    ClockDispatch(const ClockDispatch&) = delete;
    ClockDispatch& operator=(const ClockDispatch&) = delete;

    // --- Subscription management (control thread, internally synchronized) ---
    uint32_t addClock(double resolution, double latencyMs,
                      SqClockCallback callback, void* userData);
    void removeClock(uint32_t clockId);

    // --- Called by Engine (audio thread, RT-safe) ---
    void pushBeatRange(const BeatRangeUpdate& update);

    // --- Called by Engine on transport start/seek (control thread) ---
    void prime(double startBeat, double tempo,
               bool looping, double loopStart, double loopEnd);

    // --- Called by Engine on transport stop (control thread) ---
    void onTransportStop();

private:
    static constexpr int kQueueCapacity = 256;

    SPSCQueue<BeatRangeUpdate, kQueueCapacity> queue_;
    Semaphore semaphore_;

    mutable std::mutex subscriptionMutex_;
    std::vector<ClockSubscription> subscriptions_;
    uint32_t nextId_ = 1;

    // Prime request
    struct PrimeRequest {
        double startBeat;
        double tempo;
        bool   looping;
        double loopStart;
        double loopEnd;
    };
    std::mutex primeMutex_;
    PrimeRequest primeRequest_{};
    std::atomic<bool> primePending_{false};
    std::atomic<bool> stopPending_{false};

    std::thread dispatchThread_;
    std::atomic<bool> running_{true};

    void dispatchLoop();
    void processUpdate(const BeatRangeUpdate& update);
    void fireBoundaries(const ClockSubscription& sub,
                        double windowStart, double windowEnd);
    void handlePrime();
};

} // namespace squeeze
