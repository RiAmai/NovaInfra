#include <thread_pool/thread_pool.h>

#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>

using novainfra::threadpool::ThreadPool;
using novainfra::threadpool::ThreadPoolOptions;

namespace {

#if !defined(NOVAINFRA_NO_EXCEPTIONS)
void printException(const char *prefix, std::exception_ptr exception)
{
    std::cout << prefix;
    try {
        if (exception) {
            std::rethrow_exception(std::move(exception));
        }
        std::cout << "no exception\n";
    } catch (const std::exception &error) {
        std::cout << error.what() << '\n';
    } catch (...) {
        std::cout << "unknown exception\n";
    }
}
#endif

void printStats(const ThreadPool &pool)
{
    const auto stats = pool.stats();
    std::cout << "stats: workers=" << stats.workerCount << ", queued=" << stats.queuedTasks
              << ", active=" << stats.activeTasks << ", accepting=" << std::boolalpha << stats.accepting
              << ", healthy=" << stats.healthy << '\n';
}

void demonstrateBasicSubmission()
{
    std::cout << "\n== basic submission ==\n";

    ThreadPoolOptions options;
    options.workerCount = 2;
    options.queueCapacity = 32;
    ThreadPool pool(options);

    auto square = pool.submit([](int value) { return value * value; }, 12);
    auto ownedValue =
        pool.submit([](std::unique_ptr<int> value) { return *value + 1; }, std::make_unique<int>(41));
    pool.post([] { std::cout << "background task completed\n"; });

    std::cout << "square(12)=" << square.get() << '\n';
    std::cout << "move-only result=" << ownedValue.get() << '\n';
    pool.waitIdle();
    printStats(pool);
}

void demonstrateNonBlockingSubmission()
{
    std::cout << "\n== non-blocking submission ==\n";

    ThreadPool pool(ThreadPoolOptions{});
    auto result = pool.trySubmit([] { return 7; });
    const bool posted = pool.tryPost([] { std::cout << "tryPost task completed\n"; });
    std::cout << "trySubmit accepted=" << std::boolalpha << result.has_value() << '\n';
    if (result) {
        std::cout << "trySubmit result=" << result->get() << '\n';
    }
    std::cout << "tryPost accepted=" << posted << '\n';
    pool.waitIdle();

    ThreadPoolOptions boundedOptions;
    boundedOptions.workerCount = 1;
    boundedOptions.queueCapacity = 1;
    ThreadPool boundedPool(boundedOptions);

    std::promise<void> started;
    auto startedFuture = started.get_future();
    std::promise<void> release;
    auto gate = release.get_future().share();
    boundedPool.post([&started, gate] {
        started.set_value();
        gate.wait();
    });
    startedFuture.wait();

    // The running task occupies the worker; this task fills the only queue slot.
    boundedPool.post([] {});
    const bool rejectedPost = boundedPool.tryPost([] {});
    const auto rejectedSubmit = boundedPool.trySubmit([] { return 9; });
    std::cout << "full queue: tryPost accepted=" << rejectedPost
              << ", trySubmit accepted=" << rejectedSubmit.has_value() << '\n';

    release.set_value();
    boundedPool.waitIdle();
}

#if !defined(NOVAINFRA_NO_EXCEPTIONS)
void demonstrateExceptionHandling()
{
    std::cout << "\n== exception handling ==\n";

    ThreadPoolOptions options;
    options.onUnhandledException = [](std::exception_ptr exception) {
        printException("onUnhandledException: ", std::move(exception));
    };
    options.onWorkerFailure = [](std::exception_ptr exception) {
        printException("onWorkerFailure: ", std::move(exception));
    };
    ThreadPool pool(options);

    auto failed = pool.submit([]() -> int { throw std::runtime_error("submit failure"); });
    try {
        (void)failed.get();
    } catch (const std::exception &error) {
        std::cout << "future exception: " << error.what() << '\n';
    }

    pool.post([] { throw std::runtime_error("post failure"); });
    pool.waitIdle();
    std::cout << "healthy=" << std::boolalpha << pool.healthy()
              << ", worker failure recorded=" << static_cast<bool>(pool.failure()) << '\n';
}
#endif

void demonstrateLifecycle()
{
    std::cout << "\n== lifecycle ==\n";

    ThreadPool pool;
    std::cout << "accepting=" << std::boolalpha << pool.accepting() << ", workers=" << pool.workerCount()
              << ", queued=" << pool.queueSize() << ", active=" << pool.activeTaskCount() << '\n';

    pool.post([] { std::cout << "lifecycle task completed\n"; });
    pool.waitIdle();
    pool.shutdown();
    std::cout << "accepting after shutdown=" << pool.accepting() << '\n';
    pool.shutdown();
}

}  // namespace

int main()
{
    demonstrateBasicSubmission();
    demonstrateNonBlockingSubmission();
#if !defined(NOVAINFRA_NO_EXCEPTIONS)
    demonstrateExceptionHandling();
#endif
    demonstrateLifecycle();
    return 0;
}
