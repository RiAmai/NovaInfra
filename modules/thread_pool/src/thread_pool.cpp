#include <thread_pool/thread_pool.h>

#include <condition_variable>
#include <cstdio>
#include <deque>
#include <stdexcept>
#include <thread>
#include <vector>

NOVAINFRA_NAMESPACE_BEGIN
namespace threadpool {

thread_local ThreadPool *ThreadPool::currentWorkerPool_ = nullptr;

struct ThreadPool::State {
    explicit State(ThreadPoolOptions value) : options(std::move(value)) {}

    // Immutable configuration and user-provided exception handlers.
    ThreadPoolOptions options;
    // Protects the queue, counters, and lifecycle flags below.
    mutable std::mutex mutex;
    // Wake workers when tasks arrive, producers when space appears, and idle waiters.
    std::condition_variable notEmpty;
    std::condition_variable notFull;
    std::condition_variable idle;
    // Serializes shutdown calls so worker joins are performed once at a time.
    std::mutex shutdownMutex;
    // Bounded FIFO queue owning tasks that have not started.
    std::deque<std::unique_ptr<Task>> taskQueue;
    // Owns all worker threads until shutdown joins and clears them.
    std::vector<std::thread> workers;
    // Number of tasks currently removed from the queue and executing.
    std::size_t activeTaskCount = 0;
    // Configured worker count versus workers that are still running.
    std::size_t configuredWorkerCount = 0;
    std::size_t liveWorkerCount = 0;
    // First internal worker failure; user task failures do not populate this.
    std::exception_ptr workerFailure;
    // Submission and worker lifecycle state guarded by mutex.
    bool accepting = true;
    bool stopping = false;
    bool healthy = true;
};

namespace {
// Resolve the default worker count while guaranteeing at least one worker.
std::size_t resolveWorkerCount(std::size_t requested)
{
    if (requested != 0) {
        return requested;
    }
    const auto detected = std::thread::hardware_concurrency();
    return detected == 0 ? 1 : static_cast<std::size_t>(detected);
}
}  // namespace

ThreadPool::ThreadPool(ThreadPoolOptions options) : state_(std::make_unique<State>(std::move(options)))
{
    const auto workers = resolveWorkerCount(state_->options.workerCount);
    state_->configuredWorkerCount = workers;
    if (state_->options.queueCapacity == 0) {
        NOVAINFRA_THROW(std::invalid_argument("ThreadPool queue capacity must be greater than zero"));
    }
    if (workers == 0) {
        NOVAINFRA_THROW(std::invalid_argument("ThreadPool worker count must be greater than zero"));
    }

    state_->workers.reserve(workers);
    NOVAINFRA_TRY
    {
        for (std::size_t index = 0; index < workers; ++index) {
            // Count only threads that are successfully created.
            {
                std::lock_guard<std::mutex> lock(state_->mutex);
                ++state_->liveWorkerCount;
            }
            NOVAINFRA_TRY
            {
                state_->workers.emplace_back([this] { workerLoop(); });
            }
            NOVAINFRA_CATCH_ALL_BODY({
                std::lock_guard<std::mutex> lock(state_->mutex);
                --state_->liveWorkerCount;
                NOVAINFRA_RETHROW_CURRENT();
            })
        }
    }
    NOVAINFRA_CATCH_ALL_BODY({
        shutdown();
        NOVAINFRA_RETHROW_CURRENT();
    })
}

ThreadPool::~ThreadPool() NOVAINFRA_NOEXCEPT
{
    shutdown();
}

bool ThreadPool::enqueue(std::unique_ptr<Task> task, bool block)
{
    {
        std::unique_lock<std::mutex> lock(state_->mutex);
        if (!state_->accepting) {
            if (!block) {
                return false;
            }
            NOVAINFRA_THROW(std::runtime_error("ThreadPool is shut down"));
        }

        if (block && currentWorkerPool_ == this &&
            state_->taskQueue.size() >= state_->options.queueCapacity) {
            // A worker cannot wait for another worker to free space without risking a pool-wide deadlock.
            NOVAINFRA_THROW(std::logic_error(
                "ThreadPool worker cannot block while submitting to a full queue; use tryPost/trySubmit"));
        }

        if (block) {
            state_->notFull.wait(lock, [this] {
                return !state_->accepting || state_->taskQueue.size() < state_->options.queueCapacity;
            });
            if (!state_->accepting) {
                NOVAINFRA_THROW(std::runtime_error("ThreadPool is shut down"));
            }
        } else if (state_->taskQueue.size() >= state_->options.queueCapacity) {
            return false;
        }

        state_->taskQueue.emplace_back(std::move(task));
    }

    state_->notEmpty.notify_one();
    return true;
}

void ThreadPool::handleUnhandledException(std::exception_ptr exception) NOVAINFRA_NOEXCEPT
{
    const auto &handler = state_->options.onUnhandledException;
    if (handler) {
        NOVAINFRA_TRY
        {
            handler(exception);
            return;
        }
        NOVAINFRA_CATCH_ALL_BODY({})
    }
#ifdef NOVAINFRA_NO_EXCEPTIONS
    std::fputs("[*** THREAD POOL ERROR ***] unhandled task exception\n", stderr);
#else
    try {
        if (exception) {
            std::rethrow_exception(exception);
        }
    } catch (const std::exception &error) {
        std::fprintf(stderr, "[*** THREAD POOL ERROR ***] unhandled task exception: %s\n", error.what());
        return;
    } catch (...) {
    }
    std::fputs("[*** THREAD POOL ERROR ***] unhandled unknown task exception\n", stderr);
#endif
}

void ThreadPool::taskFinished() NOVAINFRA_NOEXCEPT
{
    bool becameIdle = false;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        --state_->activeTaskCount;
        becameIdle = state_->taskQueue.empty() && state_->activeTaskCount == 0;
    }
    if (becameIdle) {
        state_->idle.notify_all();
    }
}

