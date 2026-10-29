#pragma once

#include <core/export.h>
#include <core/macros.h>
#include <core/namespace.h>
#include <thread_pool/types.h>

#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

NOVAINFRA_NAMESPACE_BEGIN
namespace threadpool {

namespace detail {
// Identifies reference_wrapper values so explicit std::ref/std::cref is preserved.
template <typename T>
struct is_reference_wrapper : std::false_type {
};

template <typename T>
struct is_reference_wrapper<std::reference_wrapper<T>> : std::true_type {
};

template <typename T>
using stored_argument_t = std::conditional_t<
    is_reference_wrapper<std::decay_t<T>>::value, std::decay_t<T>,
    std::conditional_t<std::is_lvalue_reference_v<T>, std::reference_wrapper<std::remove_reference_t<T>>,
                       std::decay_t<T>>>;

// Store lvalue arguments as references and rvalue arguments as owned values.
template <typename T>
stored_argument_t<T> storeArgument(T &&argument)
{
    if constexpr (is_reference_wrapper<std::decay_t<T>>::value) {
        return stored_argument_t<T>(std::forward<T>(argument));
    } else if constexpr (std::is_lvalue_reference_v<T>) {
        return stored_argument_t<T>(argument);
    } else {
        return stored_argument_t<T>(std::forward<T>(argument));
    }
}

// Restore the invocation category of an argument stored in a task closure.
template <typename T>
decltype(auto) forwardStoredArgument(T &argument)
{
    if constexpr (is_reference_wrapper<std::decay_t<T>>::value) {
        return argument.get();
    } else {
        return std::move(argument);
    }
}

// Keep lvalue callables referential and own rvalue callables in the task.
template <typename F>
using stored_function_t =
    std::conditional_t<std::is_lvalue_reference_v<F>, std::reference_wrapper<std::remove_reference_t<F>>,
                       std::decay_t<F>>;

template <typename F>
stored_function_t<F> storeFunction(F &&function)
{
    if constexpr (std::is_lvalue_reference_v<F>) {
        return stored_function_t<F>(function);
    } else {
        return stored_function_t<F>(std::forward<F>(function));
    }
}

// Invoke a callable with the same lvalue/rvalue category it had at submission.
template <typename OriginalF, typename StoredF, typename... Args>
decltype(auto) invokeStoredFunction(StoredF &function, Args &&...args)
{
    if constexpr (std::is_lvalue_reference_v<OriginalF>) {
        return std::invoke(function.get(), std::forward<Args>(args)...);
    } else {
        return std::invoke(std::move(function), std::forward<Args>(args)...);
    }
}

template <typename F, typename... Args>
auto bindTask(F &&function, Args &&...args)
{
    using Result = std::invoke_result_t<F, Args...>;
    using Arguments = std::tuple<stored_argument_t<Args>...>;
    return [function = storeFunction<F>(std::forward<F>(function)),
            arguments = Arguments(storeArgument<Args>(std::forward<Args>(args))...)]() mutable -> Result {
        return std::apply(
            [&function](auto &...unpacked) -> Result {
                return invokeStoredFunction<F>(function, forwardStoredArgument(unpacked)...);
            },
            arguments);
    };
}
}  // namespace detail

/**
 * @brief 固定 worker 数量和有界等待队列的线程池。
 *
 * submit/trySubmit 返回 future，post/tryPost 用于 fire-and-forget 任务。
 * 线程池析构时会 drain 已接受任务并等待 worker 退出。
 */
class NOVAINFRA_LIB_PORT ThreadPool
{
private:
    // Type-erased task interface stored by the worker queue.
    struct Task {
        virtual ~Task() = default;
        virtual void run() = 0;
    };

    template <typename F>
    struct TaskModel NOVAINFRA_FINAL : Task {
        explicit TaskModel(F &&function) : function_(std::forward<F>(function)) {}
        // Execute the concrete callable held by this task.
        void run() NOVAINFRA_OVERRIDE { function_(); }
        F function_;
    };

    template <typename F>
    static std::unique_ptr<Task> makeTask(F &&function)
    {
        using Function = std::decay_t<F>;
        return std::unique_ptr<Task>(new TaskModel<Function>(std::forward<F>(function)));
    }

public:
    /**
     * @brief 创建线程池。
     * @param options worker 数量、队列容量和异常处理回调。
     */
    explicit ThreadPool(ThreadPoolOptions options = {});
    /** @brief drain 已接受任务并等待所有 worker 退出。 */
    ~ThreadPool() NOVAINFRA_NOEXCEPT;

    NOVAINFRA_DISABLE_COPY_MOVE(ThreadPool)

    /**
     * @brief 提交一个有返回值的任务并等待队列空间。
     * @tparam F 可调用对象类型。
     * @tparam Args 传给可调用对象的参数类型。
     * @param function 要在线程池中执行的函数。
     * @param args 传给函数的参数；左值以引用保存，右值移动保存。
     * @return 保存任务返回值或异常的 future。
     * @throws std::runtime_error 线程池已停止接受任务时抛出。
     * @throws std::logic_error worker 任务阻塞提交导致潜在死锁时抛出。
     */
    template <typename F, typename... Args>
    auto submit(F &&function, Args &&...args) -> std::future<std::invoke_result_t<F, Args...>>
    {
        using Result = std::invoke_result_t<F, Args...>;
        auto bound = detail::bindTask(std::forward<F>(function), std::forward<Args>(args)...);
        std::packaged_task<Result()> packaged(std::move(bound));
        auto result = packaged.get_future();
        enqueue(makeTask([task = std::move(packaged)]() mutable { task(); }), true);
        return result;
    }

