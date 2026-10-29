#include <core/mpmc_blocking_queue.h>
#include <logger/async/async_log_thread_pool.h>
#include <logger/async/async_logger.h>
#include <logger/log.h>
#include <logger/sinks/base_sink.h>
#include <logger/sinks/sink.h>

#include <gtest/gtest.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using namespace std::chrono_literals;

struct ObservedMessage {
    std::string message;
    std::string file;
    std::string function;
    int line = 0;
    bool staticSource = false;
};

class RecordingSink NOVAINFRA_FINAL : public novainfra::log::sinks::ISink
{
public:
    void log(const novainfra::log::LogMsg &msg) NOVAINFRA_OVERRIDE
    {
        std::lock_guard<std::mutex> lock(mutex_);
        messages_.push_back({std::string(msg.message), std::string(msg.source.file),
                             std::string(msg.source.function), msg.source.line,
                             msg.source.hasStaticStorage()});
    }

    void setPattern(const std::string &) NOVAINFRA_OVERRIDE {}
    void setFormatter(std::unique_ptr<novainfra::log::formatters::IFormatter>) NOVAINFRA_OVERRIDE {}

    void flush() NOVAINFRA_OVERRIDE
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++flushCount_;
    }

    std::vector<ObservedMessage> messages() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_;
    }

    int flushCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return flushCount_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<ObservedMessage> messages_;
    int flushCount_ = 0;
};

class BlockingRecordingSink NOVAINFRA_FINAL : public novainfra::log::sinks::ISink
{
public:
    void log(const novainfra::log::LogMsg &msg) NOVAINFRA_OVERRIDE
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!blockedOnce_) {
            blockedOnce_ = true;
            workerBlocked_ = true;
            stateChanged_.notify_all();
            stateChanged_.wait(lock, [this] { return released_; });
        }
        if (releaseCallback_) {
            releaseCallback_();
            releaseCallback_ = {};
        }
        messages_.push_back({std::string(msg.message), std::string(msg.source.file),
                             std::string(msg.source.function), msg.source.line,
                             msg.source.hasStaticStorage()});
    }

    void setPattern(const std::string &) NOVAINFRA_OVERRIDE {}
    void setFormatter(std::unique_ptr<novainfra::log::formatters::IFormatter>) NOVAINFRA_OVERRIDE {}

    void flush() NOVAINFRA_OVERRIDE
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++flushCount_;
        stateChanged_.notify_all();
    }

    bool waitUntilBlocked(std::chrono::milliseconds timeout = 2s)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return stateChanged_.wait_for(lock, timeout, [this] { return workerBlocked_; });
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        stateChanged_.notify_all();
    }

    void setReleaseCallback(std::function<void()> callback)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        releaseCallback_ = std::move(callback);
    }

    std::vector<ObservedMessage> messages() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_;
    }

    int flushCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return flushCount_;
    }

    bool waitForFlushCount(int expected, std::chrono::milliseconds timeout = 2s)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return stateChanged_.wait_for(lock, timeout, [this, expected] { return flushCount_ >= expected; });
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable stateChanged_;
    bool blockedOnce_ = false;
    bool workerBlocked_ = false;
    bool released_ = false;
    std::function<void()> releaseCallback_;
    std::vector<ObservedMessage> messages_;
    int flushCount_ = 0;
};

class ThrowOnceSink NOVAINFRA_FINAL : public novainfra::log::sinks::ISink
{
public:
    enum class ExceptionType { STANDARD, UNKNOWN };

    explicit ThrowOnceSink(ExceptionType exceptionType) : exceptionType_(exceptionType) {}

    void log(const novainfra::log::LogMsg &) NOVAINFRA_OVERRIDE
    {
        if (calls_.fetch_add(1) == 0) {
            if (exceptionType_ == ExceptionType::STANDARD) {
                throw std::runtime_error("async sink failure");
            }
            throw 42;
        }
        ++successes_;
    }