void ThreadPool::workerLoop() NOVAINFRA_NOEXCEPT
{
    currentWorkerPool_ = this;
    NOVAINFRA_TRY
    {
        for (;;) {
            std::unique_ptr<Task> task;
            bool taskActive = false;
            NOVAINFRA_TRY
            {
                {
                    std::unique_lock<std::mutex> lock(state_->mutex);
                    state_->notEmpty.wait(lock,
                                          [this] { return state_->stopping || !state_->taskQueue.empty(); });
                    // stopping drains the queue; an empty queue means this worker can exit.
                    if (state_->taskQueue.empty()) {
                        break;
                    }
                    task = std::move(state_->taskQueue.front());
                    state_->taskQueue.pop_front();
                    ++state_->activeTaskCount;
                    taskActive = true;
                }
                state_->notFull.notify_one();

                NOVAINFRA_TRY
                {
                    task->run();
                }
                NOVAINFRA_CATCH_ALL_BODY({ handleUnhandledException(std::current_exception()); })
            }
            NOVAINFRA_CATCH_ALL_BODY({
                if (taskActive) {
                    taskFinished();
                }
                recordWorkerFailure(std::current_exception());
                currentWorkerPool_ = nullptr;
                workerExited();
                return;
            })

            if (taskActive) {
                taskFinished();
            }
        }
    }
    NOVAINFRA_CATCH_ALL_BODY({ recordWorkerFailure(std::current_exception()); })
    currentWorkerPool_ = nullptr;
    workerExited();
}

void ThreadPool::recordWorkerFailure(std::exception_ptr exception) NOVAINFRA_NOEXCEPT
{
    bool firstFailure = false;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->healthy) {
            // Only the first internal failure is retained and reported.
            state_->healthy = false;
            state_->workerFailure = exception;
            state_->accepting = false;
            state_->stopping = true;
            firstFailure = true;
        }
    }

    state_->notEmpty.notify_all();
    state_->notFull.notify_all();
    state_->idle.notify_all();

    if (!firstFailure) {
        return;
    }
    const auto &handler = state_->options.onWorkerFailure;
    if (handler) {
        NOVAINFRA_TRY
        {
            handler(exception);
            return;
        }
        NOVAINFRA_CATCH_ALL_BODY({})
    }

#ifdef NOVAINFRA_NO_EXCEPTIONS
    std::fputs("[*** THREAD POOL ERROR ***] worker failure\n", stderr);
#else
    try {
        if (exception) {
            std::rethrow_exception(exception);
        }
    } catch (const std::exception &error) {
        std::fprintf(stderr, "[*** THREAD POOL ERROR ***] worker failure: %s\n", error.what());
        return;
    } catch (...) {
    }
    std::fputs("[*** THREAD POOL ERROR ***] unknown worker failure\n", stderr);
#endif
}

void ThreadPool::workerExited() NOVAINFRA_NOEXCEPT
{
    std::deque<std::unique_ptr<Task>> abandonedTasks;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->liveWorkerCount > 0) {
            --state_->liveWorkerCount;
        }
        // No worker remains to drain these tasks, so destroy them outside the lock.
        if (!state_->healthy && state_->liveWorkerCount == 0 && !state_->taskQueue.empty()) {
            abandonedTasks = std::move(state_->taskQueue);
        }
    }
    if (!abandonedTasks.empty()) {
        state_->idle.notify_all();
    }
}

void ThreadPool::waitIdle()
{
    if (currentWorkerPool_ == this) {
        NOVAINFRA_THROW(std::logic_error("ThreadPool::waitIdle() cannot be called from a worker task"));
    }
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->idle.wait(lock, [this] { return state_->taskQueue.empty() && state_->activeTaskCount == 0; });
}

void ThreadPool::shutdown() NOVAINFRA_NOEXCEPT
{
    if (!state_) {
        return;
    }
    std::lock_guard<std::mutex> shutdownLock(state_->shutdownMutex);
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->accepting && state_->stopping) {
            // A completed shutdown has already joined all workers.
            if (state_->workers.empty()) {
                return;
            }
        }
        state_->accepting = false;
        state_->stopping = true;
    }
    state_->notEmpty.notify_all();
    state_->notFull.notify_all();

    const auto current = std::this_thread::get_id();
    for (auto &worker : state_->workers) {
        if (worker.joinable()) {
            if (worker.get_id() == current) {
                std::fputs("[*** THREAD POOL ERROR ***] shutdown from worker thread\n", stderr);
                std::terminate();
            }
            worker.join();
        }
    }
    state_->workers.clear();
}

bool ThreadPool::accepting() const NOVAINFRA_NOEXCEPT
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->accepting;
}

bool ThreadPool::healthy() const NOVAINFRA_NOEXCEPT
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->healthy;
}

std::exception_ptr ThreadPool::failure() const NOVAINFRA_NOEXCEPT
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->workerFailure;
}

std::size_t ThreadPool::workerCount() const NOVAINFRA_NOEXCEPT
{
    return state_->configuredWorkerCount;
}

std::size_t ThreadPool::queueSize() const NOVAINFRA_NOEXCEPT
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->taskQueue.size();
}

std::size_t ThreadPool::activeTaskCount() const NOVAINFRA_NOEXCEPT
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->activeTaskCount;
}

ThreadPoolStats ThreadPool::stats() const NOVAINFRA_NOEXCEPT
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return {state_->configuredWorkerCount, state_->taskQueue.size(), state_->activeTaskCount,
            state_->accepting, state_->healthy};
}

}  // namespace threadpool
NOVAINFRA_NAMESPACE_END
