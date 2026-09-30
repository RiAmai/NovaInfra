#include <thread_pool/thread_pool.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using novainfra::threadpool::ThreadPool;
using novainfra::threadpool::ThreadPoolOptions;

namespace {
struct LvalueCallable {
    int value = 0;
    int operator()() & { return ++value; }
};

struct RvalueCallable {
    int operator()() && { return 42; }
};
}  // namespace

#ifdef NOVAINFRA_THREAD_POOL_TESTING
namespace novainfra::threadpool {
class ThreadPoolTestAccess
{
public:
    static void fail(ThreadPool &pool, std::exception_ptr exception)
    {
        pool.recordWorkerFailure(std::move(exception));
    }

    static void failAndAbandon(ThreadPool &pool, std::exception_ptr exception)
    {
        pool.recordWorkerFailure(std::move(exception));
        pool.workerExited();
    }
};
}  // namespace novainfra::threadpool
#endif

TEST(ThreadPool, SubmitReturnsValueAndSupportsMoveOnlyArguments)
{
    ThreadPoolOptions options;
    options.workerCount = 2;
    options.queueCapacity = 8;
    ThreadPool pool(options);

    auto value =
        pool.submit([](std::unique_ptr<int> input) { return *input + 1; }, std::make_unique<int>(41));
    EXPECT_EQ(value.get(), 42);
}

TEST(ThreadPool, PreservesCallableAndArgumentValueCategories)
{
    ThreadPoolOptions options;
    options.workerCount = 1;
    options.queueCapacity = 8;
    ThreadPool pool(options);

    LvalueCallable lvalueCallable;
    auto lvalueResult = pool.submit(lvalueCallable);
    auto rvalueResult = pool.submit(RvalueCallable{});
    int value = 10;
    auto referenceResult = pool.submit([](int &input) { return ++input; }, value);

    EXPECT_EQ(lvalueResult.get(), 1);
    EXPECT_EQ(rvalueResult.get(), 42);
    EXPECT_EQ(referenceResult.get(), 11);
    EXPECT_EQ(value, 11);
    EXPECT_EQ(lvalueCallable.value, 1);
}

TEST(ThreadPool, PreservesExplicitReferenceWrapper)
{
    ThreadPoolOptions options;
    options.workerCount = 1;
    options.queueCapacity = 4;
    ThreadPool pool(options);
    int value = 1;
    const int constant = 2;
    auto reference = std::ref(value);
    auto constantReference = std::cref(constant);

    auto result = pool.submit([](int &input) { input += 41; }, reference);
    auto constantResult = pool.submit([](const int &input) { return input; }, constantReference);
    result.get();
    EXPECT_EQ(constantResult.get(), 2);
    EXPECT_EQ(value, 42);
}

TEST(ThreadPool, ExecutesAllTasksAndWaitsIdle)
{
    ThreadPoolOptions options;
    options.workerCount = 4;
    options.queueCapacity = 128;
    ThreadPool pool(options);
    std::atomic<int> count{0};

    for (int i = 0; i < 1000; ++i) {
        pool.post([&count] { count.fetch_add(1, std::memory_order_relaxed); });
    }
    pool.waitIdle();
    EXPECT_EQ(count.load(), 1000);
    EXPECT_EQ(pool.activeTaskCount(), 0U);
    EXPECT_EQ(pool.queueSize(), 0U);
}

TEST(ThreadPool, TrySubmitRejectsWhenQueueIsFull)
{
    ThreadPoolOptions options;
    options.workerCount = 1;
    options.queueCapacity = 1;
    ThreadPool pool(options);
    std::promise<void> release;
    auto gate = release.get_future().share();

    pool.post([gate] { gate.wait(); });
    pool.post([] {});
    EXPECT_FALSE(pool.tryPost([] {}));
    release.set_value();
    pool.waitIdle();
}