    void setPattern(const std::string &) NOVAINFRA_OVERRIDE {}
    void setFormatter(std::unique_ptr<novainfra::log::formatters::IFormatter>) NOVAINFRA_OVERRIDE {}
    void flush() NOVAINFRA_OVERRIDE {}

    int successes() const { return successes_.load(); }

private:
    ExceptionType exceptionType_;
    std::atomic<int> calls_{0};
    std::atomic<int> successes_{0};
};

class ParallelRecordingSink NOVAINFRA_FINAL : public novainfra::log::sinks::ISink
{
public:
    explicit ParallelRecordingSink(int target) : target_(target) {}

    bool isThreadSafe() const NOVAINFRA_NOEXCEPT NOVAINFRA_OVERRIDE { return true; }

    void log(const novainfra::log::LogMsg &) NOVAINFRA_OVERRIDE
    {
        std::unique_lock<std::mutex> lock(mutex_);
        ++active_;
        peakActive_ = std::max(peakActive_, active_);
        changed_.notify_all();
        changed_.wait(lock, [this] { return released_; });
        --active_;
        ++messages_;
    }

    void setPattern(const std::string &) NOVAINFRA_OVERRIDE {}
    void setFormatter(std::unique_ptr<novainfra::log::formatters::IFormatter>) NOVAINFRA_OVERRIDE {}
    void flush() NOVAINFRA_OVERRIDE {}

    bool waitUntilParallel(std::chrono::milliseconds timeout = 2s)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, timeout, [this] { return active_ >= target_; });
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        changed_.notify_all();
    }

    int peakActive() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return peakActive_;
    }

    int messages() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_;
    }

private:
    const int target_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    int active_{0};
    int peakActive_{0};
    int messages_{0};
    bool released_{false};
};

static_assert(std::is_move_constructible<novainfra::log::detail::AsyncLogMsg>::value,
              "AsyncLogMsg must be movable");
static_assert(std::is_move_assignable<novainfra::log::detail::AsyncLogMsg>::value,
              "AsyncLogMsg must be move assignable");
static_assert(!std::is_copy_constructible<novainfra::log::detail::AsyncLogMsg>::value,
              "AsyncLogMsg must not be copied");
static_assert(!std::is_copy_assignable<novainfra::log::detail::AsyncLogMsg>::value,
              "AsyncLogMsg must not be copied");
static_assert(sizeof(novainfra::log::detail::AsyncLogMsg) <= 432,
              "AsyncLogMsg queue payload must stay compact");

template <typename Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout = 2s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

TEST(AsyncLogThreadPool, RejectsInvalidQueueCapacity)
{
    EXPECT_THROW(novainfra::log::async::AsyncLogThreadPool(0, 1), std::invalid_argument);
    EXPECT_THROW(novainfra::log::async::AsyncLogThreadPool(1, 0), std::invalid_argument);
    EXPECT_THROW(novainfra::log::async::AsyncLogThreadPool(1, 1001), std::invalid_argument);
}

TEST(AsyncLogger, RejectsUnsafeSinkForMultipleWorkers)
{
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(8, 2);
    auto sink = std::make_shared<RecordingSink>();

    try {
        (void)std::make_shared<novainfra::log::async::AsyncLogger>("unsafe", sink, worker);
        FAIL() << "expected construction to reject an unsafe sink";
    } catch (const std::invalid_argument &error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("unsafe"), std::string::npos);
        EXPECT_NE(message.find("sink[0]"), std::string::npos);
        EXPECT_NE(message.find("2 workers"), std::string::npos);
        EXPECT_NE(message.find("one worker"), std::string::npos);
    }
}

TEST(AsyncLogger, AllowsUnsafeSinkWithSingleWorker)
{
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(8, 1);
    auto sink = std::make_shared<RecordingSink>();

    EXPECT_NO_THROW({
        auto logger = std::make_shared<novainfra::log::async::AsyncLogger>("single", sink, worker);
        logger->info("single worker");
        worker.reset();
    });
}

