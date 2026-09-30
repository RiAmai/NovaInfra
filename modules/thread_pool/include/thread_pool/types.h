#pragma once

#include <core/namespace.h>

#include <cstddef>
#include <exception>
#include <functional>

NOVAINFRA_NAMESPACE_BEGIN
namespace threadpool {

/** @brief 线程池创建和错误处理选项。 */
struct ThreadPoolOptions {
    /** @brief worker 线程数量；为 0 时使用硬件并发数，且至少创建一个线程。 */
    std::size_t workerCount = 0;
    /** @brief 等待队列容量，不包含正在执行的任务。 */
    std::size_t queueCapacity = 1024;

    /**
     * @brief 处理 fire-and-forget 任务异常的回调。
     * @param exception 任务捕获到的异常；回调在线程池 worker 线程中执行。
     */
    using ExceptionHandler = std::function<void(std::exception_ptr)>;
    ExceptionHandler onUnhandledException;

    /**
     * @brief 处理 worker 内部故障的回调。
     * @param exception 内部故障对应的异常；回调在线程池 worker 线程中执行。
     */
    using WorkerFailureHandler = std::function<void(std::exception_ptr)>;
    WorkerFailureHandler onWorkerFailure;
};

/** @brief 线程池运行状态的快照。 */
struct ThreadPoolStats {
    /** @brief 配置的 worker 线程数量。 */
    std::size_t workerCount = 0;
    /** @brief 当前等待执行的任务数量。 */
    std::size_t queuedTasks = 0;
    /** @brief 当前正在执行的任务数量。 */
    std::size_t activeTasks = 0;
    /** @brief 是否仍接受新任务。 */
    bool accepting = false;
    /** @brief 是否没有记录 worker 内部故障。 */
    bool healthy = true;
};

}  // namespace threadpool
NOVAINFRA_NAMESPACE_END