TEST(ThreadPool, SubmitPropagatesTaskException)
{
    ThreadPoolOptions options;
    options.workerCount = 1;
    options.queueCapacity = 8;
    ThreadPool pool(options);
    auto result = pool.submit([]() -> int { throw std::logic_error("failure"); });
    EXPECT_THROW(result.get(), std::logic_error);
}

TEST(ThreadPool, FireAndForgetReportsExceptionAndWorkerContinues)
{
    ThreadPoolOptions options;
    options.workerCount = 1;
    options.queueCapacity = 8;
    std::promise<std::exception_ptr> reported;
    auto future = reported.get_future();
    options.onUnhandledException = [&reported](std::exception_ptr error) {
        reported.set_value(std::move(error));
    };
    ThreadPool pool(options);

    pool.post([] { throw std::runtime_error("background failure"); });
    pool.post([] {});
    ASSERT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_THROW(std::rethrow_exception(future.get()), std::runtime_error);
    pool.waitIdle();
}

TEST(ThreadPool, DefaultExceptionDiagnosticIncludesMessage)
{
    ThreadPoolOptions options;
    options.workerCount = 1;
    options.queueCapacity = 4;
    ThreadPool pool(options);
    testing::internal::CaptureStderr();
    pool.post([] { throw std::runtime_error("diagnostic detail"); });
    pool.waitIdle();
    const auto output = testing::internal::GetCapturedStderr();
    EXPECT_NE(output.find("diagnostic detail"), std::string::npos);
}

TEST(ThreadPool, WaitIdleRejectsWorkerTask)
{
    ThreadPoolOptions options;
    options.workerCount = 1;
    options.queueCapacity = 4;
    std::promise<std::exception_ptr> reported;
    auto future = reported.get_future();
    options.onUnhandledException = [&reported](std::exception_ptr error) {
        reported.set_value(std::move(error));
    };
    ThreadPool pool(options);

    pool.post([&pool] { pool.waitIdle(); });
    ASSERT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_THROW(std::rethrow_exception(future.get()), std::logic_error);
    pool.waitIdle();
}

TEST(ThreadPool, ShutdownDrainsAndRejectsNewTasks)
{
    ThreadPoolOptions options;
    options.workerCount = 2;
    options.queueCapacity = 16;
    ThreadPool pool(options);
    std::atomic<int> count{0};
    for (int i = 0; i < 20; ++i) {
        pool.post([&count] { count.fetch_add(1, std::memory_order_relaxed); });
    }

    pool.shutdown();
    EXPECT_EQ(count.load(), 20);
    EXPECT_FALSE(pool.accepting());
    EXPECT_FALSE(pool.tryPost([] {}));
    EXPECT_THROW(pool.post([] {}), std::runtime_error);
    pool.shutdown();
}

TEST(ThreadPool, InvalidCapacityIsRejected)
{
    ThreadPoolOptions options;
    options.workerCount = 1;
    options.queueCapacity = 0;
    EXPECT_THROW(ThreadPool pool(options), std::invalid_argument);
}

TEST(ThreadPool, ConcurrentShutdownIsSafe)
{
    ThreadPoolOptions options;
    options.workerCount = 4;
    options.queueCapacity = 64;
    ThreadPool pool(options);
    std::atomic<int> count{0};
    for (int i = 0; i < 100; ++i) {
        pool.post([&count] {
            std::this_thread::yield();
            count.fetch_add(1, std::memory_order_relaxed);
        });
    }

    std::thread first([&pool] { pool.shutdown(); });
    std::thread second([&pool] { pool.shutdown(); });
    first.join();
    second.join();

    EXPECT_EQ(count.load(), 100);
    EXPECT_FALSE(pool.accepting());
}