TEST(AsyncLogger, MultipleWorkersCanProcessOneLoggerConcurrently)
{
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(32, 4);
    auto sink = std::make_shared<ParallelRecordingSink>(4);
    auto logger = std::make_shared<novainfra::log::async::AsyncLogger>("parallel", sink, worker);

    for (int index = 0; index < 4; ++index) {
        logger->info("message {}", index);
    }

    ASSERT_TRUE(sink->waitUntilParallel());
    EXPECT_GE(sink->peakActive(), 2);
    sink->release();
    worker.reset();
    EXPECT_EQ(sink->messages(), 4);
}

TEST(MpmcBlockingQueue, ReleasesOverrunItemsImmediately)
{
    novainfra::MpmcBlockingQueue<std::shared_ptr<int>> queue(1);
    auto oldest = std::make_shared<int>(1);
    std::weak_ptr<int> oldestWeak = oldest;
    queue.enqueueNoWait(std::move(oldest));

    auto newest = std::make_shared<int>(2);
    queue.enqueueNoWait(std::move(newest));

    EXPECT_TRUE(oldestWeak.expired());
    EXPECT_EQ(queue.overrunCounter(), 1U);
}

TEST(AsyncLogMsg, MoveOperationsRebindOwnedDynamicFields)
{
    std::string message("body\0tail", 9);
    std::string file("dynamic/fi\0le.cpp", 17);
    std::string function("dynamic\0function", 16);
    const auto expectedMessage = message;
    const auto expectedFile = file;
    const auto expectedFunction = function;
    novainfra::log::LogMsg logMessage{
        novainfra::SystemTimePoint{},
        novainfra::log::Level::INFO,
        7,
        novainfra::log::SourceLocation{file, 73, function},
        "logger",
        message
    };

    novainfra::log::detail::AsyncLogMsg first(std::shared_ptr<novainfra::log::async::AsyncLogger>{},
                                               novainfra::log::detail::AsyncMsgType::LOG,
                                               std::move(logMessage));
    message.assign(2048, 'm');
    file.assign(2048, 'f');
    function.assign(2048, 'x');

    novainfra::log::detail::AsyncLogMsg second(std::move(first));
    novainfra::log::detail::AsyncLogMsg third(novainfra::log::detail::AsyncMsgType::FLUSH);
    third = std::move(second);

    EXPECT_EQ(third.message, expectedMessage);
    EXPECT_EQ(third.source.file, expectedFile);
    EXPECT_EQ(third.source.function, expectedFunction);
    EXPECT_EQ(third.source.line, 73);
    EXPECT_FALSE(third.source.hasStaticStorage());
}

TEST(AsyncLogMsg, MoveOperationsPreserveStaticSourceMarker)
{
    constexpr auto source = novainfra::log::SourceLocation::fromStatic("static.cpp", 17, "staticFunction");
    novainfra::log::LogMsg logMessage{
        novainfra::SystemTimePoint{}, novainfra::log::Level::INFO, 7, source, "logger", "message"};
    novainfra::log::detail::AsyncLogMsg first(std::shared_ptr<novainfra::log::async::AsyncLogger>{},
                                               novainfra::log::detail::AsyncMsgType::LOG,
                                               std::move(logMessage));

    novainfra::log::detail::AsyncLogMsg second(std::move(first));

    EXPECT_EQ(second.source.file, "static.cpp");
    EXPECT_EQ(second.source.function, "staticFunction");
    EXPECT_EQ(second.source.line, 17);
    EXPECT_TRUE(second.source.hasStaticStorage());
}

