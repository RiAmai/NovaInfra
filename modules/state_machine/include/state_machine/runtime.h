#pragma once

#include <core/export.h>
#include <core/macros.h>
#include <core/namespace.h>
#include <state_machine/types.h>

#include <condition_variable>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

NOVAINFRA_NAMESPACE_BEGIN
namespace statemachine {

/**
 * @brief 为 typed StateMachine 增加有界、串行的事件队列。
 *
 * Runtime 不拥有状态机对象，也不会创建线程。任意线程都可以调用 post()；
 * Runtime 通过注入的 executor 提交一个 drain 任务，并保证同一时刻只有一个
 * drain 任务调用状态机。没有 executor 时，poll() 会在调用线程中串行驱动状态机。
 */
template <typename Machine>
class NOVAINFRA_LIB_PORT StateMachineRuntime
{
private:
    struct Shared;

    /** @brief 队列中保存的、已经拥有事件数据的任务。 */
    struct TaskBase {
        virtual ~TaskBase() = default;
        virtual void run(Shared &state) noexcept = 0;
    };
    using Task = std::unique_ptr<TaskBase>;

    template <typename Event>
    struct EventTaskImpl final : TaskBase {
        template <typename T>
        explicit EventTaskImpl(T &&value) : event(std::forward<T>(value))
        {
        }
        void run(Shared &state) noexcept override { state.process(event); }
        Event event;
    };

    struct Shared : std::enable_shared_from_this<Shared> {
        /** @brief 保存状态机引用和运行时配置；容量为零时修正为一。 */
        Shared(Machine &machine, RuntimeOptions options)
            : machine(&machine), options(std::move(options)), status(RuntimeStatus::RUNNING)
        {
            if (this->options.capacity == 0) {
                this->options.capacity = 1;
            }
        }

        /** @brief Runtime 不拥有的状态机指针；调用方必须保证其生命周期。 */
        Machine *machine;
        /** @brief 创建后不再修改的运行时配置。 */
        RuntimeOptions options;
        /** @brief 保护队列、状态和统计数据。 */
        mutable std::mutex mutex;
        /** @brief 串行化无 executor 模式下的 poll 调用。 */
        std::mutex pollMutex;
        /** @brief 等待队列排空和 drain 任务结束。 */
        std::condition_variable idle;
        /** @brief BLOCK 策略等待队列空间。 */
        std::condition_variable space;
        /** @brief 按投递顺序保存事件任务。 */
        std::deque<Task> queue;
        /** @brief 当前 Runtime 生命周期状态。 */
        RuntimeStatus status;
        /** @brief 是否仍接受新事件。 */
        bool accepting = true;
        /** @brief 是否已经向 executor 提交了 drain 任务。 */
        bool scheduled = false;
        /** @brief 是否存在正在执行的 drain 任务。 */
        bool draining = false;
        /** @brief 当前 drain 任务所属线程，用于拒绝自等待操作。 */
        std::thread::id drainThread;
        /** @brief 累计处理统计；详细队列状态在 stats() 中补齐。 */
        RuntimeStats counters;

        /** @brief 调用错误回调，并隔离错误回调自身抛出的异常。 */
        static void reportError(const RuntimeOptions &options, std::type_index type,
                                std::exception_ptr exception) noexcept
        {
            if (!options.onError) {
                return;
            }
            NOVAINFRA_TRY
            {
                options.onError(RuntimeError{type, exception});
            }
            NOVAINFRA_CATCH_ALL_BODY({})
        }

        /** @brief 调用事件处理观察回调，并隔离观察回调异常。 */
        static void reportProcessed(const RuntimeOptions &options, const ProcessReport &report) noexcept
        {
            if (!options.onProcessed) {
                return;
            }
            NOVAINFRA_TRY
            {
                options.onProcessed(report);
            }
            NOVAINFRA_CATCH_ALL_BODY({})
        }

        /** @brief 调用未处理事件回调，并隔离观察回调异常。 */
        static void reportUnhandled(const RuntimeOptions &options, std::type_index type) noexcept
        {
            if (!options.onUnhandledEvent) {
                return;
            }
            NOVAINFRA_TRY
            {
                options.onUnhandledEvent(type);
            }
            NOVAINFRA_CATCH_ALL_BODY({})
        }

        /**
         * @brief 将 Runtime 置为失败状态并清理尚未处理的事件。
         *
         * 状态机回调失败后不能继续假设状态图处于可恢复状态，因此停止接收事件。
         */
        void fail(std::type_index eventType, std::exception_ptr exception) noexcept
        {
            std::deque<Task> retired;
            {
                std::lock_guard<std::mutex> lock(mutex);
                status = RuntimeStatus::FAILED;
                accepting = false;
                counters.status = status;
                counters.failed++;
                counters.dropped += queue.size();
                retired.swap(queue);
                scheduled = false;
                space.notify_all();
                idle.notify_all();
            }
            retired.clear();
            reportError(options, eventType, exception);
        }

