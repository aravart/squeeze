#include "EventQueue.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>

namespace squeeze {

EventQueue::EventQueue() = default;

bool EventQueue::schedule(const ScheduledEvent& event)
{
    return queue_.tryPush(event);
}

int EventQueue::retrieve(double blockStartBeats, double blockEndBeats,
                         bool looped, double loopStartBeats, double loopEndBeats,
                         int numSamples, double tempo, double sampleRate,
                         ResolvedEvent* out, int maxOut)
{
    // 1. Drain SPSC queue into staging buffer
    ScheduledEvent temp;
    while (queue_.tryPop(temp)) {
        if (std::isnan(temp.beatTime) || temp.beatTime < 0.0)
            continue;

        if (stagingCount_ < kStagingCapacity) {
            staging_[stagingCount_++] = temp;
        } else {
            SQ_LOG_RT_WARN("EventQueue staging full, dropping event at beat %.3f",
                           temp.beatTime);
        }
    }

    const double samplesPerBeat = sampleRate * 60.0 / tempo;
    const double loopLength = loopEndBeats - loopStartBeats;
    const bool wrappedBlock = looped && (blockEndBeats < blockStartBeats);

    // Block duration in beats (for looped late tolerance)
    const double blockDurationBeats = wrappedBlock
        ? (loopEndBeats - blockStartBeats) + (blockEndBeats - loopStartBeats)
        : (blockEndBeats - blockStartBeats);

    int outCount = 0;

    // 2-4. Match, resolve, output (reverse scan for swap-remove)
    for (int i = stagingCount_ - 1; i >= 0; --i) {
        const double beatTime = staging_[i].beatTime;

        if (std::isnan(beatTime) || beatTime < 0.0) {
            staging_[i] = staging_[--stagingCount_];
            continue;
        }

        // Compute forward distance for expiry
        double ahead;
        if (!looped) {
            ahead = beatTime - blockStartBeats;
        } else {
            if (beatTime >= blockStartBeats)
                ahead = beatTime - blockStartBeats;
            else
                ahead = (loopEndBeats - blockStartBeats) + (beatTime - loopStartBeats);
        }

        // Expiry
        if (!looped) {
            // Non-looping: expire if event is far behind
            if (ahead < -kExpiryBeats) {
                SQ_LOG_RT_WARN("EventQueue expiring stale event at beat %.3f", beatTime);
                staging_[i] = staging_[--stagingCount_];
                continue;
            }
        } else {
            // Looping: expire if forward distance exceeds loop length
            if (ahead > loopLength) {
                SQ_LOG_RT_WARN("EventQueue expiring stale event at beat %.3f", beatTime);
                staging_[i] = staging_[--stagingCount_];
                continue;
            }
        }

        // Match to block
        int sampleOffset = -1;

        if (!wrappedBlock) {
            if (beatTime >= blockStartBeats && beatTime < blockEndBeats) {
                sampleOffset = static_cast<int>(std::round(
                    (beatTime - blockStartBeats) * samplesPerBeat));
            }
        } else {
            int wrapSample = static_cast<int>(std::round(
                (loopEndBeats - blockStartBeats) * samplesPerBeat));

            if (beatTime >= blockStartBeats && beatTime < loopEndBeats) {
                sampleOffset = static_cast<int>(std::round(
                    (beatTime - blockStartBeats) * samplesPerBeat));
            } else if (beatTime >= loopStartBeats && beatTime < blockEndBeats) {
                sampleOffset = wrapSample + static_cast<int>(std::round(
                    (beatTime - loopStartBeats) * samplesPerBeat));
            }
        }

        // Late event check (not in block range)
        if (sampleOffset < 0) {
            double backward = blockStartBeats - beatTime;

            if (!looped) {
                // Non-looping: generous tolerance (events won't come around again)
                if (backward > 0.0 && backward <= kLateToleranceBeats) {
                    SQ_LOG_RT_WARN("EventQueue late event at beat %.3f (%.3f beats behind)",
                                   beatTime, backward);
                    sampleOffset = 0;
                }
            } else {
                // Looping: tight tolerance (events will come around again)
                // Only catch events missed by less than one block duration
                if (backward > 0.0 && backward <= blockDurationBeats) {
                    SQ_LOG_RT_WARN("EventQueue late event at beat %.3f (%.3f beats behind)",
                                   beatTime, backward);
                    sampleOffset = 0;
                }
            }
        }

        if (sampleOffset < 0)
            continue; // Not matched, stays in staging

        // Clamp to valid range
        if (sampleOffset >= numSamples)
            sampleOffset = numSamples - 1;

        if (outCount < maxOut) {
            out[outCount++] = {sampleOffset, staging_[i].targetNodeId, staging_[i].type,
                               staging_[i].channel, staging_[i].data1, staging_[i].data2,
                               staging_[i].floatValue};
        }

        // Remove from staging (swap with last)
        staging_[i] = staging_[--stagingCount_];
    }

    // Sort output by sampleOffset (insertion sort — small N expected)
    for (int i = 1; i < outCount; ++i) {
        ResolvedEvent key = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].sampleOffset > key.sampleOffset) {
            out[j + 1] = out[j];
            --j;
        }
        out[j + 1] = key;
    }

    return outCount;
}

void EventQueue::clear()
{
    ScheduledEvent temp;
    while (queue_.tryPop(temp)) {}
    stagingCount_ = 0;
}

} // namespace squeeze