TEST(AsyncLogger, ShutdownDrainsMessagesAndFlushesSinks)
{
    auto sink = std::make_shared<RecordingSink>();
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(64, 1);
    auto logger = std::make_shared<novainfra::log::async::AsyncLogger>("async", sink, worker);

    for (int i = 0; i < 50; ++i) {
        logger->info("message {}", i);
    }
    worker.reset();

    const auto messages = sink->messages();
    ASSERT_EQ(messages.size(), 50U);
    EXPECT_EQ(messages.front().message, "message 0");
    EXPECT_EQ(messages.back().message, "message 49");
    EXPECT_GE(sink->flushCount(), 1);
}

TEST(AsyncLogger, ShutdownFlushesAfterCallerReleasesLogger)
{
    auto sink = std::make_shared<RecordingSink>();
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(4, 1);
    auto logger = std::make_shared<novainfra::log::async::AsyncLogger>("async", sink, worker);

    logger->info("message");
    logger.reset();
    worker.reset();

    ASSERT_EQ(sink->messages().size(), 1U);
    EXPECT_GE(sink->flushCount(), 1);
}

TEST(AsyncLogger, ReleasesProcessedLoggerBeforeWorkerShutdown)
{
    auto sink = std::make_shared<RecordingSink>();
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(8, 1);
    std::weak_ptr<novainfra::log::async::AsyncLogger> weakLogger;
    {
        auto logger = std::make_shared<novainfra::log::async::AsyncLogger>("short-lived", sink, worker);
        weakLogger = logger;
        logger->info("message");
    }

    ASSERT_TRUE(waitUntil([&] { return weakLogger.expired(); }));
    ASSERT_EQ(sink->messages().size(), 1U);
    EXPECT_GE(sink->flushCount(), 1);

    worker.reset();
}

TEST(AsyncLogger, LoggerChurnDoesNotAccumulateStrongWorkerReferences)
{
    auto sink = std::make_shared<RecordingSink>();
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(32, 1);
    std::vector<std::weak_ptr<novainfra::log::async::AsyncLogger>> weakLoggers;
    weakLoggers.reserve(600);

    for (int index = 0; index < 600; ++index) {
        auto logger = std::make_shared<novainfra::log::async::AsyncLogger>("churn-" + std::to_string(index),
                                                                           sink, worker);
        weakLoggers.push_back(logger);
        logger->info("{}", index);
    }

    ASSERT_TRUE(waitUntil([&] {
        return std::all_of(weakLoggers.begin(), weakLoggers.end(),
                           [](const auto &logger) { return logger.expired(); });
    }));
    EXPECT_EQ(sink->messages().size(), weakLoggers.size());

    worker.reset();
}

TEST(AsyncLogger, OwnsDynamicSourceLocationAcrossThreads)
{
    auto sink = std::make_shared<BlockingRecordingSink>();
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(4, 1);
    auto logger = std::make_shared<novainfra::log::async::AsyncLogger>("async", sink, worker);

    logger->info("blocking message");
    ASSERT_TRUE(sink->waitUntilBlocked());
    {
        std::string file = "dynamic/source.cpp";
        std::string function = "dynamicFunction";
        logger->log(novainfra::log::SourceLocation{file, 73, function}, novainfra::log::Level::INFO,
                    "owned source");
        file.assign(2048, 'x');
        function.assign(2048, 'y');
    }

    sink->release();
    worker.reset();

    const auto messages = sink->messages();
    ASSERT_EQ(messages.size(), 2U);
    EXPECT_EQ(messages[1].message, "owned source");
    EXPECT_EQ(messages[1].file, "dynamic/source.cpp");
    EXPECT_EQ(messages[1].function, "dynamicFunction");
    EXPECT_EQ(messages[1].line, 73);
    EXPECT_FALSE(messages[1].staticSource);
}

TEST(AsyncLogger, OwnsLongAndEmbeddedNullMessages)
{
    auto sink = std::make_shared<RecordingSink>();
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(4, 1);
    auto logger = std::make_shared<novainfra::log::async::AsyncLogger>("async", sink, worker);
    const std::string longMessage(2048, 'x');
    const std::string embeddedNull("left\0right", 10);

    logger->info("{}", longMessage);
    logger->info("{}", embeddedNull);
    worker.reset();

    const auto messages = sink->messages();
    ASSERT_EQ(messages.size(), 2U);
    EXPECT_EQ(messages[0].message, longMessage);
    EXPECT_EQ(messages[1].message, embeddedNull);
}

