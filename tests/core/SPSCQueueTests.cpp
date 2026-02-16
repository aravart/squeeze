#include <catch2/catch_test_macros.hpp>
#include "core/SPSCQueue.h"

using squeeze::SPSCQueue;

TEST_CASE("SPSCQueue: push one item and pop returns it")
{
    SPSCQueue<int, 4> q;
    REQUIRE(q.tryPush(42));
    int out = 0;
    REQUIRE(q.tryPop(out));
    REQUIRE(out == 42);
}

TEST_CASE("SPSCQueue: pop on empty queue returns false")
{
    SPSCQueue<int, 4> q;
    int out = 0;
    REQUIRE_FALSE(q.tryPop(out));
}

TEST_CASE("SPSCQueue: empty returns true on fresh queue")
{
    SPSCQueue<int, 4> q;
    REQUIRE(q.empty());
}

TEST_CASE("SPSCQueue: empty returns false after push")
{
    SPSCQueue<int, 4> q;
    q.tryPush(1);
    REQUIRE_FALSE(q.empty());
}

TEST_CASE("SPSCQueue: size is 0 on fresh queue")
{
    SPSCQueue<int, 4> q;
    REQUIRE(q.size() == 0);
}

TEST_CASE("SPSCQueue: size reflects items after push")
{
    SPSCQueue<int, 4> q;
    q.tryPush(1);
    q.tryPush(2);
    q.tryPush(3);
    REQUIRE(q.size() == 3);
}

TEST_CASE("SPSCQueue: size decreases after pop")
{
    SPSCQueue<int, 4> q;
    q.tryPush(1);
    q.tryPush(2);
    int out;
    q.tryPop(out);
    REQUIRE(q.size() == 1);
}

TEST_CASE("SPSCQueue: fill to capacity succeeds")
{
    SPSCQueue<int, 4> q;
    REQUIRE(q.tryPush(1));
    REQUIRE(q.tryPush(2));
    REQUIRE(q.tryPush(3));
    REQUIRE(q.tryPush(4));
    REQUIRE(q.size() == 4);
}

TEST_CASE("SPSCQueue: push when full returns false")
{
    SPSCQueue<int, 4> q;
    q.tryPush(1);
    q.tryPush(2);
    q.tryPush(3);
    q.tryPush(4);
    REQUIRE_FALSE(q.tryPush(5));
}

TEST_CASE("SPSCQueue: FIFO order preserved")
{
    SPSCQueue<int, 8> q;
    q.tryPush(10);
    q.tryPush(20);
    q.tryPush(30);

    int out;
    q.tryPop(out); REQUIRE(out == 10);
    q.tryPop(out); REQUIRE(out == 20);
    q.tryPop(out); REQUIRE(out == 30);
}

TEST_CASE("SPSCQueue: interleaved push/pop maintains FIFO")
{
    SPSCQueue<int, 4> q;
    int out;

    q.tryPush(1);
    q.tryPush(2);
    q.tryPop(out); REQUIRE(out == 1);

    q.tryPush(3);
    q.tryPop(out); REQUIRE(out == 2);
    q.tryPop(out); REQUIRE(out == 3);
}

TEST_CASE("SPSCQueue: full then pop makes space for another push")
{
    SPSCQueue<int, 2> q;
    q.tryPush(1);
    q.tryPush(2);
    REQUIRE_FALSE(q.tryPush(3));

    int out;
    q.tryPop(out);
    REQUIRE(q.tryPush(3));

    q.tryPop(out); REQUIRE(out == 2);
    q.tryPop(out); REQUIRE(out == 3);
}

TEST_CASE("SPSCQueue: does not modify output on failed pop")
{
    SPSCQueue<int, 4> q;
    int out = 99;
    REQUIRE_FALSE(q.tryPop(out));
    REQUIRE(out == 99);
}

TEST_CASE("SPSCQueue: reset clears the queue")
{
    SPSCQueue<int, 4> q;
    q.tryPush(1);
    q.tryPush(2);
    q.reset();
    REQUIRE(q.empty());
    REQUIRE(q.size() == 0);
}

TEST_CASE("SPSCQueue: push and pop work after reset")
{
    SPSCQueue<int, 4> q;
    q.tryPush(1);
    q.tryPush(2);
    q.reset();

    REQUIRE(q.tryPush(10));
    int out;
    REQUIRE(q.tryPop(out));
    REQUIRE(out == 10);
}

TEST_CASE("SPSCQueue: capacity 1 works correctly")
{
    SPSCQueue<int, 1> q;
    REQUIRE(q.tryPush(42));
    REQUIRE_FALSE(q.tryPush(99));
    REQUIRE(q.size() == 1);

    int out;
    REQUIRE(q.tryPop(out));
    REQUIRE(out == 42);
    REQUIRE(q.empty());
}

TEST_CASE("SPSCQueue: large capacity works")
{
    SPSCQueue<int, 4096> q;
    for (int i = 0; i < 4096; ++i)
        REQUIRE(q.tryPush(i));
    REQUIRE_FALSE(q.tryPush(9999));
    REQUIRE(q.size() == 4096);

    for (int i = 0; i < 4096; ++i)
    {
        int out;
        REQUIRE(q.tryPop(out));
        REQUIRE(out == i);
    }
    REQUIRE(q.empty());
}

TEST_CASE("SPSCQueue: works with struct element type")
{
    struct Event {
        int type;
        float value;
    };

    SPSCQueue<Event, 8> q;
    REQUIRE(q.tryPush({1, 3.14f}));
    REQUIRE(q.tryPush({2, 2.72f}));

    Event out;
    REQUIRE(q.tryPop(out));
    REQUIRE(out.type == 1);
    REQUIRE(out.value == 3.14f);

    REQUIRE(q.tryPop(out));
    REQUIRE(out.type == 2);
    REQUIRE(out.value == 2.72f);
}

TEST_CASE("SPSCQueue: wraparound maintains correctness")
{
    SPSCQueue<int, 3> q;
    int out;

    // Fill and drain a few times to force wraparound
    for (int round = 0; round < 5; ++round)
    {
        REQUIRE(q.tryPush(round * 10 + 1));
        REQUIRE(q.tryPush(round * 10 + 2));
        REQUIRE(q.tryPush(round * 10 + 3));
        REQUIRE_FALSE(q.tryPush(999));

        q.tryPop(out); REQUIRE(out == round * 10 + 1);
        q.tryPop(out); REQUIRE(out == round * 10 + 2);
        q.tryPop(out); REQUIRE(out == round * 10 + 3);
        REQUIRE(q.empty());
    }
}
