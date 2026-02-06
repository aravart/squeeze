#include <catch2/catch_test_macros.hpp>
#include "core/Scheduler.h"
#include "core/SPSCQueue.h"

#include <thread>
#include <vector>

using namespace squeeze;

// ============================================================
// SPSCQueue basics
// ============================================================

TEST_CASE("SPSCQueue starts empty")
{
    SPSCQueue<int, 4> q;
    REQUIRE(q.size() == 0);

    int val;
    REQUIRE_FALSE(q.tryPop(val));
}

TEST_CASE("SPSCQueue push and pop single item")
{
    SPSCQueue<int, 4> q;

    REQUIRE(q.tryPush(42));
    REQUIRE(q.size() == 1);

    int val = 0;
    REQUIRE(q.tryPop(val));
    REQUIRE(val == 42);
    REQUIRE(q.size() == 0);
}

TEST_CASE("SPSCQueue maintains FIFO order")
{
    SPSCQueue<int, 8> q;

    q.tryPush(1);
    q.tryPush(2);
    q.tryPush(3);

    int val;
    q.tryPop(val); REQUIRE(val == 1);
    q.tryPop(val); REQUIRE(val == 2);
    q.tryPop(val); REQUIRE(val == 3);
}

TEST_CASE("SPSCQueue returns false when full")
{
    SPSCQueue<int, 3> q;

    REQUIRE(q.tryPush(1));
    REQUIRE(q.tryPush(2));
    REQUIRE(q.tryPush(3));
    REQUIRE_FALSE(q.tryPush(4));  // full
}

TEST_CASE("SPSCQueue can be reused after draining")
{
    SPSCQueue<int, 2> q;

    q.tryPush(1);
    q.tryPush(2);

    int val;
    q.tryPop(val);
    q.tryPop(val);

    // Now empty, should accept new items
    REQUIRE(q.tryPush(3));
    REQUIRE(q.tryPush(4));

    q.tryPop(val); REQUIRE(val == 3);
    q.tryPop(val); REQUIRE(val == 4);
}

TEST_CASE("SPSCQueue wraps around correctly")
{
    SPSCQueue<int, 3> q;

    // Fill and drain several times to force wrap-around
    for (int round = 0; round < 5; ++round)
    {
        REQUIRE(q.tryPush(round * 10 + 1));
        REQUIRE(q.tryPush(round * 10 + 2));

        int val;
        q.tryPop(val); REQUIRE(val == round * 10 + 1);
        q.tryPop(val); REQUIRE(val == round * 10 + 2);
    }
}

TEST_CASE("SPSCQueue works with struct types")
{
    struct Pair { int a; float b; };

    SPSCQueue<Pair, 4> q;
    q.tryPush({10, 3.14f});

    Pair val;
    q.tryPop(val);
    REQUIRE(val.a == 10);
    REQUIRE(val.b == 3.14f);
}

// ============================================================
// SPSCQueue thread safety (basic stress test)
// ============================================================

