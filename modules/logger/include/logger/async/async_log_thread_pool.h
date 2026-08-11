#pragma once

#include <logger/async/types.h>
#include <logger/details/async_log_msg.h>
#include <logger/registry.h>

#include <core/export.h>
#include <core/mpmc_blocking_queue.h>
#include <core/namespace.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

NOVA_NAMESPACE_BEGIN
namespace log::async {

class AsyncLogger;

class NOVA_LIB_PORT AsyncLogThreadPool
{
public:
    using Callback = std::function<void()>;

    AsyncLogThreadPool(std::size_t queueMaxItems, std::size_t threadMaxNum, Callback onStartFunc,
                       Callback onStopFunc);
    AsyncLogThreadPool(std::size_t queueMaxItems, std::size_t threadMaxNum, Callback onStartFunc);
    AsyncLogThreadPool(std::size_t queueMaxItems, std::size_t threadMaxNum);

    ~AsyncLogThreadPool() noexcept;

    NOVA_DISABLE_COPY_MOVE(AsyncLogThreadPool)

    std::size_t overrunCounter();
    void resetOverrunCounter();
    std::size_t discardCounter() const;
    void resetDiscardCounter();
    std::size_t queueSize();
    std::size_t threadCount() const noexcept { return workerPool_.size(); }

private:
    friend class AsyncLogger;

    using QueueType = MpmcBlockingQueue<details::AsyncLogMsg>;

    struct WorkerContext {
        AsyncLogThreadPool *pool = nullptr;
        bool isFlushing = false;
        std::vector<std::shared_ptr<AsyncLogger>> pendingFlushLoggers;
    };

    void postLog(std::shared_ptr<AsyncLogger> &&logger, const details::LogMsg &msg,
                 AsyncOverflowPolicy policy);
    void postFlush(std::shared_ptr<AsyncLogger> &&logger);
    void postAsyncMsg(details::AsyncLogMsg &&newMsg, AsyncOverflowPolicy policy);
    void workerLoop() noexcept;
    bool processNextMsg(WorkerContext &context);
    void flushPendingLoggers(WorkerContext &context) noexcept;
    void flushLogger(WorkerContext &context, const std::shared_ptr<AsyncLogger> &logger) noexcept;
    void rememberLogger(const std::shared_ptr<AsyncLogger> &logger);
    void pruneExpiredLoggersLocked();
    void flushRegisteredLoggers() noexcept;
    static void invokeCallback(const Callback &callback, const char *phase) noexcept;
    static void reportWorkerFailure(const char *message) noexcept;

    QueueType queue_;
    std::vector<std::thread> workerPool_;

    static constexpr std::size_t kRegistryPruneInterval = 256;
    static constexpr std::size_t kThreadMaxNum = 1000;

    mutable std::mutex registryMutex_;
    std::unordered_map<AsyncLogger *, std::weak_ptr<AsyncLogger>> registeredLoggers_;
    std::size_t registrationsUntilPrune_{kRegistryPruneInterval};

    static thread_local WorkerContext *currentWorkerContext_;
};


inline void initAsyncLogThreadPool(std::size_t queueMaxItems, std::size_t threadMaxNum,
                                   AsyncLogThreadPool::Callback onStartFunc,
                                   AsyncLogThreadPool::Callback onStopFunc)
{
    auto threadPool =
        std::make_shared<AsyncLogThreadPool>(queueMaxItems, threadMaxNum, onStartFunc, onStopFunc);
    Registry::instance().setWorker(threadPool);
}

inline void initAsyncLogThreadPool(std::size_t queueMaxItems, std::size_t threadMaxNum,
                                   AsyncLogThreadPool::Callback onStartFunc)
{
    initAsyncLogThreadPool(queueMaxItems, threadMaxNum, onStartFunc, []() {});
}

inline void initAsyncLogThreadPool(std::size_t queueMaxItems, std::size_t threadMaxNum)
{
    initAsyncLogThreadPool(queueMaxItems, threadMaxNum, []() {}, []() {});
}

inline std::shared_ptr<AsyncLogThreadPool> asyncLogThreadPool()
{
    return Registry::instance().worker();
}

}  // namespace log::async
NOVA_NAMESPACE_END