TEST(ThreadPool, ConcurrentSubmissionAndShutdownDoNotLoseAcceptedTasks)
{
    ThreadPoolOptions options;
    options.workerCount = 2;
    options.queueCapacity = 32;
    ThreadPool pool(options);
    std::atomic<int> accepted{0};
    std::atomic<int> executed{0};
    std::promise<void> start;
    auto gate = start.get_future().share();

    std::thread producer([&] {
        gate.wait();
        while (pool.tryPost([&executed] { executed.fetch_add(1, std::memory_order_relaxed); })) {
            accepted.fetch_add(1, std::memory_order_relaxed);
        }
    });
    start.set_value();
    while (accepted.load(std::memory_order_relaxed) == 0) {
        std::this_thread::yield();
    }
    pool.shutdown();
    producer.join();

    EXPECT_EQ(executed.load(), accepted.load());
    EXPECT_FALSE(pool.tryPost([] {}));
}

TEST(ThreadPool, WorkerFailureIsObservableAndRejectsNewTasks)
{
    ThreadPoolOptions options;
    options.workerCount = 1;
    options.queueCapacity = 4;
    std::atomic<int> callbacks{0};
    options.onWorkerFailure = [&callbacks](std::exception_ptr) {
        callbacks.fetch_add(1, std::memory_order_relaxed);
    };
    ThreadPool pool(options);
    const auto error = std::make_exception_ptr(std::runtime_error("worker failure"));

    novainfra::threadpool::ThreadPoolTestAccess::fail(pool, error);
    novainfra::threadpool::ThreadPoolTestAccess::fail(pool, error);
    EXPECT_FALSE(pool.healthy());
    EXPECT_EQ(pool.failure(), error);
    EXPECT_FALSE(pool.stats().healthy);
    EXPECT_EQ(callbacks.load(), 1);
    EXPECT_FALSE(pool.tryPost([] {}));
    EXPECT_THROW(pool.post([] {}), std::runtime_error);
    pool.shutdown();
}

TEST(ThreadPool, WorkerFailureAllowsRemainingWorkersToDrain)
{
    ThreadPoolOptions options;
    options.workerCount = 2;
    options.queueCapacity = 8;
    ThreadPool pool(options);
    std::promise<void> release;
    auto gate = release.get_future().share();
    pool.post([gate] { gate.wait(); });
    auto result = pool.submit([] { return 42; });

    novainfra::threadpool::ThreadPoolTestAccess::fail(
        pool, std::make_exception_ptr(std::runtime_error("worker failure")));
    release.set_value();
    EXPECT_EQ(result.get(), 42);
    pool.shutdown();
}

TEST(ThreadPool, WorkerFailureAbandonsPendingTasksWhenAllWorkersExit)
{
    ThreadPoolOptions options;
    options.workerCount = 1;
    options.queueCapacity = 4;
    ThreadPool pool(options);
    std::promise<void> release;
    auto gate = release.get_future().share();

    pool.post([gate] { gate.wait(); });
    auto pending = pool.submit([] { return 42; });
    novainfra::threadpool::ThreadPoolTestAccess::failAndAbandon(
        pool, std::make_exception_ptr(std::runtime_error("worker failure")));

    release.set_value();
    EXPECT_THROW(pending.get(), std::future_error);
    pool.waitIdle();
    EXPECT_FALSE(pool.healthy());
    pool.shutdown();
}

