#include <core/mpmc_blocking_queue.h>

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace novainfra;
using namespace std::chrono_literals;

// 生产者-消费者模式示例
int main()
{
    std::cout << "=== Producer-Consumer Pattern with MpmcBlockingQueue ===\n\n";

    // 创建有界队列，容量为 10
    MpmcBlockingQueue<int> queue(10);

    constexpr int numProducers = 3;
    constexpr int numConsumers = 2;
    constexpr int itemsPerProducer = 20;

    std::cout << "Starting " << numProducers << " producers and " << numConsumers << " consumers\n";
    std::cout << "Each producer will produce " << itemsPerProducer << " items\n\n";

    // 启动生产者线程
    std::vector<std::thread> producers;
    for (int p = 0; p < numProducers; ++p) {
        producers.emplace_back([&queue, p]() {
            for (int i = 0; i < itemsPerProducer; ++i) {
                int value = p * 100 + i;
                queue.enqueue(std::move(value));
                std::cout << "[Producer " << p << "] Enqueued: " << value << " (queue size: " << queue.size()
                          << ")\n";
                std::this_thread::sleep_for(10ms);
            }
            std::cout << "[Producer " << p << "] Finished\n";
        });
    }

    // 启动消费者线程
    std::vector<std::thread> consumers;
    std::atomic<int> totalConsumed{0};

    for (int c = 0; c < numConsumers; ++c) {
        consumers.emplace_back([&queue, &totalConsumed, c]() {
            int expectedTotal = numProducers * itemsPerProducer;
            while (totalConsumed < expectedTotal) {
                int value;
                if (queue.dequeueFor(value, 100ms)) {
                    std::cout << "[Consumer " << c << "] Dequeued: " << value
                              << " (queue size: " << queue.size() << ")\n";
                    ++totalConsumed;
                    std::this_thread::sleep_for(15ms);
                }
            }
            std::cout << "[Consumer " << c << "] Finished\n";
        });
    }

    // 等待所有生产者完成
    for (auto &t : producers) {
        t.join();
    }

    // 等待所有消费者完成
    for (auto &t : consumers) {
        t.join();
    }

    std::cout << "\n=== Summary ===\n";
    std::cout << "Total items consumed: " << totalConsumed.load() << "\n";
    std::cout << "Expected: " << numProducers * itemsPerProducer << "\n";
    std::cout << "Queue overruns: " << queue.overrunCounter() << "\n";
    std::cout << "Queue discards: " << queue.discardCounter() << "\n";
    std::cout << "Final queue size: " << queue.size() << "\n";

    std::cout << "\n=== Different Enqueue Strategies ===\n\n";

    MpmcBlockingQueue<int> smallQueue(3);

    // 策略 1: enqueue (阻塞直到有空间)
    std::cout << "1. enqueue() - Blocking until space available\n";
    smallQueue.enqueue(1);
    smallQueue.enqueue(2);
    smallQueue.enqueue(3);
    std::cout << "   Queue full, size: " << smallQueue.size() << "\n";

    int val;
    smallQueue.dequeue(val);
    std::cout << "   Dequeued: " << val << ", size: " << smallQueue.size() << "\n";

    // 策略 2: enqueueNoWait (立即入队，满时覆盖最旧项)
    std::cout << "\n2. enqueueNoWait() - Immediate, overruns oldest when full\n";
    smallQueue.enqueueNoWait(4);
    smallQueue.enqueueNoWait(5);  // Will overrun item 2
    std::cout << "   Overrun counter: " << smallQueue.overrunCounter() << "\n";

    int frontItem;
    smallQueue.dequeue(frontItem);
    std::cout << "   Front item was: " << frontItem << " (should be 3)\n";

    // 策略 3: enqueueIfHaveRoom (仅在有空间时入队)
    std::cout << "\n3. enqueueIfHaveRoom() - Only enqueue if space available\n";
    MpmcBlockingQueue<int> queue2(2);
    queue2.enqueueIfHaveRoom(10);
    queue2.enqueueIfHaveRoom(20);
    queue2.enqueueIfHaveRoom(30);  // 会被丢弃
    std::cout << "   Queue size: " << queue2.size() << " (max 2)\n";
    std::cout << "   Discard counter: " << queue2.discardCounter() << "\n";

    std::cout << "\n=== Example completed successfully ===\n";

    return 0;
}
