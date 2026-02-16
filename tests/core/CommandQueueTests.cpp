#include <catch2/catch_test_macros.hpp>
#include "core/CommandQueue.h"
#include <vector>

using squeeze::Command;
using squeeze::CommandQueue;
using squeeze::GarbageItem;

// ---- Command struct tests ----

TEST_CASE("Command: default payload fields are zero-initialized")
{
    Command cmd;
    cmd.type = Command::Type::transportPlay;
    REQUIRE(cmd.ptr == nullptr);
    REQUIRE(cmd.doubleValue1 == 0.0);
    REQUIRE(cmd.doubleValue2 == 0.0);
    REQUIRE(cmd.int64Value == 0);
    REQUIRE(cmd.intValue1 == 0);
    REQUIRE(cmd.intValue2 == 0);
}

TEST_CASE("Command: swapSnapshot carries pointer payload")
{
    int dummy = 42;
    Command cmd;
    cmd.type = Command::Type::swapSnapshot;
    cmd.ptr = &dummy;
    REQUIRE(cmd.ptr == &dummy);
}

TEST_CASE("Command: setTempo carries double payload")
{
    Command cmd;
    cmd.type = Command::Type::setTempo;
    cmd.doubleValue1 = 120.0;
    REQUIRE(cmd.doubleValue1 == 120.0);
}

TEST_CASE("Command: setTimeSignature carries int payloads")
{
    Command cmd;
    cmd.type = Command::Type::setTimeSignature;
    cmd.intValue1 = 3;
    cmd.intValue2 = 4;
    REQUIRE(cmd.intValue1 == 3);
    REQUIRE(cmd.intValue2 == 4);
}

TEST_CASE("Command: setLoopPoints carries two doubles")
{
    Command cmd;
    cmd.type = Command::Type::setLoopPoints;
    cmd.doubleValue1 = 4.0;
    cmd.doubleValue2 = 8.0;
    REQUIRE(cmd.doubleValue1 == 4.0);
    REQUIRE(cmd.doubleValue2 == 8.0);
}

TEST_CASE("Command: seekSamples carries int64 payload")
{
    Command cmd;
    cmd.type = Command::Type::seekSamples;
    cmd.int64Value = 48000LL * 60;
    REQUIRE(cmd.int64Value == 48000LL * 60);
}

// ---- GarbageItem tests ----

TEST_CASE("GarbageItem: destroy calls deleter and nulls ptr")
{
    bool deleted = false;
    GarbageItem item;
    item.ptr = &deleted;
    item.deleter = [](void* p) { *static_cast<bool*>(p) = true; };

    item.destroy();
    REQUIRE(deleted);
    REQUIRE(item.ptr == nullptr);
}

TEST_CASE("GarbageItem: destroy is no-op with null ptr")
{
    GarbageItem item;
    item.ptr = nullptr;
    item.deleter = [](void*) { FAIL("should not be called"); };
    item.destroy();  // should not crash or call deleter
}

TEST_CASE("GarbageItem: destroy is no-op with null deleter")
{
    int dummy = 0;
    GarbageItem item;
    item.ptr = &dummy;
    item.deleter = nullptr;
    item.destroy();  // should not crash
    REQUIRE(item.ptr == nullptr);
}

TEST_CASE("GarbageItem: destroy is safe to call twice")
{
    int callCount = 0;
    GarbageItem item;
    item.ptr = &callCount;
    item.deleter = [](void* p) { (*static_cast<int*>(p))++; };

    item.destroy();
    item.destroy();
    REQUIRE(callCount == 1);
}

TEST_CASE("GarbageItem: wrap creates correct deleter for heap object")
{
    auto* p = new int(42);
    GarbageItem item = GarbageItem::wrap(p);
    REQUIRE(item.ptr == p);
    REQUIRE(item.deleter != nullptr);
    item.destroy();  // should free without leak
    REQUIRE(item.ptr == nullptr);
}

TEST_CASE("GarbageItem: default constructed is safe to destroy")
{
    GarbageItem item;
    item.destroy();  // no crash
}

// ---- CommandQueue tests ----

TEST_CASE("CommandQueue: sendCommand and processPending round-trip")
{
    CommandQueue q;
    Command cmd;
    cmd.type = Command::Type::transportPlay;
    REQUIRE(q.sendCommand(cmd));

    std::vector<Command::Type> received;
    int count = q.processPending([&](const Command& c) {
        received.push_back(c.type);
    });

    REQUIRE(count == 1);
    REQUIRE(received.size() == 1);
    REQUIRE(received[0] == Command::Type::transportPlay);
}

TEST_CASE("CommandQueue: processPending returns 0 when empty")
{
    CommandQueue q;
    int count = q.processPending([](const Command&) {
        FAIL("should not be called");
    });
    REQUIRE(count == 0);
}

