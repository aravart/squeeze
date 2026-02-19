#include "core/EventScheduler.h"

#include <algorithm>
#include <cmath>

namespace squeeze {

// ═══════════════════════════════════════════════════════════════════
// Control thread
// ═══════════════════════════════════════════════════════════════════

bool EventScheduler::schedule(const ScheduledEvent& event)
{
    if (!queue_.tryPush(event))
    {
        SQ_WARN("EventScheduler::schedule: queue full, dropping event "
                "(type=%d, beat=%.3f, target=%d)",
                static_cast<int>(event.type), event.beatTime, event.targetHandle);
        return false;
    }
    SQ_TRACE("EventScheduler::schedule: queued event type=%d beat=%.3f target=%d",
             static_cast<int>(event.type), event.beatTime, event.targetHandle);
    return true;
}

// ═══════════════════════════════════════════════════════════════════
// Audio thread
// ═══════════════════════════════════════════════════════════════════

static int typePriority(ScheduledEvent::Type t)
{
    switch (t) {
        case ScheduledEvent::Type::noteOff:     return 0;
        case ScheduledEvent::Type::cc:          return 1;
        case ScheduledEvent::Type::pitchBend:   return 2;
        case ScheduledEvent::Type::paramChange:  return 3;
        case ScheduledEvent::Type::noteOn:      return 4;
    }
    return 5;
}

int EventScheduler::retrieve(double blockStartBeats, double blockEndBeats,
                              int numSamples, double tempo, double sampleRate,
                              ResolvedEvent* out, int maxOut)
{
    // Phase 1: Drain SPSC queue into staging
    ScheduledEvent incoming;
    while (queue_.tryPop(incoming))
    {
        if (std::isnan(incoming.beatTime) || incoming.beatTime < 0.0)
        {
            SQ_WARN_RT("EventScheduler::retrieve: discarding event with invalid beatTime=%.3f",
                        incoming.beatTime);
            continue;
        }
        if (stagingCount_ >= kStagingCapacity)
        {
            SQ_WARN_RT("EventScheduler::retrieve: staging full, dropping event "
                        "(type=%d, beat=%.3f)", static_cast<int>(incoming.type), incoming.beatTime);
            continue;
        }
        staging_[stagingCount_++] = incoming;
    }

    // Phase 2: Scan staging, match to block
    double samplesPerBeat = sampleRate * 60.0 / tempo;
    int outCount = 0;

    for (int i = stagingCount_ - 1; i >= 0; --i)
    {
        auto& ev = staging_[i];
        double ahead = ev.beatTime - blockStartBeats;

        // Expiry: remove events far in the past
        if (ahead < -kExpiryBeats)
        {
            SQ_WARN_RT("EventScheduler::retrieve: expiring stale event "
                        "(type=%d, beat=%.3f, blockStart=%.3f, behind=%.3f beats)",
                        static_cast<int>(ev.type), ev.beatTime, blockStartBeats, -ahead);
            staging_[i] = staging_[--stagingCount_];
            continue;
        }

        bool matched = false;
        int sampleOffset = 0;

        // Match to block: beatTime in [blockStartBeats, blockEndBeats)
        if (ev.beatTime >= blockStartBeats && ev.beatTime < blockEndBeats)
        {
            matched = true;
            sampleOffset = static_cast<int>(
                std::round((ev.beatTime - blockStartBeats) * samplesPerBeat));
            sampleOffset = std::clamp(sampleOffset, 0, numSamples - 1);
        }
        // Late event rescue: slightly in the past
        else if (ahead < 0.0 && -ahead <= kLateToleranceBeats)
        {
            matched = true;
            sampleOffset = 0;
            SQ_WARN_RT("EventScheduler::retrieve: late event rescued "
                        "(beat=%.3f, blockStart=%.3f, late by %.3f beats)",
                        ev.beatTime, blockStartBeats, -ahead);
        }

        if (!matched)
            continue;

        // Emit or postpone
        if (outCount < maxOut)
        {
            out[outCount].sampleOffset = sampleOffset;
            out[outCount].targetHandle = ev.targetHandle;
            out[outCount].type         = ev.type;
            out[outCount].channel      = ev.channel;
            out[outCount].data1        = ev.data1;
            out[outCount].data2        = ev.data2;
            out[outCount].floatValue   = ev.floatValue;
            ++outCount;
            // Remove from staging (swap with last)
            staging_[i] = staging_[--stagingCount_];
        }
        else
        {
            SQ_WARN_RT("EventScheduler::retrieve: output buffer full, "
                        "postponing event to next block (beat=%.3f)", ev.beatTime);
        }
    }

    // Phase 3: Insertion sort output by sampleOffset, tie-break by type priority
    for (int i = 1; i < outCount; ++i)
    {
        ResolvedEvent key = out[i];
        int keyPrio = typePriority(key.type);
        int j = i - 1;
        while (j >= 0 && (out[j].sampleOffset > key.sampleOffset ||
               (out[j].sampleOffset == key.sampleOffset && typePriority(out[j].type) > keyPrio)))
        {
            out[j + 1] = out[j];
            --j;
        }
        out[j + 1] = key;
    }

    return outCount;
}

void EventScheduler::clear()
{
    // Drain SPSC queue (discard all)
    ScheduledEvent discard;
    while (queue_.tryPop(discard)) {}

    stagingCount_ = 0;
    SQ_TRACE_RT("EventScheduler::clear: all events discarded");
}

} // namespace squeeze
