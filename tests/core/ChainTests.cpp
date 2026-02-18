#include <catch2/catch_test_macros.hpp>
#include "core/Processor.h"
#include "core/Chain.h"

using namespace squeeze;

// --- Local test helper: TrackingProcessor ---
// Records lifecycle calls and has configurable latency.

class TrackingProcessor : public Processor {
public:
    explicit TrackingProcessor(const std::string& name, int latency = 0)
        : Processor(name), latency_(latency) {}

    void prepare(double sampleRate, int blockSize) override
    {
        preparedSampleRate_ = sampleRate;
        preparedBlockSize_ = blockSize;
        prepareCount_++;
    }

    void release() override { releaseCount_++; }
    void reset() override { resetCount_++; }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        // Apply a recognizable transformation: add 1.0 to every sample
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample(ch, i, buffer.getSample(ch, i) + 1.0f);
    }

    int getLatencySamples() const override { return latency_; }

    int prepareCount_ = 0;
    int releaseCount_ = 0;
    int resetCount_ = 0;
    double preparedSampleRate_ = 0.0;
    int preparedBlockSize_ = 0;

private:
    int latency_;
};

// Helper to create a TrackingProcessor unique_ptr
static std::unique_ptr<TrackingProcessor> makeTracker(const std::string& name, int latency = 0)
{
    return std::make_unique<TrackingProcessor>(name, latency);
}

// ═══════════════════════════════════════════════════════════════════
// Construction & Default State
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Chain: default-constructed chain is empty")
{
    Chain chain;
    CHECK(chain.size() == 0);
}

TEST_CASE("Chain: getProcessorArray on empty chain returns empty vector")
{
    Chain chain;
    auto arr = chain.getProcessorArray();
    CHECK(arr.empty());
}

TEST_CASE("Chain: getLatencySamples on empty chain returns 0")
{
    Chain chain;
    CHECK(chain.getLatencySamples() == 0);
}

// ═══════════════════════════════════════════════════════════════════
// Append
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Chain: append increases size by 1")
{
    Chain chain;
    chain.append(makeTracker("A"));
    CHECK(chain.size() == 1);
}

TEST_CASE("Chain: append places processor at end")
{
    Chain chain;
    chain.append(makeTracker("A"));
    chain.append(makeTracker("B"));
    chain.append(makeTracker("C"));

    REQUIRE(chain.size() == 3);
    CHECK(chain.at(0)->getName() == "A");
    CHECK(chain.at(1)->getName() == "B");
    CHECK(chain.at(2)->getName() == "C");
}

TEST_CASE("Chain: append prepares processor if chain is already prepared")
{
    Chain chain;
    chain.prepare(44100.0, 512);

    auto p = makeTracker("A");
    auto* raw = p.get();
    chain.append(std::move(p));

    auto* tp = static_cast<TrackingProcessor*>(raw);
    CHECK(tp->prepareCount_ == 1);
    CHECK(tp->preparedSampleRate_ == 44100.0);
    CHECK(tp->preparedBlockSize_ == 512);
}

TEST_CASE("Chain: append does not prepare processor if chain is not prepared")
{
    Chain chain;

    auto p = makeTracker("A");
    auto* raw = p.get();
    chain.append(std::move(p));

    auto* tp = static_cast<TrackingProcessor*>(raw);
    CHECK(tp->prepareCount_ == 0);
}

// ═══════════════════════════════════════════════════════════════════
// Insert
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Chain: insert at 0 places processor at beginning")
{
    Chain chain;
    chain.append(makeTracker("A"));
    chain.append(makeTracker("B"));
    chain.insert(0, makeTracker("Z"));

    REQUIRE(chain.size() == 3);
    CHECK(chain.at(0)->getName() == "Z");
    CHECK(chain.at(1)->getName() == "A");
    CHECK(chain.at(2)->getName() == "B");
}

TEST_CASE("Chain: insert at middle shifts elements right")
{
    Chain chain;
    chain.append(makeTracker("A"));
    chain.append(makeTracker("C"));
    chain.insert(1, makeTracker("B"));

    REQUIRE(chain.size() == 3);
    CHECK(chain.at(0)->getName() == "A");
    CHECK(chain.at(1)->getName() == "B");
    CHECK(chain.at(2)->getName() == "C");
}

