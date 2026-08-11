#include <logger/async/async_log_thread_pool.h>
#include <logger/async/async_logger.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

NOVA_NAMESPACE_BEGIN
namespace log::async {

thread_local AsyncLogThreadPool::WorkerContext *AsyncLogThreadPool::currentWorkerContext_ = nullptr;

AsyncLogThreadPool::AsyncLogThreadPool(std::size_t queueMaxItems, std::size_t threadMaxNum,
                                       Callback onStartFunc, Callback onStopFunc)
    : queue_(queueMaxItems)
{
    if (threadMaxNum == 0 || threadMaxNum > kThreadMaxNum) {
        NOVA_THROW(std::invalid_argument("AsyncLogThreadPool thread count must be between 1 and 1000"));
    }

    workerPool_.reserve(threadMaxNum);
    NOVA_TRY
    {
        for (std::size_t index = 0; index < threadMaxNum; ++index) {
            workerPool_.emplace_back([this, onStartFunc, onStopFunc] {
                invokeCallback(onStartFunc, "start");
                workerLoop();
                invokeCallback(onStopFunc, "stop");
            });
        }
    }
    NOVA_CATCH_ALL_BODY({
        for (std::size_t index = workerPool_.size(); index > 0; --index) {
            queue_.enqueue(details::AsyncLogMsg(details::AsyncMsgType::TERMINATE));
        }
        for (auto &worker : workerPool_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        NOVA_RETHROW_CURRENT();
    })
}

AsyncLogThreadPool::AsyncLogThreadPool(std::size_t queueMaxItems, std::size_t threadMaxNum,
                                       Callback onStartFunc)
    : AsyncLogThreadPool(queueMaxItems, threadMaxNum, std::move(onStartFunc), Callback{})
{
}

AsyncLogThreadPool::AsyncLogThreadPool(std::size_t queueMaxItems, std::size_t threadMaxNum)
    : AsyncLogThreadPool(queueMaxItems, threadMaxNum, Callback{}, Callback{})
{
}

AsyncLogThreadPool::~AsyncLogThreadPool() noexcept
{
    if (currentWorkerContext_ != nullptr && currentWorkerContext_->pool == this) {
        std::fputs("[*** ASYNC LOG ERROR ***] AsyncLogThreadPool cannot be destroyed by its worker thread\n",
                   stderr);
        std::terminate();
    }

    for (std::size_t index = 0; index < workerPool_.size(); ++index) {
        queue_.enqueue(details::AsyncLogMsg(details::AsyncMsgType::TERMINATE));
    }
    for (auto &worker : workerPool_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    flushRegisteredLoggers();
}

std::size_t AsyncLogThreadPool::overrunCounter()
{
    return queue_.overrunCounter();
}

void AsyncLogThreadPool::resetOverrunCounter()
{
    queue_.resetOverrunCounter();
}

std::size_t AsyncLogThreadPool::discardCounter() const
{
    return queue_.discardCounter();
}

void AsyncLogThreadPool::resetDiscardCounter()
{
    queue_.resetDiscardCounter();
}

std::size_t AsyncLogThreadPool::queueSize()
{
    return queue_.size();
}

void AsyncLogThreadPool::postLog(std::shared_ptr<AsyncLogger> &&logger, const details::LogMsg &msg,
                                 AsyncOverflowPolicy policy)
{
    postAsyncMsg(details::AsyncLogMsg(std::move(logger), details::AsyncMsgType::LOG, msg), policy);
}

void AsyncLogThreadPool::postFlush(std::shared_ptr<AsyncLogger> &&logger)
{
    auto *context = currentWorkerContext_;
    if (context != nullptr && context->pool == this) {
        if (context->isFlushing || !logger) {
            return;
        }
        const auto alreadyPending = std::find(context->pendingFlushLoggers.begin(),
                                              context->pendingFlushLoggers.end(), logger) !=
                                    context->pendingFlushLoggers.end();
        if (!alreadyPending) {
            context->pendingFlushLoggers.push_back(std::move(logger));
        }
        return;
    }

    // Flush controls always use BLOCK. They must not be dropped by an overflow
    // policy selected for ordinary log messages.
    postAsyncMsg(details::AsyncLogMsg(std::move(logger), details::AsyncMsgType::FLUSH),
                 AsyncOverflowPolicy::BLOCK);
}

void AsyncLogThreadPool::postAsyncMsg(details::AsyncLogMsg &&newMsg, AsyncOverflowPolicy policy)
{
    switch (policy) {
        case AsyncOverflowPolicy::BLOCK:
            queue_.enqueue(std::move(newMsg));
            return;
        case AsyncOverflowPolicy::OVER_RUN_OLDEST:
            if (!queue_.enqueueNoWait(std::move(newMsg), [](const details::AsyncLogMsg &oldest) {
                    return oldest.msgType == details::AsyncMsgType::LOG;
                })) {
                // A control message is at the head. Preserve it and wait for
                // room rather than violating reliable flush/shutdown ordering.
                queue_.enqueue(std::move(newMsg));
            }
            return;
        case AsyncOverflowPolicy::DISCARD_NEW:
            queue_.enqueueIfHaveRoom(std::move(newMsg));
            return;
    }
    NOVA_THROW(std::invalid_argument("Invalid async overflow policy"));
}

void AsyncLogThreadPool::workerLoop() noexcept
{
    WorkerContext context;
    context.pool = this;
    currentWorkerContext_ = &context;

    while (true) {
        bool keepRunning = true;
        NOVA_TRY
        {
            keepRunning = processNextMsg(context);
        }
        NOVA_CATCH_STD_BODY({
            reportWorkerFailure(ex.what());
        })
        NOVA_CATCH_ALL_BODY({
            reportWorkerFailure("unknown exception escaped async logger backend");
        })
        flushPendingLoggers(context);
        if (!keepRunning) {
            break;
        }
    }

    currentWorkerContext_ = nullptr;
}

bool AsyncLogThreadPool::processNextMsg(WorkerContext &context)
{
    details::AsyncLogMsg incomingAsyncMsg;
    queue_.dequeue(incomingAsyncMsg);

    switch (incomingAsyncMsg.msgType) {
        case details::AsyncMsgType::LOG: {
            if (!incomingAsyncMsg.loggerPtr) {
                reportWorkerFailure("async log message has no logger");
                return true;
            }
            rememberLogger(incomingAsyncMsg.loggerPtr);
            incomingAsyncMsg.loggerPtr->backendSinkIt(incomingAsyncMsg);
            if (incomingAsyncMsg.loggerPtr->shouldFlush(incomingAsyncMsg.level)) {
                flushLogger(context, incomingAsyncMsg.loggerPtr);
            }
            return true;
        }

        case details::AsyncMsgType::FLUSH: {
            if (!incomingAsyncMsg.loggerPtr) {
                reportWorkerFailure("async flush message has no logger");
                return true;
            }
            rememberLogger(incomingAsyncMsg.loggerPtr);
            flushLogger(context, incomingAsyncMsg.loggerPtr);
            return true;
        }

        case details::AsyncMsgType::TERMINATE:
            return false;

        default:
            assert(false);
            return true;
    }
}

void AsyncLogThreadPool::flushPendingLoggers(WorkerContext &context) noexcept
{
    if (context.pendingFlushLoggers.empty() || context.isFlushing) {
        return;
    }

    auto pending = std::move(context.pendingFlushLoggers);
    context.pendingFlushLoggers.clear();
    for (const auto &logger : pending) {
        flushLogger(context, logger);
    }
}

void AsyncLogThreadPool::flushLogger(WorkerContext &context,
                                     const std::shared_ptr<AsyncLogger> &logger) noexcept
{
    if (!logger || context.isFlushing) {
        return;
    }

    context.isFlushing = true;
    NOVA_TRY
    {
        logger->backendFlush();
    }
    NOVA_CATCH_STD_BODY({
        reportWorkerFailure(ex.what());
    })
    NOVA_CATCH_ALL_BODY({
        reportWorkerFailure("unknown exception while flushing async logger");
    })
    context.isFlushing = false;
}

void AsyncLogThreadPool::rememberLogger(const std::shared_ptr<AsyncLogger> &logger)
{
    if (logger->workerRegistered_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    NOVA_TRY
    {
        std::lock_guard<std::mutex> lock(registryMutex_);
        registeredLoggers_.insert_or_assign(logger.get(), logger);
        if (--registrationsUntilPrune_ == 0) {
            pruneExpiredLoggersLocked();
            registrationsUntilPrune_ = kRegistryPruneInterval;
        }
    }
    NOVA_CATCH_ALL_BODY({
        logger->workerRegistered_.store(false, std::memory_order_release);
        NOVA_RETHROW_CURRENT();
    })
}

void AsyncLogThreadPool::pruneExpiredLoggersLocked()
{
    for (auto it = registeredLoggers_.begin(); it != registeredLoggers_.end();) {
        if (it->second.expired()) {
            it = registeredLoggers_.erase(it);
        } else {
            ++it;
        }
    }
}

void AsyncLogThreadPool::flushRegisteredLoggers() noexcept
{
    std::vector<std::shared_ptr<AsyncLogger>> liveLoggers;
    {
        std::lock_guard<std::mutex> lock(registryMutex_);
        liveLoggers.reserve(registeredLoggers_.size());
        for (auto &entry : registeredLoggers_) {
            if (auto logger = entry.second.lock()) {
                liveLoggers.push_back(std::move(logger));
            }
        }
        registeredLoggers_.clear();
    }

    WorkerContext context;
    context.pool = this;
    for (const auto &logger : liveLoggers) {
        flushLogger(context, logger);
    }
}

void AsyncLogThreadPool::invokeCallback(const Callback &callback, const char *phase) noexcept
{
    if (!callback) {
        return;
    }
    NOVA_TRY
    {
        callback();
    }
    NOVA_CATCH_STD_BODY({
        char message[256];
        std::snprintf(message, sizeof(message), "async worker %s callback failed: %s", phase, ex.what());
        reportWorkerFailure(message);
    })
    NOVA_CATCH_ALL_BODY({
        reportWorkerFailure("async worker callback failed with an unknown exception");
    })
}

void AsyncLogThreadPool::reportWorkerFailure(const char *message) noexcept
{
    std::fprintf(stderr, "[*** ASYNC LOG ERROR ***] %s\n", message);
}

}  // namespace log::async
NOVA_NAMESPACE_END
