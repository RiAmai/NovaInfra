#pragma once

#include <logger/async/async_log_thread_pool.h>
#include <logger/async/types.h>
#include <logger/logger.h>

#include <memory>
#include <atomic>

NOVA_NAMESPACE_BEGIN
namespace log::async {

// AsyncLogger must be managed by std::shared_ptr so queued messages can keep it alive.
// Explicit flush requests are reliably enqueued but do not wait for the backend flush to finish.
class NOVA_LIB_PORT AsyncLogger final : public std::enable_shared_from_this<AsyncLogger>, public Logger
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
    ~AsyncLogger() noexcept override;

    std::shared_ptr<Logger> clone(std::string name) override;

protected:
    void sinkIt(const details::LogMsg &msg) override;
    void _flush() override;
    void backendSinkIt(const details::LogMsg &incomingLogMsg);
    void backendFlush();
    void backendFlushNoexcept() noexcept;

private:
    static AsyncOverflowPolicy validateOverflowPolicy(AsyncOverflowPolicy policy);
    void validateThreadSafeSinks() const;

    std::weak_ptr<AsyncLogThreadPool> worker_;
    AsyncOverflowPolicy overflowPolicy_;
    std::atomic<bool> workerRegistered_{false};
};
}  // namespace log::async
NOVA_NAMESPACE_END