TEST_CASE("SPSCQueue handles concurrent producer and consumer")
{
    SPSCQueue<int, 1024> q;
    const int N = 10000;

    std::thread producer([&]() {
        for (int i = 0; i < N; ++i)
            while (!q.tryPush(i)) {}  // spin until space
    });

    std::vector<int> received;
    received.reserve(N);

    std::thread consumer([&]() {
        int val;
        while ((int)received.size() < N) {
            if (q.tryPop(val))
                received.push_back(val);
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(received.size() == N);
    for (int i = 0; i < N; ++i)
        REQUIRE(received[i] == i);
}

// ============================================================
// GarbageItem
// ============================================================

TEST_CASE("GarbageItem::wrap creates item with correct deleter")
{
    bool deleted = false;
    struct Marker {
        bool& flag;
        ~Marker() { flag = true; }
    };

    auto* m = new Marker{deleted};
    GarbageItem item = GarbageItem::wrap(m);

    REQUIRE(item.ptr != nullptr);
    REQUIRE(item.deleter != nullptr);
    REQUIRE_FALSE(deleted);

    item.destroy();
    REQUIRE(deleted);
    REQUIRE(item.ptr == nullptr);
}

TEST_CASE("GarbageItem::destroy is safe to call with nullptr")
{
    GarbageItem item{nullptr, nullptr};
    item.destroy();  // should not crash
}

// ============================================================
// Scheduler: command queue
// ============================================================

TEST_CASE("Scheduler processes a single command")
{
    Scheduler sched;

    Command cmd;
    cmd.type = Command::Type::setParameter;
    cmd.paramValue = 0.75f;

    REQUIRE(sched.sendCommand(cmd));

    int count = 0;
    float receivedValue = 0.0f;

    sched.processPending([&](const Command& c) {
        receivedValue = c.paramValue;
        count++;
    });

    REQUIRE(count == 1);
    REQUIRE(receivedValue == 0.75f);
}

TEST_CASE("Scheduler processes commands in FIFO order")
{
    Scheduler sched;

    for (int i = 0; i < 5; ++i)
    {
        Command cmd;
        cmd.type = Command::Type::setParameter;
        cmd.paramIndex = i;
        sched.sendCommand(cmd);
    }

    std::vector<int> order;
    sched.processPending([&](const Command& c) {
        order.push_back(c.paramIndex);
    });

    REQUIRE(order.size() == 5);
    for (int i = 0; i < 5; ++i)
        REQUIRE(order[i] == i);
}

TEST_CASE("Scheduler processPending returns 0 when no commands")
{
    Scheduler sched;

    int count = sched.processPending([](const Command&) {});
    REQUIRE(count == 0);
}

TEST_CASE("Scheduler processes swapGraph command with pointer")
{
    Scheduler sched;

    int dummySnapshot = 42;

    Command cmd;
    cmd.type = Command::Type::swapGraph;
    cmd.ptr = &dummySnapshot;
    sched.sendCommand(cmd);

    void* receivedPtr = nullptr;
    sched.processPending([&](const Command& c) {
        REQUIRE(c.type == Command::Type::swapGraph);
        receivedPtr = c.ptr;
    });

    REQUIRE(receivedPtr == &dummySnapshot);
}

// ============================================================
// Scheduler: garbage collection
// ============================================================

TEST_CASE("Scheduler collects garbage sent from audio thread side")
{
    Scheduler sched;

    bool deleted = false;
    struct Marker {
        bool& flag;
        ~Marker() { flag = true; }
    };

    auto* m = new Marker{deleted};
    REQUIRE(sched.sendGarbage(GarbageItem::wrap(m)));

    REQUIRE_FALSE(deleted);
    sched.collectGarbage();
    REQUIRE(deleted);
}

TEST_CASE("Scheduler collectGarbage handles multiple items")
{
    Scheduler sched;

    int deleteCount = 0;
    struct Counter {
        int& count;
        ~Counter() { count++; }
    };

    for (int i = 0; i < 5; ++i)
        sched.sendGarbage(GarbageItem::wrap(new Counter{deleteCount}));

    sched.collectGarbage();
    REQUIRE(deleteCount == 5);
}

TEST_CASE("Scheduler collectGarbage is a no-op when empty")
{
    Scheduler sched;
    sched.collectGarbage();  // should not crash
}

// ============================================================
// Scheduler: round-trip pattern
// ============================================================

TEST_CASE("Full round trip: send command, process, garbage collect")
{
    Scheduler sched;

    // Simulate: control thread allocates a "snapshot"
    auto* snapshot = new int(99);

    Command cmd;
    cmd.type = Command::Type::swapGraph;
    cmd.ptr = snapshot;
    sched.sendCommand(cmd);

    // Audio thread processes command, swaps snapshot, sends old one to garbage
    int* oldSnapshot = nullptr;
    sched.processPending([&](const Command& c) {
        // "Swap" — in real code, this replaces the active snapshot
        oldSnapshot = static_cast<int*>(c.ptr);
    });

    // Audio thread sends old snapshot to garbage
    // (In this test, the "old" one is the one we just received — simplified)
    sched.sendGarbage(GarbageItem::wrap(oldSnapshot));

    // Control thread collects
    sched.collectGarbage();

    // snapshot was deleted by collectGarbage — can't access it
    // but no crash means success
}
