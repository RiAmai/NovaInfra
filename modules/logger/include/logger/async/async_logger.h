#pragma once

#include <logger/async/async_log_thread_pool.h>
#include <logger/async/types.h>
#include <logger/logger.h>

#include <memory>
#include <atomic>

NOVAINFRA_NAMESPACE_BEGIN
namespace log::async {

// AsyncLogger must be managed by std::shared_ptr so queued messages can keep it alive.
// Explicit flush requests are reliably enqueued but do not wait for the backend flush to finish.
class NOVAINFRA_LIB_PORT AsyncLogger NOVAINFRA_FINAL : public std::enable_shared_from_this<AsyncLogger>,
                                                       public Logger
{
    friend class AsyncLogThreadPool;

public:
    template <typename It>
    AsyncLogger(std::string name, It begin, It end, std::weak_ptr<AsyncLogThreadPool> worker,
                AsyncOverflowPolicy overflowPolicy = AsyncOverflowPolicy::BLOCK)
        : Logger(std::move(name), begin, end), worker_(std::move(worker)),
          overflowPolicy_(validateOverflowPolicy(overflowPolicy))
    {
        validateThreadSafeSinks();
    }

    AsyncLogger(std::string name, sinks::SinkInitList sinksList, std::weak_ptr<AsyncLogThreadPool> worker,
                AsyncOverflowPolicy overflowPolicy = AsyncOverflowPolicy::BLOCK);

    AsyncLogger(std::string name, sinks::SinkPtr singleSink, std::weak_ptr<AsyncLogThreadPool> worker,
                AsyncOverflowPolicy overflowPolicy = AsyncOverflowPolicy::BLOCK);
    AsyncLogger(const AsyncLogger &other);
    ~AsyncLogger() NOVAINFRA_NOEXCEPT NOVAINFRA_OVERRIDE;

    std::shared_ptr<Logger> clone(std::string name) NOVAINFRA_OVERRIDE;

protected:
    void sinkIt(const details::LogMsg &msg) NOVAINFRA_OVERRIDE;
    void _flush() NOVAINFRA_OVERRIDE;
    void backendSinkIt(const details::LogMsg &incomingLogMsg);
    void backendFlush();
    void backendFlushNoexcept() NOVAINFRA_NOEXCEPT;

private:
    static AsyncOverflowPolicy validateOverflowPolicy(AsyncOverflowPolicy policy);
    void validateThreadSafeSinks() const;

    std::weak_ptr<AsyncLogThreadPool> worker_;
    AsyncOverflowPolicy overflowPolicy_;
    std::atomic<bool> workerRegistered_{false};
};
}  // namespace log::async
NOVAINFRA_NAMESPACE_END
