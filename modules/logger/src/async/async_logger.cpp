#include <logger/async/async_logger.h>

#include <cstdio>
#include <exception>
#include <stdexcept>

NOVA_NAMESPACE_BEGIN
namespace log::async {

AsyncOverflowPolicy AsyncLogger::validateOverflowPolicy(AsyncOverflowPolicy policy)
{
    switch (policy) {
        case AsyncOverflowPolicy::BLOCK:
        case AsyncOverflowPolicy::OVER_RUN_OLDEST:
        case AsyncOverflowPolicy::DISCARD_NEW:
            return policy;
    }
    NOVA_THROW(std::invalid_argument("Invalid async overflow policy"));
    return AsyncOverflowPolicy::BLOCK;
}

// async logger
AsyncLogger::AsyncLogger(std::string name, sinks::SinkInitList sinksList,
                         std::weak_ptr<AsyncLogThreadPool> worker, AsyncOverflowPolicy overflowPolicy)
    : AsyncLogger(std::move(name), sinksList.begin(), sinksList.end(), std::move(worker), overflowPolicy)
{
}

AsyncLogger::AsyncLogger(std::string name, sinks::SinkPtr singleSink,
                         std::weak_ptr<AsyncLogThreadPool> worker, AsyncOverflowPolicy overflowPolicy)
    : AsyncLogger(std::move(name), {singleSink}, std::move(worker), overflowPolicy)
{
}

AsyncLogger::AsyncLogger(const AsyncLogger &other)
    : std::enable_shared_from_this<AsyncLogger>(), Logger(other), worker_(other.worker_),
      overflowPolicy_(other.overflowPolicy_)
{
    validateThreadSafeSinks();
}

AsyncLogger::~AsyncLogger() noexcept
{
    backendFlushNoexcept();
}

std::shared_ptr<Logger> AsyncLogger::clone(std::string name)
{
    auto cloned = std::make_shared<AsyncLogger>(*this);
    cloned->name_ = std::move(name);
    return cloned;
}

void AsyncLogger::sinkIt(const details::LogMsg &msg)
{
    NOVA_TRY
    {
        auto self = weak_from_this().lock();
        if (!self) {
            errHandler("AsyncLogger must be managed by std::shared_ptr");
            return;
        }
        if (auto worker = worker_.lock()) {
            worker->postLog(std::move(self), msg, overflowPolicy_);
            return;
        }
        errHandler("async log: worker thread doesn't exist anymore");
    }
    NOVA_LOG_DETAILS_CATCH(msg.source)
}

void AsyncLogger::_flush()
{
    NOVA_TRY
    {
        auto self = weak_from_this().lock();
        if (!self) {
            errHandler("AsyncLogger must be managed by std::shared_ptr");
            return;
        }
        if (auto worker = worker_.lock()) {
            worker->postFlush(std::move(self));
            return;
        }
        errHandler("async flush: worker thread doesn't exist anymore");
    }
    NOVA_LOG_DETAILS_CATCH(SourceLocation())
}

void AsyncLogger::backendSinkIt(const details::LogMsg &incomingLogMsg)
{
    for (auto &sink : sinks_) {
        if (!sink) {
            errHandler("Logger sink is null");
            continue;
        }
        if (sink->shouldLog(incomingLogMsg.level)) {
            NOVA_TRY
            {
                sink->log(incomingLogMsg);
            }
            NOVA_LOG_DETAILS_CATCH(incomingLogMsg.source)
        }
    }

}

void AsyncLogger::validateThreadSafeSinks() const
{
    const auto worker = worker_.lock();
    if (!worker || worker->threadCount() <= 1) {
        return;
    }

    const auto workerCount = worker->threadCount();
    for (std::size_t sinkIndex = 0; sinkIndex < sinks_.size(); ++sinkIndex) {
        const auto &sink = sinks_[sinkIndex];
        if (sink && !sink->isThreadSafe()) {
            NOVA_THROW(std::invalid_argument(
                "AsyncLogger '" + name_ + "' cannot use sink[" + std::to_string(sinkIndex) + "] with " +
                std::to_string(workerCount) + " workers: use a thread-safe sink or configure one worker"));
        }
    }
}

void AsyncLogger::backendFlush()
{
    for (auto &sink : sinks_) {
        if (!sink) {
            errHandler("Logger sink is null");
            continue;
        }
        NOVA_TRY
        {
            sink->flush();
        }
        NOVA_LOG_DETAILS_CATCH(SourceLocation())
    }
}

void AsyncLogger::backendFlushNoexcept() noexcept
{
    NOVA_TRY
    {
        backendFlush();
    }
    NOVA_CATCH_STD_BODY({
        std::fprintf(stderr, "[*** ASYNC LOG ERROR ***] final flush failed: %s\n", ex.what());
    })
    NOVA_CATCH_ALL_BODY({
        std::fputs("[*** ASYNC LOG ERROR ***] final flush failed with an unknown exception\n", stderr);
    })
}

}  // namespace log::async
NOVA_NAMESPACE_END
