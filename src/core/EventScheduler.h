#pragma once

#include "core/SPSCQueue.h"
#include "core/Logger.h"

#include <array>
#include <cmath>

namespace squeeze {

struct ScheduledEvent {
    double beatTime;        // PPQ timestamp (quarter notes from origin)
    int targetHandle;       // source or processor handle

    enum class Type { noteOn, noteOff, cc, pitchBend, paramChange };
    Type type;

    int channel;            // MIDI channel 1–16 (MIDI events only)
    int data1;              // note number, CC number, pitch bend (0–16383), or param token
    int data2;              // CC value (0–127)
    float floatValue;       // velocity (0.0–1.0) or param value (0.0–1.0)
};

struct ResolvedEvent {
    int sampleOffset;       // sample position within the block [0, numSamples)
    int targetHandle;
    ScheduledEvent::Type type;
    int channel;
    int data1;
    int data2;
    float floatValue;
};

class EventScheduler {
public:
    EventScheduler() = default;
    ~EventScheduler() = default;

    EventScheduler(const EventScheduler&) = delete;
    EventScheduler& operator=(const EventScheduler&) = delete;

    // --- Control thread ---
    bool schedule(const ScheduledEvent& event);

    // --- Audio thread ---
    int retrieve(double blockStartBeats, double blockEndBeats,
                 int numSamples, double tempo, double sampleRate,
                 ResolvedEvent* out, int maxOut);

    void clear();

    // --- Queries (for testing) ---
    int stagingCount() const { return stagingCount_; }

private:
    static constexpr int kQueueCapacity   = 4096;
    static constexpr int kStagingCapacity = 4096;
    static constexpr double kLateToleranceBeats = 1.0;
    static constexpr double kExpiryBeats        = 16.0;

    SPSCQueue<ScheduledEvent, kQueueCapacity> queue_;
    std::array<ScheduledEvent, kStagingCapacity> staging_;
    int stagingCount_ = 0;
};

} // namespace squeeze
