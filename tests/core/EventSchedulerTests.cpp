#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/EventScheduler.h"

#include <limits>

using namespace squeeze;
using Catch::Matchers::WithinAbs;

static constexpr double kSampleRate = 44100.0;
static constexpr double kTempo      = 120.0;   // BPM

// At 120 BPM, 44100 Hz: samplesPerBeat = 44100 * 60 / 120 = 22050
static constexpr double kSamplesPerBeat = kSampleRate * 60.0 / kTempo;

static ScheduledEvent makeNoteOn(int handle, double beat, int channel = 1,
                                  int note = 60, float velocity = 0.8f)
{
    return {beat, handle, ScheduledEvent::Type::noteOn, channel, note, 0, velocity};
}

static ScheduledEvent makeNoteOff(int handle, double beat, int channel = 1,
                                   int note = 60)
{
    return {beat, handle, ScheduledEvent::Type::noteOff, channel, note, 0, 0.0f};
}

static ScheduledEvent makeCC(int handle, double beat, int channel = 1,
                              int ccNum = 1, int ccVal = 64)
{
    return {beat, handle, ScheduledEvent::Type::cc, channel, ccNum, ccVal, 0.0f};
}

static ScheduledEvent makePitchBend(int handle, double beat, int channel = 1,
                                     int value = 8192)
{
    return {beat, handle, ScheduledEvent::Type::pitchBend, channel, value, 0, 0.0f};
}

static ScheduledEvent makeParamChange(int handle, double beat, int token = 0,
                                       float value = 0.5f)
{
    return {beat, handle, ScheduledEvent::Type::paramChange, 0, token, 0, value};
}


// ═══════════════════════════════════════════════════════════════════
// schedule()
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("EventScheduler schedule succeeds and returns true")
{
    EventScheduler es;
    CHECK(es.schedule(makeNoteOn(1, 0.0)));
}

TEST_CASE("EventScheduler schedule returns false when queue is full")
{
    EventScheduler es;
    int pushed = 0;
    for (int i = 0; i < 5000; ++i)
    {
        if (!es.schedule(makeNoteOn(1, static_cast<double>(i))))
            break;
        ++pushed;
    }
    // Queue capacity is 4096 — should fill up
    CHECK(pushed == 4096);
}


// ═══════════════════════════════════════════════════════════════════
// retrieve() — basic resolution
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("EventScheduler retrieve with no events returns 0")
{
    EventScheduler es;
    ResolvedEvent out[16];
    int count = es.retrieve(0.0, 1.0, 512, kTempo, kSampleRate, out, 16);
    CHECK(count == 0);
}

TEST_CASE("EventScheduler retrieve resolves event at block start to sampleOffset 0")
{
    EventScheduler es;
    es.schedule(makeNoteOn(1, 0.0));

    ResolvedEvent out[16];
    int count = es.retrieve(0.0, 1.0, 512, kTempo, kSampleRate, out, 16);
    REQUIRE(count == 1);
    CHECK(out[0].sampleOffset == 0);
    CHECK(out[0].targetHandle == 1);
    CHECK(out[0].type == ScheduledEvent::Type::noteOn);
    CHECK(out[0].channel == 1);
    CHECK(out[0].data1 == 60);
    CHECK_THAT(out[0].floatValue, WithinAbs(0.8f, 1e-6));
}

TEST_CASE("EventScheduler retrieve resolves correct sampleOffset mid-block")
{
    EventScheduler es;
    // Event at beat 0.5 in block [0.0, 1.0) at 120 BPM, 44100 Hz
    // Expected: 0.5 * 22050 = 11025
    es.schedule(makeNoteOn(1, 0.5));

    ResolvedEvent out[16];
    int count = es.retrieve(0.0, 1.0, 22050, kTempo, kSampleRate, out, 16);
    REQUIRE(count == 1);
    CHECK(out[0].sampleOffset == 11025);
}

TEST_CASE("EventScheduler retrieve clamps sampleOffset to numSamples - 1")
{
    EventScheduler es;
    // Event very close to blockEnd — round() might produce numSamples
    // Block [0.0, 1.0), event at 0.99999, numSamples = 22050
    // offset = round(0.99999 * 22050) = round(22049.78) = 22050 → clamp to 22049
    es.schedule(makeNoteOn(1, 0.99999));

    ResolvedEvent out[16];
    int count = es.retrieve(0.0, 1.0, 22050, kTempo, kSampleRate, out, 16);
    REQUIRE(count == 1);
    CHECK(out[0].sampleOffset <= 22049);
}


