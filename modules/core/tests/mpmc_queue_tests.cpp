#include <core/mpmc_blocking_queue.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace novainfra;
using namespace std::chrono_literals;

TEST(MpmcBlockingQueue, BasicEnqueueDequeue)
{
    MpmcBlockingQueue<int> q(10);

    q.enqueue(1);
    q.enqueue(2);
    q.enqueue(3);

    EXPECT_EQ(q.size(), 3u);

    int val;
    q.dequeue(val);
    EXPECT_EQ(val, 1);

    q.dequeue(val);
    EXPECT_EQ(val, 2);

    q.dequeue(val);
    EXPECT_EQ(val, 3);

    EXPECT_EQ(q.size(), 0u);
}

TEST(MpmcBlockingQueue, DequeueForTimeout)
{
    MpmcBlockingQueue<int> q(10);

    int val = 0;
    auto start = std::chrono::steady_clock::now();
    bool result = q.dequeueFor(val, 50ms);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result);
    EXPECT_GE(elapsed, 50ms);
}

TEST(MpmcBlockingQueue, DequeueForSuccess)
{
    MpmcBlockingQueue<int> q(10);
    q.enqueue(42);

    int val = 0;
    bool result = q.dequeueFor(val, 100ms);

    EXPECT_TRUE(result);
    EXPECT_EQ(val, 42);
}

TEST(MpmcBlockingQueue, EnqueueNoWaitOverrun)
{
    MpmcBlockingQueue<int> q(3);

    q.enqueueNoWait(1);
    q.enqueueNoWait(2);
    q.enqueueNoWait(3);
    EXPECT_EQ(q.size(), 3u);

    // This should overrun the oldest item (1)
    q.enqueueNoWait(4);
    EXPECT_EQ(q.size(), 3u);
    EXPECT_EQ(q.overrunCounter(), 1u);

    int val;
    q.dequeue(val);
    EXPECT_EQ(val, 2);  // 1 was overrun
}

TEST(MpmcBlockingQueue, EnqueueNoWaitWithGuard)
{
    MpmcBlockingQueue<int> q(3);

    q.enqueueNoWait(1);
    q.enqueueNoWait(2);
    q.enqueueNoWait(3);

    // Try to enqueue with guard that prevents overrun
    bool result = q.enqueueNoWait(4, [](const int &front) { return front != 1; });
    EXPECT_FALSE(result);  // Should fail because guard protects item 1
    EXPECT_EQ(q.size(), 3u);

    // Now with a guard that allows overrun
    result = q.enqueueNoWait(4, [](const int &) { return true; });
    EXPECT_TRUE(result);
    EXPECT_EQ(q.size(), 3u);
}

TEST(MpmcBlockingQueue, EnqueueIfHaveRoom)
{
    MpmcBlockingQueue<int> q(2);

    q.enqueueIfHaveRoom(1);
    q.enqueueIfHaveRoom(2);
    EXPECT_EQ(q.size(), 2u);
    EXPECT_EQ(q.discardCounter(), 0u);

    // Queue is full, this should be discarded
    q.enqueueIfHaveRoom(3);
    EXPECT_EQ(q.size(), 2u);
    EXPECT_EQ(q.discardCounter(), 1u);

    int val;
    q.dequeue(val);
    EXPECT_EQ(val, 1);

    // Now there's room
    q.enqueueIfHaveRoom(4);
    EXPECT_EQ(q.size(), 2u);
}

TEST(MpmcBlockingQueue, MultiProducerMultiConsumer)
{
    constexpr int numProducers = 4;
    constexpr int numConsumers = 4;
    constexpr int itemsPerProducer = 100;

    MpmcBlockingQueue<int> q(50);
    std::atomic<int> producedCount{0};
    std::atomic<int> consumedCount{0};
    std::vector<int> consumedValues;
    std::mutex valuesMutex;

    // Start producers
    std::vector<std::thread> producers;
    for (int p = 0; p < numProducers; ++p) {
        producers.emplace_back([&q, &producedCount, p]() {
            for (int i = 0; i < itemsPerProducer; ++i) {
                q.enqueue(p * 1000 + i);
                ++producedCount;
            }
        });
    }

    // Start consumers
    std::vector<std::thread> consumers;
    for (int c = 0; c < numConsumers; ++c) {
        consumers.emplace_back([&q, &consumedCount, &consumedValues, &valuesMutex]() {
            int val;
            while (consumedCount < numProducers * itemsPerProducer) {
                if (q.dequeueFor(val, 100ms)) {
                    {
                        std::lock_guard<std::mutex> lock(valuesMutex);
                        consumedValues.push_back(val);
                    }
                    ++consumedCount;
                }
            }
        });
    }

    for (auto &t : producers)
        t.join();
    for (auto &t : consumers)
        t.join();

    EXPECT_EQ(consumedCount.load(), numProducers * itemsPerProducer);
    EXPECT_EQ(consumedValues.size(), static_cast<size_t>(numProducers * itemsPerProducer));
}

TEST(MpmcBlockingQueue, ResetCounters)
{
    MpmcBlockingQueue<int> q(2);

    q.enqueueNoWait(1);
    q.enqueueNoWait(2);
    q.enqueueNoWait(3);  // Overrun
    EXPECT_EQ(q.overrunCounter(), 1u);

    q.enqueueIfHaveRoom(4);  // Discard
    EXPECT_EQ(q.discardCounter(), 1u);

    q.resetOverrunCounter();
    q.resetDiscardCounter();

    EXPECT_EQ(q.overrunCounter(), 0u);
    EXPECT_EQ(q.discardCounter(), 0u);
}

TEST(MpmcBlockingQueue, MoveSemantics)
{
    MpmcBlockingQueue<std::unique_ptr<int>> q(5);

    q.enqueue(std::make_unique<int>(42));
    q.enqueue(std::make_unique<int>(100));

    std::unique_ptr<int> val;
    q.dequeue(val);
    EXPECT_NE(val, nullptr);
    EXPECT_EQ(*val, 42);
}

#ifndef NOVAINFRA_NO_EXCEPTIONS
TEST(MpmcBlockingQueue, InvalidCapacity)
{
    EXPECT_THROW(MpmcBlockingQueue<int>(0), std::invalid_argument);
    EXPECT_THROW(MpmcBlockingQueue<int>(std::numeric_limits<size_t>::max()), std::invalid_argument);
}

#endif