TEST_CASE("Chain: insert at size() is equivalent to append")
{
    Chain chain;
    chain.append(makeTracker("A"));
    chain.insert(1, makeTracker("B"));

    REQUIRE(chain.size() == 2);
    CHECK(chain.at(0)->getName() == "A");
    CHECK(chain.at(1)->getName() == "B");
}

TEST_CASE("Chain: insert with negative index clamps to 0")
{
    Chain chain;
    chain.append(makeTracker("A"));
    chain.insert(-5, makeTracker("Z"));

    REQUIRE(chain.size() == 2);
    CHECK(chain.at(0)->getName() == "Z");
    CHECK(chain.at(1)->getName() == "A");
}

TEST_CASE("Chain: insert with index beyond size clamps to size")
{
    Chain chain;
    chain.append(makeTracker("A"));
    chain.insert(100, makeTracker("Z"));

    REQUIRE(chain.size() == 2);
    CHECK(chain.at(0)->getName() == "A");
    CHECK(chain.at(1)->getName() == "Z");
}

TEST_CASE("Chain: insert prepares processor if chain is already prepared")
{
    Chain chain;
    chain.prepare(48000.0, 256);
    chain.append(makeTracker("A"));

    auto p = makeTracker("B");
    auto* raw = p.get();
    chain.insert(0, std::move(p));

    auto* tp = static_cast<TrackingProcessor*>(raw);
    CHECK(tp->prepareCount_ == 1);
    CHECK(tp->preparedSampleRate_ == 48000.0);
    CHECK(tp->preparedBlockSize_ == 256);
}

// ═══════════════════════════════════════════════════════════════════
// Remove
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Chain: remove returns the processor and decreases size")
{
    Chain chain;
    chain.append(makeTracker("A"));
    chain.append(makeTracker("B"));

    auto removed = chain.remove(0);
    REQUIRE(removed != nullptr);
    CHECK(removed->getName() == "A");
    CHECK(chain.size() == 1);
    CHECK(chain.at(0)->getName() == "B");
}

TEST_CASE("Chain: remove last element leaves chain empty")
{
    Chain chain;
    chain.append(makeTracker("A"));

    auto removed = chain.remove(0);
    CHECK(removed != nullptr);
    CHECK(chain.size() == 0);
}

TEST_CASE("Chain: remove with out-of-range index returns nullptr")
{
    Chain chain;
    chain.append(makeTracker("A"));

    CHECK(chain.remove(-1) == nullptr);
    CHECK(chain.remove(1) == nullptr);
    CHECK(chain.remove(100) == nullptr);
    CHECK(chain.size() == 1);  // unchanged
}

TEST_CASE("Chain: remove from empty chain returns nullptr")
{
    Chain chain;
    CHECK(chain.remove(0) == nullptr);
}

TEST_CASE("Chain: removed processor is still valid (caller owns it)")
{
    Chain chain;
    chain.append(makeTracker("A"));

    auto removed = chain.remove(0);
    REQUIRE(removed != nullptr);
    CHECK(removed->getName() == "A");
    // Can still interact with removed processor
    removed->prepare(44100.0, 512);
}

// ═══════════════════════════════════════════════════════════════════
// Move
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Chain: move reorders processors forward")
{
    Chain chain;
    chain.append(makeTracker("A"));
    chain.append(makeTracker("B"));
    chain.append(makeTracker("C"));

    chain.move(0, 2);  // A moves to end

    REQUIRE(chain.size() == 3);
    CHECK(chain.at(0)->getName() == "B");
    CHECK(chain.at(1)->getName() == "C");
    CHECK(chain.at(2)->getName() == "A");
}

TEST_CASE("Chain: move reorders processors backward")
{
    Chain chain;
    chain.append(makeTracker("A"));
    chain.append(makeTracker("B"));
    chain.append(makeTracker("C"));

    chain.move(2, 0);  // C moves to beginning

    REQUIRE(chain.size() == 3);
    CHECK(chain.at(0)->getName() == "C");
    CHECK(chain.at(1)->getName() == "A");
    CHECK(chain.at(2)->getName() == "B");
}

