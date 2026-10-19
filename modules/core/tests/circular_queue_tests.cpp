#include <core/circular_queue.h>

#include <gtest/gtest.h>

#include <memory>

using namespace novainfra;

TEST(CircularQueue, DefaultConstruction)
{
    CircularQueue<int> q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
}

TEST(CircularQueue, BasicPushAndPop)
{
    CircularQueue<int> q(5);

    q.pushBack(1);
    q.pushBack(2);
    q.pushBack(3);

    EXPECT_EQ(q.size(), 3u);
    EXPECT_FALSE(q.empty());
    EXPECT_FALSE(q.full());

    EXPECT_EQ(q.front(), 1);
    q.popFront();

    EXPECT_EQ(q.front(), 2);
    q.popFront();

    EXPECT_EQ(q.front(), 3);
    q.popFront();

    EXPECT_TRUE(q.empty());
}

TEST(CircularQueue, FullQueue)
{
    CircularQueue<int> q(3);

    q.pushBack(1);
    q.pushBack(2);
    q.pushBack(3);

    EXPECT_TRUE(q.full());
    EXPECT_EQ(q.size(), 3u);
}

TEST(CircularQueue, OverrunBehavior)
{
    CircularQueue<int> q(3);

    q.pushBack(1);
    q.pushBack(2);
    q.pushBack(3);
    EXPECT_EQ(q.overrunCounter(), 0u);

    // Push when full - should overrun the oldest (1)
    q.pushBack(4);
    EXPECT_EQ(q.overrunCounter(), 1u);
    EXPECT_EQ(q.size(), 3u);
    EXPECT_EQ(q.front(), 2);

    // Push again - should overrun 2
    q.pushBack(5);
    EXPECT_EQ(q.overrunCounter(), 2u);
    EXPECT_EQ(q.front(), 3);
}

TEST(CircularQueue, AtAccessor)
{
    CircularQueue<int> q(5);

    q.pushBack(10);
    q.pushBack(20);
    q.pushBack(30);

    EXPECT_EQ(q.at(0), 10);
    EXPECT_EQ(q.at(1), 20);
    EXPECT_EQ(q.at(2), 30);
}

TEST(CircularQueue, AtAccessorAfterWraparound)
{
    CircularQueue<int> q(3);

    q.pushBack(1);
    q.pushBack(2);
    q.pushBack(3);
    q.pushBack(4);  // Overruns 1

    EXPECT_EQ(q.at(0), 2);
    EXPECT_EQ(q.at(1), 3);
    EXPECT_EQ(q.at(2), 4);
}

TEST(CircularQueue, ResetOverrunCounter)
{
    CircularQueue<int> q(2);

    q.pushBack(1);
    q.pushBack(2);
    q.pushBack(3);  // Overrun
    q.pushBack(4);  // Overrun

    EXPECT_EQ(q.overrunCounter(), 2u);

    q.resetOverrunCounter();
    EXPECT_EQ(q.overrunCounter(), 0u);
}

TEST(CircularQueue, CopyConstruction)
{
    CircularQueue<int> q1(5);
    q1.pushBack(1);
    q1.pushBack(2);
    q1.pushBack(3);

    CircularQueue<int> q2(q1);

    EXPECT_EQ(q2.size(), 3u);
    EXPECT_EQ(q2.front(), 1);
    EXPECT_EQ(q2.at(1), 2);
    EXPECT_EQ(q2.at(2), 3);
}

TEST(CircularQueue, CopyAssignment)
{
    CircularQueue<int> q1(5);
    q1.pushBack(1);
    q1.pushBack(2);

    CircularQueue<int> q2(10);
    q2 = q1;

    EXPECT_EQ(q2.size(), 2u);
    EXPECT_EQ(q2.front(), 1);
}

TEST(CircularQueue, MoveConstruction)
{
    CircularQueue<int> q1(3);
    q1.pushBack(1);
    q1.pushBack(2);
    q1.pushBack(3);
    q1.pushBack(4);  // Causes overrun

    CircularQueue<int> q2(std::move(q1));

    EXPECT_EQ(q2.size(), 3u);
    EXPECT_EQ(q2.overrunCounter(), 1u);
    EXPECT_EQ(q2.front(), 2);

    // q1 should be in valid empty state
    EXPECT_EQ(q1.size(), 0u);
    EXPECT_TRUE(q1.empty());
}

TEST(CircularQueue, MoveAssignment)
{
    CircularQueue<int> q1(5);
    q1.pushBack(1);
    q1.pushBack(2);

    CircularQueue<int> q2(10);
    q2 = std::move(q1);

    EXPECT_EQ(q2.size(), 2u);
    EXPECT_EQ(q2.front(), 1);

    // q1 should be in valid empty state
    EXPECT_EQ(q1.size(), 0u);
    EXPECT_TRUE(q1.empty());
}

TEST(CircularQueue, MoveOnlyTypes)
{
    CircularQueue<std::unique_ptr<int>> q(3);

    q.pushBack(std::make_unique<int>(42));
    q.pushBack(std::make_unique<int>(100));

    EXPECT_EQ(*q.front(), 42);
    q.popFront();
    EXPECT_EQ(*q.front(), 100);
}

TEST(CircularQueue, CapacityOne)
{
    CircularQueue<int> q(1);

    q.pushBack(1);
    EXPECT_TRUE(q.full());
    EXPECT_EQ(q.size(), 1u);
    EXPECT_EQ(q.front(), 1);

    q.pushBack(2);  // Should overrun
    EXPECT_EQ(q.overrunCounter(), 1u);
    EXPECT_EQ(q.front(), 2);
}

TEST(CircularQueue, AlternatingPushPop)
{
    CircularQueue<int> q(3);

    for (int i = 0; i < 10; ++i) {
        q.pushBack(std::move(i));
        EXPECT_EQ(q.front(), i);
        q.popFront();
        EXPECT_TRUE(q.empty());
    }

    EXPECT_EQ(q.overrunCounter(), 0u);
}

TEST(CircularQueue, FrontMutability)
{
    CircularQueue<int> q(3);
    q.pushBack(10);

    // Modify through non-const front()
    q.front() = 99;
    EXPECT_EQ(q.front(), 99);
}

#ifndef NOVAINFRA_NO_EXCEPTIONS
TEST(CircularQueue, RejectsCapacityOverflow)
{
    EXPECT_THROW(CircularQueue<int>(std::numeric_limits<std::size_t>::max()), std::length_error);
}
#endif