TEST_CASE("CommandQueue: commands processed in FIFO order")
{
    CommandQueue q;

    Command c1; c1.type = Command::Type::transportPlay;
    Command c2; c2.type = Command::Type::setTempo; c2.doubleValue1 = 140.0;
    Command c3; c3.type = Command::Type::transportStop;

    q.sendCommand(c1);
    q.sendCommand(c2);
    q.sendCommand(c3);

    std::vector<Command::Type> order;
    q.processPending([&](const Command& c) {
        order.push_back(c.type);
    });

    REQUIRE(order.size() == 3);
    REQUIRE(order[0] == Command::Type::transportPlay);
    REQUIRE(order[1] == Command::Type::setTempo);
    REQUIRE(order[2] == Command::Type::transportStop);
}

TEST_CASE("CommandQueue: swapSnapshot payload survives round-trip")
{
    CommandQueue q;
    int dummy = 99;

    Command cmd;
    cmd.type = Command::Type::swapSnapshot;
    cmd.ptr = &dummy;
    q.sendCommand(cmd);

    void* received = nullptr;
    q.processPending([&](const Command& c) {
        received = c.ptr;
    });
    REQUIRE(received == &dummy);
}

TEST_CASE("CommandQueue: sendGarbage and collectGarbage round-trip")
{
    CommandQueue q;
    bool deleted = false;

    GarbageItem item;
    item.ptr = &deleted;
    item.deleter = [](void* p) { *static_cast<bool*>(p) = true; };

    REQUIRE(q.sendGarbage(item));
    REQUIRE_FALSE(deleted);

    int count = q.collectGarbage();
    REQUIRE(count == 1);
    REQUIRE(deleted);
}

TEST_CASE("CommandQueue: collectGarbage returns 0 when empty")
{
    CommandQueue q;
    REQUIRE(q.collectGarbage() == 0);
}

TEST_CASE("CommandQueue: collectGarbage drains multiple items")
{
    CommandQueue q;
    int deleteCount = 0;

    for (int i = 0; i < 5; ++i) {
        GarbageItem item;
        item.ptr = &deleteCount;
        item.deleter = [](void* p) { (*static_cast<int*>(p))++; };
        q.sendGarbage(item);
    }

    int count = q.collectGarbage();
    REQUIRE(count == 5);
    REQUIRE(deleteCount == 5);
}

TEST_CASE("CommandQueue: wrap and collectGarbage deletes heap object")
{
    CommandQueue q;
    auto* p = new int(42);
    q.sendGarbage(GarbageItem::wrap(p));
    int count = q.collectGarbage();
    REQUIRE(count == 1);
    // If this leaks, sanitizers will catch it
}

TEST_CASE("CommandQueue: commands and garbage are independent queues")
{
    CommandQueue q;

    Command cmd;
    cmd.type = Command::Type::transportPause;
    q.sendCommand(cmd);

    bool deleted = false;
    GarbageItem item;
    item.ptr = &deleted;
    item.deleter = [](void* p) { *static_cast<bool*>(p) = true; };
    q.sendGarbage(item);

    // Process only commands — garbage should stay
    int cmdCount = q.processPending([](const Command&) {});
    REQUIRE(cmdCount == 1);
    REQUIRE_FALSE(deleted);

    // Collect only garbage — no commands left
    int garbCount = q.collectGarbage();
    REQUIRE(garbCount == 1);
    REQUIRE(deleted);
}

TEST_CASE("commandTypeName: returns correct names for all types")
{
    REQUIRE(std::string(squeeze::commandTypeName(Command::Type::swapSnapshot)) == "swapSnapshot");
    REQUIRE(std::string(squeeze::commandTypeName(Command::Type::transportPlay)) == "transportPlay");
    REQUIRE(std::string(squeeze::commandTypeName(Command::Type::transportStop)) == "transportStop");
    REQUIRE(std::string(squeeze::commandTypeName(Command::Type::transportPause)) == "transportPause");
    REQUIRE(std::string(squeeze::commandTypeName(Command::Type::setTempo)) == "setTempo");
    REQUIRE(std::string(squeeze::commandTypeName(Command::Type::setTimeSignature)) == "setTimeSignature");
    REQUIRE(std::string(squeeze::commandTypeName(Command::Type::seekSamples)) == "seekSamples");
    REQUIRE(std::string(squeeze::commandTypeName(Command::Type::seekBeats)) == "seekBeats");
    REQUIRE(std::string(squeeze::commandTypeName(Command::Type::setLoopPoints)) == "setLoopPoints");
    REQUIRE(std::string(squeeze::commandTypeName(Command::Type::setLooping)) == "setLooping");
}