// ═══════════════════════════════════════════════════════════════════
// retrieve() — staging persistence
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("EventScheduler future events persist in staging across calls")
{
    EventScheduler es;
    es.schedule(makeNoteOn(1, 10.0));  // far in the future

    ResolvedEvent out[16];
    // Block [0.0, 1.0) — event should not fire
    int count = es.retrieve(0.0, 1.0, 22050, kTempo, kSampleRate, out, 16);
    CHECK(count == 0);
    CHECK(es.stagingCount() == 1);

    // Block [10.0, 11.0) — event should fire
    count = es.retrieve(10.0, 11.0, 22050, kTempo, kSampleRate, out, 16);
    REQUIRE(count == 1);
    CHECK(out[0].sampleOffset == 0);
    CHECK(es.stagingCount() == 0);
}


// ═══════════════════════════════════════════════════════════════════
// retrieve() — expiry
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("EventScheduler retrieve expires events more than 16 beats in the past")
{
    EventScheduler es;
    es.schedule(makeNoteOn(1, 0.0));

    ResolvedEvent out[16];
    // Block starts at beat 17.0 — event at 0.0 is 17 beats behind (> kExpiryBeats)
    int count = es.retrieve(17.0, 18.0, 22050, kTempo, kSampleRate, out, 16);
    CHECK(count == 0);
    CHECK(es.stagingCount() == 0);  // expired and removed
}

TEST_CASE("EventScheduler retrieve does not expire events within 16 beats")
{
    EventScheduler es;
    es.schedule(makeNoteOn(1, 0.0));

    ResolvedEvent out[16];
    // Block starts at beat 15.0 — event at 0.0 is 15 beats behind (< kExpiryBeats)
    // It's also > kLateToleranceBeats, so it won't be rescued — stays in staging
    int count = es.retrieve(15.0, 16.0, 22050, kTempo, kSampleRate, out, 16);
    CHECK(count == 0);
    // Event should still be in staging (not expired, just too late to rescue)
    CHECK(es.stagingCount() == 1);
}

TEST_CASE("EventScheduler retrieve expires event at exactly 16 beats behind (boundary)")
{
    EventScheduler es;
    // Event at beat 0.0, block starts at beat 16.0 — ahead = -16.0
    // kExpiryBeats = 16.0, condition is ahead < -kExpiryBeats (strict <)
    // -16.0 < -16.0 is false → event should NOT be expired
    es.schedule(makeNoteOn(1, 0.0));

    ResolvedEvent out[16];
    int count = es.retrieve(16.0, 17.0, 22050, kTempo, kSampleRate, out, 16);
    CHECK(count == 0);
    // At exactly -16.0, strict < means not expired — kept in staging
    CHECK(es.stagingCount() == 1);
}

TEST_CASE("EventScheduler retrieve expires event just past 16 beats behind")
{
    EventScheduler es;
    // Event at beat 0.0, block starts at beat 16.001 — ahead = -16.001
    // -16.001 < -16.0 is true → event should be expired
    es.schedule(makeNoteOn(1, 0.0));

    ResolvedEvent out[16];
    int count = es.retrieve(16.001, 17.001, 22050, kTempo, kSampleRate, out, 16);
    CHECK(count == 0);
    CHECK(es.stagingCount() == 0);  // expired and removed
}


// ═══════════════════════════════════════════════════════════════════
// retrieve() — late event rescue
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("EventScheduler retrieve rescues late events within 1.0 beat at sampleOffset 0")
{
    EventScheduler es;
    es.schedule(makeNoteOn(1, 0.0));

    ResolvedEvent out[16];
    // Block starts at beat 0.5 — event at 0.0 is 0.5 beats late
    int count = es.retrieve(0.5, 1.5, 22050, kTempo, kSampleRate, out, 16);
    REQUIRE(count == 1);
    CHECK(out[0].sampleOffset == 0);
}

TEST_CASE("EventScheduler retrieve does not rescue events more than 1.0 beat late")
{
    EventScheduler es;
    es.schedule(makeNoteOn(1, 0.0));

    ResolvedEvent out[16];
    // Block starts at beat 1.5 — event at 0.0 is 1.5 beats late (> 1.0)
    int count = es.retrieve(1.5, 2.5, 22050, kTempo, kSampleRate, out, 16);
    CHECK(count == 0);
    CHECK(es.stagingCount() == 1);  // kept in staging, not expired yet
}