TEST_CASE("Chain: move to same index is a no-op")
{
    Chain chain;
    chain.append(makeTracker("A"));
    chain.append(makeTracker("B"));

    chain.move(0, 0);

    CHECK(chain.at(0)->getName() == "A");
    CHECK(chain.at(1)->getName() == "B");
}

TEST_CASE("Chain: move with out-of-range indices is a no-op")
{
    Chain chain;
    chain.append(makeTracker("A"));
    chain.append(makeTracker("B"));

    chain.move(-1, 0);  // invalid from
    CHECK(chain.at(0)->getName() == "A");

    chain.move(0, 5);   // invalid to
    CHECK(chain.at(0)->getName() == "A");

    chain.move(5, 0);   // invalid from
    CHECK(chain.at(0)->getName() == "A");

    CHECK(chain.size() == 2);
}

// ═══════════════════════════════════════════════════════════════════
// Clear
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Chain: clear destroys all processors")
{
    Chain chain;
    chain.append(makeTracker("A"));
    chain.append(makeTracker("B"));
    chain.append(makeTracker("C"));

    chain.clear();
    CHECK(chain.size() == 0);
    CHECK(chain.getProcessorArray().empty());
}

TEST_CASE("Chain: clear on empty chain is a no-op")
{
    Chain chain;
    chain.clear();
    CHECK(chain.size() == 0);
}

// ═══════════════════════════════════════════════════════════════════
// Query: at()
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Chain: at returns correct processor")
{
    Chain chain;
    chain.append(makeTracker("A"));
    chain.append(makeTracker("B"));

    CHECK(chain.at(0)->getName() == "A");
    CHECK(chain.at(1)->getName() == "B");
}

TEST_CASE("Chain: at with out-of-range index returns nullptr")
{
    Chain chain;
    chain.append(makeTracker("A"));

    CHECK(chain.at(-1) == nullptr);
    CHECK(chain.at(1) == nullptr);
    CHECK(chain.at(100) == nullptr);
}

TEST_CASE("Chain: at on empty chain returns nullptr")
{
    Chain chain;
    CHECK(chain.at(0) == nullptr);
}

// ═══════════════════════════════════════════════════════════════════
// Query: findByHandle()
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Chain: findByHandle returns processor with matching handle")
{
    Chain chain;
    auto p = makeTracker("A");
    p->setHandle(42);
    chain.append(std::move(p));

    auto* found = chain.findByHandle(42);
    REQUIRE(found != nullptr);
    CHECK(found->getName() == "A");
}

TEST_CASE("Chain: findByHandle returns nullptr for unknown handle")
{
    Chain chain;
    auto p = makeTracker("A");
    p->setHandle(42);
    chain.append(std::move(p));

    CHECK(chain.findByHandle(99) == nullptr);
}

TEST_CASE("Chain: findByHandle on empty chain returns nullptr")
{
    Chain chain;
    CHECK(chain.findByHandle(1) == nullptr);
}

TEST_CASE("Chain: findByHandle finds among multiple processors")
{
    Chain chain;
    auto a = makeTracker("A"); a->setHandle(10);
    auto b = makeTracker("B"); b->setHandle(20);
    auto c = makeTracker("C"); c->setHandle(30);
    chain.append(std::move(a));
    chain.append(std::move(b));
    chain.append(std::move(c));

    CHECK(chain.findByHandle(10)->getName() == "A");
    CHECK(chain.findByHandle(20)->getName() == "B");
    CHECK(chain.findByHandle(30)->getName() == "C");
}

// ═══════════════════════════════════════════════════════════════════
// Query: indexOf()
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Chain: indexOf returns correct index")
{
    Chain chain;
    chain.append(makeTracker("A"));
    chain.append(makeTracker("B"));

    CHECK(chain.indexOf(chain.at(0)) == 0);
    CHECK(chain.indexOf(chain.at(1)) == 1);
}

TEST_CASE("Chain: indexOf returns -1 for unknown processor")
{
    Chain chain;
    chain.append(makeTracker("A"));

    TrackingProcessor other("X");
    CHECK(chain.indexOf(&other) == -1);
}

TEST_CASE("Chain: indexOf returns -1 for nullptr")
{
    Chain chain;
    chain.append(makeTracker("A"));
    CHECK(chain.indexOf(nullptr) == -1);
}