TEST(AsyncLogger, MacroSourceRetainsStaticStorageMarker)
{
    auto sink = std::make_shared<RecordingSink>();
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(4, 1);
    auto logger = std::make_shared<novainfra::log::async::AsyncLogger>("async", sink, worker);

    NOVAINFRA_LOGGER_INFO(logger, "macro source");
    worker.reset();

    const auto messages = sink->messages();
    ASSERT_EQ(messages.size(), 1U);
    EXPECT_TRUE(messages[0].staticSource);
    EXPECT_NE(messages[0].file.find("async_logger_tests.cpp"), std::string::npos);
}

TEST(AsyncLogger, ContinuesAfterUnknownSinkException)
{
    auto sink = std::make_shared<ThrowOnceSink>(ThrowOnceSink::ExceptionType::UNKNOWN);
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(4, 1);
    auto logger = std::make_shared<novainfra::log::async::AsyncLogger>("async", sink, worker);
    std::atomic<int> errors{0};
    logger->setErrHandler([&](const std::string &) { ++errors; });

    logger->info("throws");
    logger->info("continues");
    worker.reset();

    EXPECT_EQ(sink->successes(), 1);
    EXPECT_EQ(errors.load(), 1);
}

TEST(AsyncLogger, ContinuesWhenCustomErrorHandlerThrows)
{
    auto sink = std::make_shared<ThrowOnceSink>(ThrowOnceSink::ExceptionType::STANDARD);
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(4, 1);
    auto logger = std::make_shared<novainfra::log::async::AsyncLogger>("async", sink, worker);
    logger->setErrHandler([](const std::string &) { throw std::runtime_error("handler failure"); });

    logger->info("throws");
    logger->info("continues");
    worker.reset();

    EXPECT_EQ(sink->successes(), 1);
}

TEST(AsyncLogger, RejectsInvalidOverflowPolicy)
{
    auto sink = std::make_shared<RecordingSink>();
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(4, 1);

    EXPECT_THROW(std::make_shared<novainfra::log::async::AsyncLogger>(
                     "async", sink, worker, static_cast<novainfra::log::async::AsyncOverflowPolicy>(99)),
                 std::invalid_argument);
}

TEST(AsyncLogger, DiscardNewPolicyCountsAndDropsNewestLog)
{
    auto sink = std::make_shared<BlockingRecordingSink>();
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(1, 1);
    auto logger = std::make_shared<novainfra::log::async::AsyncLogger>(
        "async", sink, worker, novainfra::log::async::AsyncOverflowPolicy::DISCARD_NEW);

    logger->info("first");
    ASSERT_TRUE(sink->waitUntilBlocked());
    logger->info("second");
    logger->info("discarded");
    EXPECT_EQ(worker->discardCounter(), 1U);

    sink->release();
    worker.reset();

    const auto messages = sink->messages();
    ASSERT_EQ(messages.size(), 2U);
    EXPECT_EQ(messages[0].message, "first");
    EXPECT_EQ(messages[1].message, "second");
}

TEST(AsyncLogger, OverrunPolicyCountsAndDropsOldestQueuedLog)
{
    auto sink = std::make_shared<BlockingRecordingSink>();
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(1, 1);
    auto logger = std::make_shared<novainfra::log::async::AsyncLogger>(
        "async", sink, worker, novainfra::log::async::AsyncOverflowPolicy::OVER_RUN_OLDEST);

    logger->info("first");
    ASSERT_TRUE(sink->waitUntilBlocked());
    logger->info("overrun");
    logger->info("newest");
    EXPECT_EQ(worker->overrunCounter(), 1U);

    sink->release();
    worker.reset();

    const auto messages = sink->messages();
    ASSERT_EQ(messages.size(), 2U);
    EXPECT_EQ(messages[0].message, "first");
    EXPECT_EQ(messages[1].message, "newest");
}