TEST_CASE("EventScheduler retrieve rescues event exactly 1.0 beat late")
{
    EventScheduler es;
    es.schedule(makeNoteOn(1, 1.0));

    ResolvedEvent out[16];
    // Block starts at beat 2.0 — event is exactly 1.0 beat late
    int count = es.retrieve(2.0, 3.0, 22050, kTempo, kSampleRate, out, 16);
    REQUIRE(count == 1);
    CHECK(out[0].sampleOffset == 0);
}


// ═══════════════════════════════════════════════════════════════════
// retrieve() — sorting and type priority
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("EventScheduler retrieve sorts output by sampleOffset ascending")
{
    EventScheduler es;
    es.schedule(makeNoteOn(1, 0.8));
    es.schedule(makeNoteOn(2, 0.2));
    es.schedule(makeNoteOn(3, 0.5));

    ResolvedEvent out[16];
    int count = es.retrieve(0.0, 1.0, 22050, kTempo, kSampleRate, out, 16);
    REQUIRE(count == 3);
    CHECK(out[0].sampleOffset <= out[1].sampleOffset);
    CHECK(out[1].sampleOffset <= out[2].sampleOffset);
    // Verify correct handles after sort
    CHECK(out[0].targetHandle == 2);  // beat 0.2
    CHECK(out[1].targetHandle == 3);  // beat 0.5
    CHECK(out[2].targetHandle == 1);  // beat 0.8
}

TEST_CASE("EventScheduler retrieve sorts same-offset events by type priority")
{
    EventScheduler es;
    // All at beat 1.0 — should sort: noteOff, cc, pitchBend, paramChange, noteOn
    es.schedule(makeNoteOn(1, 1.0));
    es.schedule(makeParamChange(2, 1.0));
    es.schedule(makeCC(3, 1.0));
    es.schedule(makeNoteOff(4, 1.0));
    es.schedule(makePitchBend(5, 1.0));

    ResolvedEvent out[16];
    int count = es.retrieve(1.0, 2.0, 22050, kTempo, kSampleRate, out, 16);
    REQUIRE(count == 5);
    CHECK(out[0].type == ScheduledEvent::Type::noteOff);      // priority 0
    CHECK(out[1].type == ScheduledEvent::Type::cc);            // priority 1
    CHECK(out[2].type == ScheduledEvent::Type::pitchBend);     // priority 2
    CHECK(out[3].type == ScheduledEvent::Type::paramChange);   // priority 3
    CHECK(out[4].type == ScheduledEvent::Type::noteOn);        // priority 4
}


// ═══════════════════════════════════════════════════════════════════
// retrieve() — output buffer full
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("EventScheduler retrieve keeps events in staging when output buffer is full")
{
    EventScheduler es;
    es.schedule(makeNoteOn(1, 0.0));
    es.schedule(makeNoteOn(2, 0.5));
    es.schedule(makeNoteOn(3, 0.8));

    // Only room for 1 event
    ResolvedEvent out[1];
    int count = es.retrieve(0.0, 1.0, 22050, kTempo, kSampleRate, out, 1);
    CHECK(count == 1);
    // Remaining events stay in staging
    CHECK(es.stagingCount() == 2);

    // Next call picks up the rest
    ResolvedEvent out2[16];
    count = es.retrieve(0.0, 1.0, 22050, kTempo, kSampleRate, out2, 16);
    CHECK(count == 2);
    CHECK(es.stagingCount() == 0);
}


// ═══════════════════════════════════════════════════════════════════
// retrieve() — invalid events
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("EventScheduler retrieve discards NaN beatTime events")
{
    EventScheduler es;
    ScheduledEvent bad = makeNoteOn(1, std::nan(""));
    es.schedule(bad);

    ResolvedEvent out[16];
    int count = es.retrieve(0.0, 1.0, 512, kTempo, kSampleRate, out, 16);
    CHECK(count == 0);
    CHECK(es.stagingCount() == 0);
}

TEST_CASE("EventScheduler retrieve discards negative beatTime events")
{
    EventScheduler es;
    es.schedule(makeNoteOn(1, -1.0));

    ResolvedEvent out[16];
    int count = es.retrieve(0.0, 1.0, 512, kTempo, kSampleRate, out, 16);
    CHECK(count == 0);
    CHECK(es.stagingCount() == 0);
}