TEST_CASE("Chain: indexOf on empty chain returns -1")
{
    Chain chain;
    TrackingProcessor other("X");
    CHECK(chain.indexOf(&other) == -1);
}

// ═══════════════════════════════════════════════════════════════════
// Prepare & Release
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Chain: prepare forwards to all processors")
{
    Chain chain;
    auto a = makeTracker("A");
    auto b = makeTracker("B");
    auto* rawA = a.get();
    auto* rawB = b.get();
    chain.append(std::move(a));
    chain.append(std::move(b));

    chain.prepare(44100.0, 512);

    CHECK(rawA->prepareCount_ == 1);
    CHECK(rawA->preparedSampleRate_ == 44100.0);
    CHECK(rawA->preparedBlockSize_ == 512);
    CHECK(rawB->prepareCount_ == 1);
    CHECK(rawB->preparedSampleRate_ == 44100.0);
    CHECK(rawB->preparedBlockSize_ == 512);
}

TEST_CASE("Chain: release forwards to all processors")
{
    Chain chain;
    auto a = makeTracker("A");
    auto b = makeTracker("B");
    auto* rawA = a.get();
    auto* rawB = b.get();
    chain.append(std::move(a));
    chain.append(std::move(b));

    chain.prepare(44100.0, 512);
    chain.release();

    CHECK(rawA->releaseCount_ == 1);
    CHECK(rawB->releaseCount_ == 1);
}

TEST_CASE("Chain: prepare on empty chain does not crash")
{
    Chain chain;
    chain.prepare(44100.0, 512);  // no-op, should not crash
}

TEST_CASE("Chain: release on empty chain does not crash")
{
    Chain chain;
    chain.release();  // no-op, should not crash
}

TEST_CASE("Chain: processor added after prepare is auto-prepared")
{
    Chain chain;
    chain.prepare(96000.0, 128);

    auto p = makeTracker("Late");
    auto* raw = p.get();
    chain.append(std::move(p));

    CHECK(raw->prepareCount_ == 1);
    CHECK(raw->preparedSampleRate_ == 96000.0);
    CHECK(raw->preparedBlockSize_ == 128);
}

TEST_CASE("Chain: inserted processor after prepare is auto-prepared")
{
    Chain chain;
    chain.prepare(44100.0, 256);
    chain.append(makeTracker("A"));

    auto p = makeTracker("Inserted");
    auto* raw = p.get();
    chain.insert(0, std::move(p));

    CHECK(raw->prepareCount_ == 1);
    CHECK(raw->preparedSampleRate_ == 44100.0);
    CHECK(raw->preparedBlockSize_ == 256);
}

// ═══════════════════════════════════════════════════════════════════
// Latency
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Chain: getLatencySamples returns sum of all processor latencies")
{
    Chain chain;
    chain.append(makeTracker("A", 128));
    chain.append(makeTracker("B", 256));
    chain.append(makeTracker("C", 64));

    CHECK(chain.getLatencySamples() == 448);
}

TEST_CASE("Chain: getLatencySamples with zero-latency processors")
{
    Chain chain;
    chain.append(makeTracker("A", 0));
    chain.append(makeTracker("B", 0));

    CHECK(chain.getLatencySamples() == 0);
}

TEST_CASE("Chain: getLatencySamples with single processor")
{
    Chain chain;
    chain.append(makeTracker("A", 512));

    CHECK(chain.getLatencySamples() == 512);
}

TEST_CASE("Chain: getLatencySamples updates after remove")
{
    Chain chain;
    chain.append(makeTracker("A", 100));
    chain.append(makeTracker("B", 200));

    CHECK(chain.getLatencySamples() == 300);

    chain.remove(0);
    CHECK(chain.getLatencySamples() == 200);
}

// ═══════════════════════════════════════════════════════════════════
// Snapshot: getProcessorArray()
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Chain: getProcessorArray returns pointers in order")
{
    Chain chain;
    chain.append(makeTracker("A"));
    chain.append(makeTracker("B"));
    chain.append(makeTracker("C"));

    auto arr = chain.getProcessorArray();
    REQUIRE(arr.size() == 3);
    CHECK(arr[0]->getName() == "A");
    CHECK(arr[1]->getName() == "B");
    CHECK(arr[2]->getName() == "C");
}