TEST(ThreadPool, BlockedSubmitWakesWhenWorkerFailureStopsAcceptance)
{
    ThreadPoolOptions options;
    options.workerCount = 1;
    options.queueCapacity = 1;
    ThreadPool pool(options);
    std::promise<void> release;
    auto gate = release.get_future().share();
    pool.post([gate] { gate.wait(); });
    pool.post([] {});

    std::promise<std::exception_ptr> submissionError;
    auto errorFuture = submissionError.get_future();
    std::thread producer([&] {
        try {
            pool.post([] {});
        } catch (...) {
            submissionError.set_value(std::current_exception());
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    novainfra::threadpool::ThreadPoolTestAccess::fail(
        pool, std::make_exception_ptr(std::runtime_error("worker failure")));
    release.set_value();

    ASSERT_EQ(errorFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_THROW(std::rethrow_exception(errorFuture.get()), std::runtime_error);
    producer.join();
    pool.shutdown();
}

TEST(ThreadPool, WaitIdleWakesAfterAllWorkersFail)
{
    ThreadPoolOptions options;
    options.workerCount = 1;
    options.queueCapacity = 2;
    ThreadPool pool(options);
    std::promise<void> release;
    auto gate = release.get_future().share();
    pool.post([gate] { gate.wait(); });

    std::atomic<bool> idleReturned{false};
    std::thread waiter([&] {
        pool.waitIdle();
        idleReturned.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    novainfra::threadpool::ThreadPoolTestAccess::fail(
        pool, std::make_exception_ptr(std::runtime_error("worker failure")));
    release.set_value();

    waiter.join();
    EXPECT_TRUE(idleReturned.load(std::memory_order_acquire));
    pool.shutdown();
}

TEST(ThreadPool, WorkerFailureCallbackExceptionsAreContained)
{
    ThreadPoolOptions options;
    options.workerCount = 1;
    options.queueCapacity = 2;
    options.onWorkerFailure = [](std::exception_ptr) { throw std::runtime_error("callback failure"); };
    ThreadPool pool(options);

    EXPECT_NO_THROW(novainfra::threadpool::ThreadPoolTestAccess::fail(
        pool, std::make_exception_ptr(std::runtime_error("worker failure"))));
    EXPECT_FALSE(pool.healthy());
    pool.shutdown();
}

TEST(ThreadPool, NormalShutdownKeepsPoolHealthy)
{
    ThreadPoolOptions options;
    options.workerCount = 2;
    options.queueCapacity = 4;
    ThreadPool pool(options);
    pool.post([] {});
    pool.shutdown();
    EXPECT_TRUE(pool.healthy());
    EXPECT_FALSE(pool.failure());
}

TEST(ThreadPool, MultipleProducersExecuteEveryAcceptedTask)
{
    ThreadPoolOptions options;
    options.workerCount = 4;
    options.queueCapacity = 8;
    ThreadPool pool(options);
    constexpr int producerCount = 4;
    constexpr int tasksPerProducer = 250;
    std::atomic<int> executed{0};
    std::vector<std::thread> producers;
    producers.reserve(producerCount);

    for (int producer = 0; producer < producerCount; ++producer) {
        producers.emplace_back([&pool, &executed] {
            for (int task = 0; task < tasksPerProducer; ++task) {
                pool.post([&executed] { executed.fetch_add(1, std::memory_order_relaxed); });
            }
        });
    }
    for (auto &producer : producers) {
        producer.join();
    }
    pool.waitIdle();
    EXPECT_EQ(executed.load(std::memory_order_relaxed), producerCount * tasksPerProducer);
}

TEST(ThreadPool, WorkerBlockingSubmissionFailsFastWhenQueueIsFull)
{
    ThreadPoolOptions options;
    options.workerCount = 1;
    options.queueCapacity = 1;
    ThreadPool pool(options);
    std::promise<void> nestedFinished;
    auto finished = nestedFinished.get_future();

    auto outer = pool.submit([&pool, &nestedFinished] {
        pool.post([] {});
        bool rejected = false;
        try {
            pool.post([] {});
        } catch (const std::logic_error &) {
            rejected = true;
        }
        if (!rejected) {
            throw std::logic_error("worker submission was not rejected");
        }
        nestedFinished.set_value();
        return 7;
    });

    ASSERT_EQ(outer.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(outer.get(), 7);
    ASSERT_EQ(finished.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    pool.waitIdle();
}

TEST(ThreadPool, WorkerCanSubmitWhenQueueHasCapacity)
{
    ThreadPoolOptions options;
    options.workerCount = 1;
    options.queueCapacity = 2;
    ThreadPool pool(options);
    std::atomic<bool> submitted{false};

    auto outer = pool.submit([&pool, &submitted] {
        pool.post([&submitted] { submitted.store(true, std::memory_order_release); });
        return 1;
    });

    EXPECT_EQ(outer.get(), 1);
    pool.waitIdle();
    EXPECT_TRUE(submitted.load(std::memory_order_acquire));
}