        /** @brief 向外部 executor 提交唯一的 drain 任务。 */
        bool schedule() noexcept
        {
            if (!options.executor) {
                return true;
            }

            bool accepted = false;
            NOVAINFRA_TRY
            {
                auto self = this->shared_from_this();
                accepted = options.executor([self] { self->drain(std::nullopt); });
            }
            NOVAINFRA_CATCH_ALL_BODY({
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    ++counters.executorFailures;
                }
                fail(typeid(void), std::current_exception());
                return false;
            })
            if (!accepted) {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    counters.executorFailures++;
                }
                fail(typeid(void), {});
                return false;
            }
            return true;
        }

        /** @brief 将事件值移动保存到任务中，保证异步执行时事件仍然有效。 */
        template <typename Event>
        static Task makeTask(Event &&event)
        {
            using StoredEvent = std::decay_t<Event>;
            return std::make_unique<EventTaskImpl<StoredEvent>>(std::forward<Event>(event));
        }

        /** @brief 在 drain 线程中执行一个事件并更新处理统计。 */
        template <typename Event>
        void process(const Event &event) noexcept
        {
            ProcessReport report;
            report.eventType = typeid(Event);
            NOVAINFRA_TRY
            {
                report.status =
                    machine->process_event(event) ? ProcessStatus::HANDLED : ProcessStatus::UNHANDLED;
                machine->flush_queue();
            }
            NOVAINFRA_CATCH_ALL_BODY({
                report.status = ProcessStatus::FAILED;
                report.exception = std::current_exception();
            })

            {
                std::lock_guard<std::mutex> lock(mutex);
                counters.processed++;
                if (report.status == ProcessStatus::HANDLED) {
                    counters.handled++;
                } else if (report.status == ProcessStatus::UNHANDLED) {
                    counters.unhandled++;
                }
            }
            reportProcessed(options, report);

            if (report.status == ProcessStatus::UNHANDLED) {
                reportUnhandled(options, report.eventType);
            } else if (report.status == ProcessStatus::FAILED) {
                fail(report.eventType, report.exception);
            }
        }

        /**
         * @brief 按 FIFO 顺序排空事件队列。
         * @param limit 本次最多处理的事件数。
         */
        void drain(const std::optional<std::size_t> &limit) noexcept
        {
            std::size_t drained = 0;
            {
                std::lock_guard<std::mutex> lock(mutex);
                draining = true;
                drainThread = std::this_thread::get_id();
                counters.status = status;
            }

            for (;;) {
                Task task;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    if (queue.empty() || status == RuntimeStatus::FAILED ||
                        (limit.has_value() && drained >= *limit)) {
                        scheduled = false;
                        draining = false;
                        drainThread = std::thread::id{};
                        if (!accepting && queue.empty() && status != RuntimeStatus::FAILED) {
                            status = RuntimeStatus::STOPPED;
                        }
                        counters.status = status;
                        counters.queued = queue.size();
                        counters.active = 0;
                        space.notify_all();
                        idle.notify_all();
                        return;
                    }
                    task = std::move(queue.front());
                    queue.pop_front();
                    counters.queued = queue.size();
                    counters.active = 1;
                    space.notify_all();
                }
                task->run(*this);
                // User payload destructors may call stats()/post(). They must
                // run without mutex and finish before this drain becomes idle.
                task.reset();
                ++drained;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    counters.active = 0;
                }
            }
        }

        /** @brief 将已封装事件加入队列，并按需安排 drain 任务。 */
        PostStatus post(Task task)
        {
            bool needsSchedule = false;
            {
                std::unique_lock<std::mutex> lock(mutex);
                if (!accepting || status == RuntimeStatus::FAILED || status == RuntimeStatus::STOPPED) {
                    return PostStatus::STOPPED;
                }
                while (queue.size() >= options.capacity) {
                    if (options.queueFullPolicy == QueueFullPolicy::REJECT) {
                        return PostStatus::QUEUE_FULL;
                    }
                    if (options.queueFullPolicy == QueueFullPolicy::DROP_NEWEST) {
                        counters.dropped++;
                        return PostStatus::DROPPED;
                    }
                    if (draining && drainThread == std::this_thread::get_id()) {
                        return PostStatus::QUEUE_FULL;
                    }
                    space.wait(lock, [this] {
                        return queue.size() < options.capacity || !accepting ||
                               status == RuntimeStatus::FAILED;
                    });
                    if (!accepting || status == RuntimeStatus::FAILED) {
                        return PostStatus::STOPPED;
                    }
                }
                queue.push_back(std::move(task));
                counters.accepted++;
                counters.queued = queue.size();
                if (options.executor && !scheduled) {
                    scheduled = true;
                    needsSchedule = true;
                }
            }

            if (needsSchedule && !schedule()) {
                return PostStatus::EXECUTOR_REJECTED;
            }
            return PostStatus::ACCEPTED;
        }

        /**
         * @brief 在无 executor 模式下由调用线程排空队列。
         * @param limit 本次最多处理的事件数。
         * @return 本次实际处理的事件数量。
         */
        std::size_t poll(const std::optional<std::size_t> &limit)
        {
            if (options.executor) {
                NOVAINFRA_THROW(std::logic_error("StateMachineRuntime::poll requires no executor"));
                return 0;
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (draining && drainThread == std::this_thread::get_id()) {
                    NOVAINFRA_THROW(
                        std::logic_error("StateMachineRuntime::poll cannot be called from a task"));
                    return 0;
                }
            }
            std::unique_lock<std::mutex> pollLock(pollMutex);
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (draining && drainThread == std::this_thread::get_id()) {
                    NOVAINFRA_THROW(
                        std::logic_error("StateMachineRuntime::poll cannot be called from a task"));
                    return 0;
                }
                if (!scheduled && !queue.empty()) {
                    scheduled = true;
                }
            }
            std::size_t processedBefore;
            {
                std::lock_guard<std::mutex> lock(mutex);
                processedBefore = static_cast<std::size_t>(counters.processed);
            }
            drain(limit);
            std::lock_guard<std::mutex> lock(mutex);
            return static_cast<std::size_t>(counters.processed - processedBefore);
        }

        /** @brief 停止接收事件，并按指定策略排空或丢弃队列。 */
        void shutdown(ShutdownMode mode) noexcept
        {
            std::deque<Task> retired;
            std::lock_guard<std::mutex> lock(mutex);
            accepting = false;
            if (mode == ShutdownMode::DISCARD) {
                counters.dropped += queue.size();
                retired.swap(queue);
                if (!draining) {
                    scheduled = false;
                }
            }
            if (status != RuntimeStatus::FAILED) {
                status = (queue.empty() && !draining) ? RuntimeStatus::STOPPED : RuntimeStatus::STOPPING;
            }
            counters.status = status;
            counters.queued = queue.size();
            space.notify_all();
            idle.notify_all();
        }

        void dispose() noexcept
        {
            shutdown(ShutdownMode::DISCARD);
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (draining && drainThread == std::this_thread::get_id()) {
                    std::terminate();
                }
            }
            std::unique_lock<std::mutex> pollLock(pollMutex);
            std::unique_lock<std::mutex> lock(mutex);
            // A delayed executor closure owns Shared and will find an empty
            // queue. Only a drain already in progress can access the machine.
            idle.wait(lock, [this] { return !draining; });
        }

        /** @brief 等待队列为空且当前 drain 任务结束。 */
        void waitIdle()
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (draining && drainThread == std::this_thread::get_id()) {
                NOVAINFRA_THROW(std::logic_error("StateMachineRuntime::waitIdle cannot run from a task"));
                return;
            }
            idle.wait(lock,
                      [this] { return queue.empty() && !draining && !scheduled && counters.active == 0; });
        }

        /** @brief 在锁内复制一份当前运行统计快照。 */
        RuntimeStats stats() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            RuntimeStats result = counters;
            result.status = status;
            result.queued = queue.size();
            result.active = counters.active;
            return result;
        }
    };