TEST_CASE("Chain: getProcessorArray returns a copy, not a reference")
{
    Chain chain;
    chain.append(makeTracker("A"));
    chain.append(makeTracker("B"));

    auto arr1 = chain.getProcessorArray();
    chain.append(makeTracker("C"));
    auto arr2 = chain.getProcessorArray();

    // arr1 still has 2 elements — it's a snapshot
    CHECK(arr1.size() == 2);
    CHECK(arr2.size() == 3);
}

TEST_CASE("Chain: getProcessorArray pointers match at() results")
{
    Chain chain;
    chain.append(makeTracker("A"));
    chain.append(makeTracker("B"));

    auto arr = chain.getProcessorArray();
    CHECK(arr[0] == chain.at(0));
    CHECK(arr[1] == chain.at(1));
}

// ═══════════════════════════════════════════════════════════════════
// Ownership & Destruction
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Chain: destructor destroys owned processors")
{
    bool destroyed = false;

    class DestructorTracker : public Processor {
    public:
        DestructorTracker(bool& flag) : Processor("DtorTracker"), flag_(flag) {}
        ~DestructorTracker() override { flag_ = true; }
        void prepare(double, int) override {}
        void process(juce::AudioBuffer<float>&) override {}
    private:
        bool& flag_;
    };

    {
        Chain chain;
        chain.append(std::make_unique<DestructorTracker>(destroyed));
        CHECK_FALSE(destroyed);
    }
    CHECK(destroyed);
}

TEST_CASE("Chain: removed processor is not destroyed by chain")
{
    bool destroyed = false;

    class DestructorTracker : public Processor {
    public:
        DestructorTracker(bool& flag) : Processor("DtorTracker"), flag_(flag) {}
        ~DestructorTracker() override { flag_ = true; }
        void prepare(double, int) override {}
        void process(juce::AudioBuffer<float>&) override {}
    private:
        bool& flag_;
    };

    auto removed = [&]() -> std::unique_ptr<Processor> {
        Chain chain;
        chain.append(std::make_unique<DestructorTracker>(destroyed));
        return chain.remove(0);
    }();

    // Chain was destroyed but processor lives on
    CHECK_FALSE(destroyed);
    CHECK(removed != nullptr);
}

// ═══════════════════════════════════════════════════════════════════
// Combined / Integration
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("Chain: full workflow — build, query, modify, snapshot")
{
    Chain chain;
    chain.prepare(44100.0, 512);

    // Build chain
    auto a = makeTracker("EQ", 64);
    auto b = makeTracker("Comp", 128);
    auto c = makeTracker("Limit", 32);
    a->setHandle(1);
    b->setHandle(2);
    c->setHandle(3);
    chain.append(std::move(a));
    chain.append(std::move(b));
    chain.append(std::move(c));

    CHECK(chain.size() == 3);
    CHECK(chain.getLatencySamples() == 224);

    // Query
    CHECK(chain.findByHandle(2)->getName() == "Comp");
    CHECK(chain.indexOf(chain.at(1)) == 1);

    // Snapshot before modification
    auto snap1 = chain.getProcessorArray();
    CHECK(snap1.size() == 3);

    // Insert saturator between EQ and Comp
    auto sat = makeTracker("Sat", 0);
    sat->setHandle(4);
    chain.insert(1, std::move(sat));

    CHECK(chain.size() == 4);
    CHECK(chain.at(0)->getName() == "EQ");
    CHECK(chain.at(1)->getName() == "Sat");
    CHECK(chain.at(2)->getName() == "Comp");
    CHECK(chain.at(3)->getName() == "Limit");

    // Old snapshot unchanged
    CHECK(snap1.size() == 3);

    // Remove saturator
    auto removed = chain.remove(1);
    CHECK(removed->getName() == "Sat");
    CHECK(chain.size() == 3);

    // Move limiter to front
    chain.move(2, 0);
    CHECK(chain.at(0)->getName() == "Limit");
    CHECK(chain.at(1)->getName() == "EQ");
    CHECK(chain.at(2)->getName() == "Comp");
}