TEST(AsyncLogger, FlushControlMessageIsNotDiscardedWhenQueueIsFull)
{
    auto sink = std::make_shared<BlockingRecordingSink>();
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(1, 1);
    auto logger = std::make_shared<novainfra::log::async::AsyncLogger>(
        "async", sink, worker, novainfra::log::async::AsyncOverflowPolicy::DISCARD_NEW);

    logger->info("first");
    ASSERT_TRUE(sink->waitUntilBlocked());
    logger->info("second");
    auto flushResult = std::async(std::launch::async, [logger] { logger->flush(); });
    EXPECT_EQ(flushResult.wait_for(50ms), std::future_status::timeout);

    sink->release();
    EXPECT_EQ(flushResult.wait_for(2s), std::future_status::ready);
    flushResult.get();
    worker.reset();

    EXPECT_EQ(worker, nullptr);
    EXPECT_EQ(sink->messages().size(), 2U);
    EXPECT_GE(sink->flushCount(), 2);
}

TEST(AsyncLogger, WorkerThreadFlushDoesNotDeadlockWhenQueueIsFull)
{
    auto sink = std::make_shared<BlockingRecordingSink>();
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(1, 1);
    auto logger = std::make_shared<novainfra::log::async::AsyncLogger>("async", sink, worker);
    std::weak_ptr<novainfra::log::async::AsyncLogger> weakLogger = logger;
    sink->setReleaseCallback([weakLogger] {
        if (auto locked = weakLogger.lock()) {
            locked->flush();
            locked->flush();
            locked->flush();
        }
    });

    logger->info("first");
    ASSERT_TRUE(sink->waitUntilBlocked());
    logger->info("second");
    sink->release();
    ASSERT_TRUE(sink->waitForFlushCount(1));
    EXPECT_EQ(sink->flushCount(), 1);
    worker.reset();

    EXPECT_EQ(sink->messages().size(), 2U);
    EXPECT_GE(sink->flushCount(), 2);
}

TEST(AsyncLogger, StackManagedInstanceReportsOwnershipError)
{
    auto sink = std::make_shared<RecordingSink>();
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(4, 1);
    novainfra::log::async::AsyncLogger logger("stack", sink, worker);
    int errors = 0;
    logger.setErrHandler([&](const std::string &) { ++errors; });

    logger.info("not enqueued");
    logger.flush();
    worker.reset();

    EXPECT_EQ(errors, 2);
    EXPECT_TRUE(sink->messages().empty());
}

TEST(AsyncLogger, BacktraceOwnsOriginallyStaticSourceUntilAsyncDispatch)
{
    auto sink = std::make_shared<BlockingRecordingSink>();
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(16, 1);
    auto logger = std::make_shared<novainfra::log::async::AsyncLogger>("backtrace", sink, worker);
    logger->info("hold");
    const bool blocked = sink->waitUntilBlocked();
    EXPECT_TRUE(blocked);
    logger->setLevel(novainfra::log::Level::WARN);
    logger->enableBacktrace(1);
    logger->log(novainfra::log::SourceLocation::fromStatic("backtrace_file.cpp", 17, "backtrace_function"),
                novainfra::log::Level::DEBUG, "hidden");
    logger->dumpBacktrace();
    sink->release();
    worker.reset();
    bool found = false;
    for (const auto &msg : sink->messages()) {
        if (msg.message == "hidden") {
            found = true;
            EXPECT_EQ(msg.file, "backtrace_file.cpp");
            EXPECT_EQ(msg.function, "backtrace_function");
            EXPECT_EQ(msg.line, 17);
            EXPECT_FALSE(msg.staticSource);
        }
    }
    EXPECT_TRUE(found);
}

}  // namespace
