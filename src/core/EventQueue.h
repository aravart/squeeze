#pragma once

#include "SPSCQueue.h"
#include <array>
#include <cmath>

namespace squeeze {

struct ScheduledEvent {
    double beatTime;
    int targetNodeId;

    enum class Type { noteOn, noteOff, cc, paramChange };
    Type type;

    int channel;

    // Interpretation depends on type:
    //   noteOn:      data1 = note (0-127), floatValue = velocity (0-127 as float)
    //   noteOff:     data1 = note (0-127)
    //   cc:          data1 = CC number (0-127), data2 = value (0-127)
    //   paramChange: data1 = paramIndex, floatValue = normalized value (0.0-1.0)
    int data1;
    int data2;
    float floatValue;
};

struct ResolvedEvent {
    int sampleOffset;
    int targetNodeId;
    ScheduledEvent::Type type;
    int channel;
    int data1;
    int data2;
    float floatValue;
};

class EventQueue {
public:
    EventQueue();

    // Control thread: push event into SPSC queue
    bool schedule(const ScheduledEvent& event);

    // Audio thread: drain queue, match events to block, resolve sample offsets
    int retrieve(double blockStartBeats, double blockEndBeats,
                 bool looped, double loopStartBeats, double loopEndBeats,
                 int numSamples, double tempo, double sampleRate,
                 ResolvedEvent* out, int maxOut);

    // Audio thread: discard all events
    void clear();

private:
    static constexpr int kQueueCapacity = 4096;
    static constexpr int kStagingCapacity = 4096;
    static constexpr double kLateToleranceBeats = 4.0;
    static constexpr double kExpiryBeats = 16.0;

    SPSCQueue<ScheduledEvent, kQueueCapacity> queue_;
    std::array<ScheduledEvent, kStagingCapacity> staging_;
    int stagingCount_ = 0;
};

} // namespace squeeze