public:
    /**
     * @brief 创建状态机 Runtime。
     * @param machine 要驱动的状态机；Runtime 不拥有该对象。
     * @param options 队列、executor 和观察回调配置。
     */
    explicit StateMachineRuntime(Machine &machine, RuntimeOptions options = {})
        : state_(std::make_shared<Shared>(machine, std::move(options)))
    {
    }

    /** @brief 丢弃待处理事件并等待正在执行的事件；禁止在自身回调中析构。 */
    ~StateMachineRuntime() noexcept { state_->dispose(); }

    NOVAINFRA_DISABLE_COPY_MOVE(StateMachineRuntime)

    /**
     * @brief 从任意线程投递一个事件。
     * @param event 要复制或移动到队列中的 typed event。
     * @return 投递结果；成功时返回 ACCEPTED。
     */
    template <typename Event>
    PostStatus post(Event &&event)
    {
        return state_->post(Shared::makeTask(std::forward<Event>(event)));
    }

    /** @brief 无 executor 时在当前线程处理事件，直到队列暂时为空。 */
    std::size_t poll() { return state_->poll(std::nullopt); }
    /** @brief 无 executor 时在当前线程最多处理 maxEvents 个事件。 */
    std::size_t poll(std::size_t maxEvents) { return state_->poll(maxEvents); }
    /** @brief 停止接收事件；DRAIN 会处理已接受事件，DISCARD 会丢弃队列。 */
    void shutdown(ShutdownMode mode = ShutdownMode::DRAIN) noexcept { state_->shutdown(mode); }
    /** @brief 等待当前队列排空；不能从 Runtime 的 drain 回调中调用。 */
    void waitIdle() { state_->waitIdle(); }
    /** @brief 返回当前运行状态和累计计数的快照。 */
    RuntimeStats stats() const { return state_->stats(); }
    /** @brief 返回 Runtime 是否仍接受新事件。 */
    bool accepting() const { return stats().status == RuntimeStatus::RUNNING; }

private:
    std::shared_ptr<Shared> state_;
};

}  // 状态机命名空间
NOVAINFRA_NAMESPACE_END