    /**
     * @brief 提交一个无返回值任务并等待队列空间。
     * @tparam F 可调用对象类型。
     * @tparam Args 传给可调用对象的参数类型。
     * @param function 要在线程池中执行的函数。
     * @param args 传给函数的参数；左值以引用保存，右值移动保存。
     * @throws std::runtime_error 线程池已停止接受任务时抛出。
     * @throws std::logic_error worker 任务阻塞提交导致潜在死锁时抛出。
     */
    template <typename F, typename... Args>
    void post(F &&function, Args &&...args)
    {
        auto bound = detail::bindTask(std::forward<F>(function), std::forward<Args>(args)...);
        enqueue(makeTask([this, task = std::move(bound)]() mutable {
                    NOVAINFRA_TRY
                    {
                        task();
                    }
                    NOVAINFRA_CATCH_ALL_BODY({ handleUnhandledException(std::current_exception()); })
                }),
                true);
    }

    /**
     * @brief 非阻塞提交一个有返回值的任务。
     * @tparam F 可调用对象类型。
     * @tparam Args 传给可调用对象的参数类型。
     * @param function 要在线程池中执行的函数。
     * @param args 传给函数的参数。
     * @return 成功时包含 future；队列已满或线程池已停止时返回空值。
     */
    template <typename F, typename... Args>
    auto trySubmit(F &&function,
                   Args &&...args) -> std::optional<std::future<std::invoke_result_t<F, Args...>>>
    {
        using Result = std::invoke_result_t<F, Args...>;
        auto bound = detail::bindTask(std::forward<F>(function), std::forward<Args>(args)...);
        std::packaged_task<Result()> packaged(std::move(bound));
        auto result = packaged.get_future();
        if (!enqueue(makeTask([task = std::move(packaged)]() mutable { task(); }), false)) {
            return std::nullopt;
        }
        return std::optional<std::future<Result>>(std::move(result));
    }

    /**
     * @brief 非阻塞提交一个无返回值任务。
     * @tparam F 可调用对象类型。
     * @tparam Args 传给可调用对象的参数类型。
     * @param function 要在线程池中执行的函数。
     * @param args 传给函数的参数。
     * @return 成功入队返回 true；队列已满或线程池已停止返回 false。
     */
    template <typename F, typename... Args>
    bool tryPost(F &&function, Args &&...args)
    {
        auto bound = detail::bindTask(std::forward<F>(function), std::forward<Args>(args)...);
        return enqueue(makeTask([this, task = std::move(bound)]() mutable {
                           NOVAINFRA_TRY
                           {
                               task();
                           }
                           NOVAINFRA_CATCH_ALL_BODY({ handleUnhandledException(std::current_exception()); })
                       }),
                       false);
    }

    /**
     * @brief 等待当前队列和正在执行的任务都为空。
     * @note 其他线程随后提交的新任务不包含在本次等待范围内。
     * @throws std::logic_error 在线程池自己的 worker 中调用时抛出。
     */
    void waitIdle();
    /**
     * @brief 停止接受任务，执行已接受任务并等待所有 worker 退出。
     * @note 重复调用是安全的；不能从本线程池的 worker 中调用。
     */
    void shutdown() NOVAINFRA_NOEXCEPT;

    /** @brief 返回当前是否接受新任务。 */
    bool accepting() const NOVAINFRA_NOEXCEPT;
    /** @brief 返回是否尚未记录 worker 内部故障。 */
    bool healthy() const NOVAINFRA_NOEXCEPT;
    /** @brief 返回首次记录的 worker 内部故障；没有故障时返回空异常指针。 */
    std::exception_ptr failure() const NOVAINFRA_NOEXCEPT;
    /** @brief 返回配置的 worker 数量。 */
    std::size_t workerCount() const NOVAINFRA_NOEXCEPT;
    /** @brief 返回当前等待队列中的任务数量。 */
    std::size_t queueSize() const NOVAINFRA_NOEXCEPT;
    /** @brief 返回当前正在执行的任务数量。 */
    std::size_t activeTaskCount() const NOVAINFRA_NOEXCEPT;
    /** @brief 返回线程池当前状态快照。 */
    ThreadPoolStats stats() const NOVAINFRA_NOEXCEPT;

private:
    // Enqueue a task, optionally applying bounded-queue backpressure.
    bool enqueue(std::unique_ptr<Task> task, bool block);
    // Dispatch a fire-and-forget task exception to the configured handler.
    void handleUnhandledException(std::exception_ptr exception) NOVAINFRA_NOEXCEPT;
    // Decrement active task count and wake idle waiters when the pool becomes idle.
    void taskFinished() NOVAINFRA_NOEXCEPT;
    // Worker main loop: dequeue, execute, classify failures, and exit cleanly.
    void workerLoop() NOVAINFRA_NOEXCEPT;
    // Record the first internal worker failure and stop accepting tasks.
    void recordWorkerFailure(std::exception_ptr exception) NOVAINFRA_NOEXCEPT;
    // Account for a worker exit and abandon queued tasks after total worker loss.
    void workerExited() NOVAINFRA_NOEXCEPT;

    struct State;
    std::unique_ptr<State> state_;
#ifdef NOVAINFRA_THREAD_POOL_TESTING
    friend class ThreadPoolTestAccess;
#endif
    static thread_local ThreadPool *currentWorkerPool_;
};

}  // namespace threadpool
NOVAINFRA_NAMESPACE_END