TEST_CASE("EventScheduler retrieve discards infinite beatTime events")
{
    EventScheduler es;
    es.schedule(makeNoteOn(1, std::numeric_limits<double>::infinity()));
    es.schedule(makeNoteOn(2, -std::numeric_limits<double>::infinity()));

    ResolvedEvent out[16];
    int count = es.retrieve(0.0, 1.0, 512, kTempo, kSampleRate, out, 16);
    CHECK(count == 0);
    CHECK(es.stagingCount() == 0);
}


// ═══════════════════════════════════════════════════════════════════
// retrieve() — all event types
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("EventScheduler retrieve resolves all event types correctly")
{
    EventScheduler es;
    es.schedule(makeNoteOn(1, 0.0, 1, 60, 0.9f));
    es.schedule(makeNoteOff(1, 0.1, 1, 60));
    es.schedule(makeCC(1, 0.2, 1, 7, 100));
    es.schedule(makePitchBend(1, 0.3, 1, 12000));
    es.schedule(makeParamChange(2, 0.4, 42, 0.75f));

    ResolvedEvent out[16];
    int count = es.retrieve(0.0, 1.0, 22050, kTempo, kSampleRate, out, 16);
    REQUIRE(count == 5);

    // Find each type in output (sorted by offset, so order should be by beat)
    // beat 0.0: noteOn
    CHECK(out[0].type == ScheduledEvent::Type::noteOn);
    CHECK(out[0].data1 == 60);
    CHECK_THAT(out[0].floatValue, WithinAbs(0.9f, 1e-6));

    // beat 0.1: noteOff
    CHECK(out[1].type == ScheduledEvent::Type::noteOff);
    CHECK(out[1].data1 == 60);

    // beat 0.2: cc
    CHECK(out[2].type == ScheduledEvent::Type::cc);
    CHECK(out[2].data1 == 7);
    CHECK(out[2].data2 == 100);

    // beat 0.3: pitchBend
    CHECK(out[3].type == ScheduledEvent::Type::pitchBend);
    CHECK(out[3].data1 == 12000);

    // beat 0.4: paramChange
    CHECK(out[4].type == ScheduledEvent::Type::paramChange);
    CHECK(out[4].data1 == 42);
    CHECK_THAT(out[4].floatValue, WithinAbs(0.75f, 1e-6));
}


// ═══════════════════════════════════════════════════════════════════
// clear()
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("EventScheduler clear removes all staged and queued events")
{
    EventScheduler es;
    // Put some events into staging via a retrieve that doesn't match
    es.schedule(makeNoteOn(1, 10.0));
    es.schedule(makeNoteOn(2, 20.0));

    ResolvedEvent out[16];
    es.retrieve(0.0, 1.0, 512, kTempo, kSampleRate, out, 16);
    CHECK(es.stagingCount() == 2);

    // Schedule more that are still in the SPSC queue
    es.schedule(makeNoteOn(3, 30.0));

    es.clear();
    CHECK(es.stagingCount() == 0);

    // Verify nothing comes out
    int count = es.retrieve(0.0, 100.0, 512, kTempo, kSampleRate, out, 16);
    CHECK(count == 0);
}


// ═══════════════════════════════════════════════════════════════════
// Edge cases
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("EventScheduler retrieve handles event exactly at blockEndBeats (exclusive)")
{
    EventScheduler es;
    // blockEnd is exclusive — event at exactly blockEnd should not fire
    es.schedule(makeNoteOn(1, 1.0));

    ResolvedEvent out[16];
    int count = es.retrieve(0.0, 1.0, 22050, kTempo, kSampleRate, out, 16);
    CHECK(count == 0);
    CHECK(es.stagingCount() == 1);

    // Should fire in next block
    count = es.retrieve(1.0, 2.0, 22050, kTempo, kSampleRate, out, 16);
    REQUIRE(count == 1);
    CHECK(out[0].sampleOffset == 0);
}

TEST_CASE("EventScheduler multiple retrieve calls drain events correctly")
{
    EventScheduler es;
    for (int i = 0; i < 100; ++i)
        es.schedule(makeNoteOn(1, static_cast<double>(i) * 0.01));

    ResolvedEvent out[256];
    int count = es.retrieve(0.0, 1.0, 22050, kTempo, kSampleRate, out, 256);
    CHECK(count == 100);
    CHECK(es.stagingCount() == 0);

    // Verify sorted
    for (int i = 1; i < count; ++i)
        CHECK(out[i].sampleOffset >= out[i - 1].sampleOffset);
}
