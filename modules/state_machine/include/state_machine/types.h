#pragma once

#include <core/namespace.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <typeindex>

NOVAINFRA_NAMESPACE_BEGIN
namespace statemachine {

/** @brief 由 Runtime 调度的、无参数任务。 */
using EventTask = std::function<void()>;
/** @brief 外部事件执行器；返回 true 表示接受任务。 */
using Executor = std::function<bool(EventTask)>;

/** @brief 队列达到容量上限时的处理方式。 */
enum class QueueFullPolicy {
    /** @brief 阻塞投递线程，直到队列出现空间。 */
    BLOCK,
    /** @brief 立即返回队列已满。 */
    REJECT,
    /** @brief 丢弃当前新事件并立即返回。 */
    DROP_NEWEST
};

/** @brief 投递事件的结果。 */
enum class PostStatus {
    /** @brief 事件已进入队列。 */
    ACCEPTED,
    /** @brief 队列已满且采用拒绝策略。 */
    QUEUE_FULL,
    /** @brief 事件因队列策略被丢弃。 */
    DROPPED,
    /** @brief Runtime 已停止接收事件。 */
    STOPPED,
    /** @brief 外部 executor 拒绝了 drain 任务。 */
    EXECUTOR_REJECTED
};

/** @brief Runtime 的生命周期状态。 */
enum class RuntimeStatus {
    /** @brief 正常接收并处理事件。 */
    RUNNING,
    /** @brief 已停止接收新事件，正在处理已接受事件。 */
    STOPPING,
    /** @brief 已停止且没有待处理事件。 */
    STOPPED,
    /** @brief 发生不可继续处理的异常，已停止接收事件。 */
    FAILED
};

/** @brief Runtime 关闭时对尚未处理事件的处理方式。 */
enum class ShutdownMode {
    /** @brief 处理已经接受的事件后停止。 */
    DRAIN,
    /** @brief 直接丢弃尚未开始处理的事件后停止。 */
    DISCARD
};

/** @brief 单个事件的处理结果。 */
enum class ProcessStatus {
    /** @brief 当前状态图匹配并处理了事件。 */
    HANDLED,
    /** @brief 当前状态图没有匹配转移。 */
    UNHANDLED,
    /** @brief guard 或 action 执行失败。 */
    FAILED
};

/** @brief 已处理事件的诊断结果。 */
struct ProcessReport {
    /** @brief 事件是否被处理、未匹配或执行失败。 */
    ProcessStatus status = ProcessStatus::UNHANDLED;
    /** @brief 事件的运行时类型信息。 */
    std::type_index eventType{typeid(void)};
    /** @brief 处理失败时保存的异常对象。 */
    std::exception_ptr exception;
};

/** @brief Runtime 错误回调接收的诊断信息。 */
struct RuntimeError {
    /** @brief 发生错误的事件类型；调度器错误使用 void。 */
    std::type_index eventType{typeid(void)};
    /** @brief 回调或执行器抛出的异常。 */
    std::exception_ptr exception;
};

/** @brief Runtime 处理异常的回调；回调异常会被 Runtime 吞掉。 */
using ErrorHandler = std::function<void(const RuntimeError &)>;
/** @brief 未找到匹配转移时调用的回调。 */
using UnhandledEventHandler = std::function<void(std::type_index)>;
/** @brief 每个事件处理完成后调用的观察回调。 */
using ProcessedEventHandler = std::function<void(const ProcessReport &)>;

/** @brief Runtime 的队列、执行器和观察回调配置。 */
struct RuntimeOptions {
    /** @brief 等待队列容量，不包含当前正在执行的事件。 */
    std::size_t capacity = 1024;
    /** @brief 队列满时阻塞、拒绝或丢弃新事件。 */
    QueueFullPolicy queueFullPolicy = QueueFullPolicy::REJECT;
    /** @brief 接收 drain 任务的外部执行器；为空时使用 poll()。 */
    Executor executor;
    /** @brief 事件处理或执行器失败时调用的回调。 */
    ErrorHandler onError;
    /** @brief 未匹配事件的观察回调。 */
    UnhandledEventHandler onUnhandledEvent;
    /** @brief 事件处理结果观察回调。 */
    ProcessedEventHandler onProcessed;
};

/** @brief Runtime 的累计计数和当前运行状态快照。 */
struct RuntimeStats {
    /** @brief 当前 Runtime 状态。 */
    RuntimeStatus status = RuntimeStatus::STOPPED;
    /** @brief 已接受进入队列的事件数量。 */
    std::uint64_t accepted = 0;
    /** @brief 已开始处理的事件数量。 */
    std::uint64_t processed = 0;
    /** @brief 成功匹配转移的事件数量。 */
    std::uint64_t handled = 0;
    /** @brief 没有匹配转移的事件数量。 */
    std::uint64_t unhandled = 0;
    /** @brief 处理失败的事件数量。 */
    std::uint64_t failed = 0;
    /** @brief 因队列策略或失败清理而丢弃的事件数量。 */
    std::uint64_t dropped = 0;
    /** @brief 外部执行器拒绝 drain 任务的次数。 */
    std::uint64_t executorFailures = 0;
    /** @brief 当前等待队列中的事件数量。 */
    std::size_t queued = 0;
    /** @brief 当前正在执行的事件数量，最多为 1。 */
    std::size_t active = 0;
};

}  // 状态机命名空间
NOVAINFRA_NAMESPACE_END
