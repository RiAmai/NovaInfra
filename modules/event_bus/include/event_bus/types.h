#pragma once

#include <core/namespace.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <typeindex>

NOVAINFRA_NAMESPACE_BEGIN
namespace eventbus {

/** @brief 异步事件队列已满时的处理策略。 */
enum class QueueFullPolicy {
    /** @brief 阻塞提交线程，直到队列出现空间。 */
    BLOCK,
    /** @brief 立即返回 QUEUE_FULL。 */
    REJECT,
    /** @brief 丢弃当前提交的新事件并返回 DROPPED。 */
    DROP_NEWEST
};

/** @brief 异步发布事件的结果。 */
enum class AsyncPublishStatus {
    /** @brief 事件已接受并进入调度流程。 */
    ACCEPTED,
    /** @brief 没有该事件类型的订阅者。 */
    NO_SUBSCRIBERS,
    /** @brief 没有配置异步执行器。 */
    NO_EXECUTOR,
    /** @brief 非阻塞提交时队列已满。 */
    QUEUE_FULL,
    /** @brief 事件因队列策略被丢弃。 */
    DROPPED,
    /** @brief EventBus 已停止接受新事件。 */
    STOPPED,
    /** @brief 外部执行器拒绝了调度任务。 */
    EXECUTOR_REJECTED
};

/** @brief EventBus 关闭时对已排队事件的处理方式。 */
enum class ShutdownMode {
    /** @brief 执行完已接受的事件后关闭。 */
    DRAIN,
    /** @brief 丢弃尚未开始执行的事件后关闭。 */
    DISCARD
};

/** @brief 无参数的异步事件任务。 */
using EventTask = std::function<void()>;
/** @brief 外部异步执行器；返回 true 表示接受任务。 */
using Executor = std::function<bool(EventTask)>;

/** @brief 订阅回调异常的诊断信息。 */
struct CallbackError {
    /** @brief 发生异常的事件类型。 */
    std::type_index eventType{typeid(void)};
    /** @brief 发生异常的订阅 ID。 */
    std::uint64_t subscriptionId = 0;
    /** @brief 回调抛出的异常。 */
    std::exception_ptr exception;
};
/** @brief EventBus 回调异常处理器。 */
using ErrorHandler = std::function<void(const CallbackError &)>;

/** @brief EventBus 的队列、执行器和错误处理配置。 */
struct EventBusOptions {
    /** @brief 所有事件类型共享的异步等待队列容量，不包含正在执行的回调。 */
    std::size_t queueCapacity = 1024;
    /** @brief 队列已满时的处理策略。 */
    QueueFullPolicy queueFullPolicy = QueueFullPolicy::BLOCK;
    /** @brief 异步事件使用的外部执行器；为空时不能异步发布。 */
    Executor executor;
    /** @brief 订阅回调抛出异常时调用的处理器。 */
    ErrorHandler onError;
};

/** @brief 同步发布一次事件的投递统计。 */
struct DispatchReport {
    /** @brief 成功执行的订阅回调数量。 */
    std::size_t delivered = 0;
    /** @brief 抛出异常的订阅回调数量。 */
    std::size_t failed = 0;
};

/** @brief EventBus 的运行统计快照。 */
struct EventBusStats {
    /** @brief 已接受的异步事件数量。 */
    std::uint64_t accepted = 0;
    /** @brief 已投递给订阅者的事件数量。 */
    std::uint64_t delivered = 0;
    /** @brief 被丢弃的事件数量。 */
    std::uint64_t dropped = 0;
    /** @brief 执行失败的回调数量。 */
    std::uint64_t failed = 0;
    /** @brief 外部执行器拒绝调度的次数。 */
    std::uint64_t executorFailures = 0;
    /** @brief 当前排队事件数量。 */
    std::size_t queued = 0;
    /** @brief 当前正在执行的事件数量。 */
    std::size_t active = 0;
    /** @brief 是否仍接受新事件。 */
    bool accepting = true;
};

}  // namespace eventbus
NOVAINFRA_NAMESPACE_END
