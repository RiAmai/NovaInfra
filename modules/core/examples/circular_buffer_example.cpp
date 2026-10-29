#include <core/circular_queue.h>

#include <iostream>
#include <string>

using namespace novainfra;

// CircularQueue 作为固定大小缓冲区的示例
int main()
{
    std::cout << "=== CircularQueue Example ===\n\n";

    // 示例 1: 基本使用 - 固定容量的环形缓冲区
    std::cout << "1. Basic Usage - Fixed Capacity Ring Buffer\n";
    CircularQueue<int> buffer(5);

    for (int i = 1; i <= 5; ++i) {
        buffer.pushBack(std::move(i));
        std::cout << "   Pushed " << i << ", size: " << buffer.size()
                  << ", full: " << (buffer.full() ? "yes" : "no") << "\n";
    }

    std::cout << "   Buffer contents: ";
    for (size_t i = 0; i < buffer.size(); ++i) {
        std::cout << buffer.at(i) << " ";
    }
    std::cout << "\n\n";

    // 示例 2: 覆盖行为 - 当满时自动覆盖最旧数据
    std::cout << "2. Overrun Behavior - Automatic Overwrite When Full\n";
    CircularQueue<std::string> logBuffer(3);

    logBuffer.pushBack("Log entry 1");
    logBuffer.pushBack("Log entry 2");
    logBuffer.pushBack("Log entry 3");
    std::cout << "   Buffer full, size: " << logBuffer.size() << "\n";

    // 继续添加会覆盖最旧的条目
    logBuffer.pushBack("Log entry 4");  // 覆盖 "Log entry 1"
    std::cout << "   Pushed 'Log entry 4' (overwrites oldest)\n";
    std::cout << "   Overrun counter: " << logBuffer.overrunCounter() << "\n";
    std::cout << "   Front entry: " << logBuffer.front() << "\n";

    logBuffer.pushBack("Log entry 5");  // 覆盖 "Log entry 2"
    std::cout << "   Pushed 'Log entry 5'\n";
    std::cout << "   Overrun counter: " << logBuffer.overrunCounter() << "\n";

    std::cout << "   Current buffer contents:\n";
    for (size_t i = 0; i < logBuffer.size(); ++i) {
        std::cout << "     [" << i << "] " << logBuffer.at(i) << "\n";
    }
    std::cout << "\n";

    // 示例 3: 使用场景 - 最近 N 个事件的滚动窗口
    std::cout << "3. Use Case - Rolling Window of Recent Events\n";
    CircularQueue<double> temperatureReadings(10);

    // 模拟温度传感器读数
    double temps[] = {22.5, 23.1, 22.8, 24.0, 23.5, 22.9, 23.3, 22.7, 23.8, 24.2, 23.6, 24.1};

    std::cout << "   Simulating temperature sensor readings:\n";
    for (double temp : temps) {
        temperatureReadings.pushBack(std::move(temp));
        std::cout << "   Reading: " << temp << "°C";

        if (temperatureReadings.size() >= 10) {
            // 计算最近 10 个读数的平均值
            double sum = 0;
            for (size_t i = 0; i < temperatureReadings.size(); ++i) {
                sum += temperatureReadings.at(i);
            }
            double avg = sum / temperatureReadings.size();
            std::cout << " | Rolling avg (last 10): " << avg << "°C";
        }
        std::cout << "\n";
    }

    std::cout << "\n";

    // 示例 4: Pop 操作
    std::cout << "4. Pop Operations\n";
    CircularQueue<int> queue(5);
    queue.pushBack(10);
    queue.pushBack(20);
    queue.pushBack(30);

    std::cout << "   Initial: front = " << queue.front() << ", size = " << queue.size() << "\n";

    queue.popFront();
    std::cout << "   After pop: front = " << queue.front() << ", size = " << queue.size() << "\n";

    queue.popFront();
    std::cout << "   After pop: front = " << queue.front() << ", size = " << queue.size() << "\n";

    queue.popFront();
    std::cout << "   After pop: empty = " << (queue.empty() ? "yes" : "no") << "\n\n";

    // 示例 5: 移动语义
    std::cout << "5. Move Semantics\n";
    CircularQueue<int> source(5);
    source.pushBack(100);
    source.pushBack(200);
    source.pushBack(300);

    std::cout << "   Source queue size before move: " << source.size() << "\n";

    CircularQueue<int> destination(std::move(source));
    std::cout << "   Destination queue size after move: " << destination.size() << "\n";
    std::cout << "   Source queue size after move: " << source.size() << " (should be 0)\n";
    std::cout << "   Destination front: " << destination.front() << "\n\n";

    // 示例 6: 计数器管理
    std::cout << "6. Counter Management\n";
    CircularQueue<int> counterQueue(3);
    counterQueue.pushBack(1);
    counterQueue.pushBack(2);
    counterQueue.pushBack(3);
    counterQueue.pushBack(4);  // Overrun
    counterQueue.pushBack(5);  // Overrun

    std::cout << "   Overrun counter: " << counterQueue.overrunCounter() << "\n";
    counterQueue.resetOverrunCounter();
    std::cout << "   After reset: " << counterQueue.overrunCounter() << "\n\n";

    std::cout << "=== Example completed successfully ===\n";

    return 0;
}
