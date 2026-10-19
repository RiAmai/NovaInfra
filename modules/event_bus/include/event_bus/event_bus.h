#pragma once

#include <core/export.h>
#include <core/macros.h>
#include <event_bus/subscription.h>
#include <event_bus/types.h>

#include <functional>
#include <memory>
#include <type_traits>
#include <typeindex>
#include <utility>

NOVAINFRA_NAMESPACE_BEGIN
namespace eventbus {
namespace detail {
class NOVAINFRA_LIB_PORT State
{
public:
    explicit State(EventBusOptions options);
    std::shared_ptr<Slot> subscribe(std::type_index type, std::function<void(const void *)> callback);
    DispatchReport publish(std::type_index type, const void *event);
    AsyncPublishStatus enqueue(std::type_index type, std::shared_ptr<const void> event, bool mayBlock);
    void shutdown(ShutdownMode mode);
    bool accepting() const noexcept;
    EventBusStats stats() const noexcept;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}  // namespace detail

/**
 * @brief 类型安全的进程内事件总线。
 *
 * 同步事件在 publish() 调用线程中执行；异步事件通过用户提供的 Executor
 * 调度，并按事件类型维护 FIFO 队列。
 */
class NOVAINFRA_LIB_PORT EventBus
{
public:
    /**
     * @brief 创建事件总线。
     * @param options 队列容量、队列满策略、异步执行器和错误处理器。
     */
    explicit EventBus(EventBusOptions options = {});
    /** @brief 关闭总线并释放内部资源。 */
    ~EventBus() noexcept;
    NOVAINFRA_DISABLE_COPY_MOVE(EventBus)

    /**
     * @brief 注册一个类型安全的事件订阅。
     * @tparam Event 事件类型。
     * @tparam Callback 可调用对象类型，必须接受 const Event&。
     * @param callback 收到事件时调用的回调。
     * @return RAII 订阅句柄；句柄析构或取消后不再接收新回调。
     */
    template <typename Event, typename Callback>
    Subscription subscribe(Callback &&callback)
    {
        const auto state = state_;
        using T = std::decay_t<Event>;
        static_assert(std::is_invocable_r_v<void, Callback &, const T &>,
                      "EventBus callback must accept const Event&");
        std::function<void(const T &)> typed(std::forward<Callback>(callback));
        return Subscription(state->subscribe(typeid(T), [function = std::move(typed)](const void *value) {
            function(*static_cast<const T *>(value));
        }));
    }

    /**
     * @brief 同步发布一个事件。
     * @tparam Event 事件类型。
     * @param event 要发布的事件；回调在当前线程同步执行。
     * @return 投递数量和失败回调数量。
     * @throws std::runtime_error 总线已关闭时抛出。
     */
    template <typename Event>
    DispatchReport publish(const Event &event)
    {
        // A synchronous callback may destroy the owning EventBus.
        const auto state = state_;
        return state->publish(typeid(std::decay_t<Event>), &event);
    }

    /**
     * @brief 发布一个可能阻塞的异步事件。
     * @tparam Event 事件类型。
     * @param event 要发布的事件；右值会移动到异步队列。
     * @return 异步发布结果。
     * @throws std::logic_error 回调中因队列满而尝试阻塞时抛出。
     */
    template <typename Event>
    AsyncPublishStatus publishAsync(Event &&event)
    {
        const auto state = state_;
        using T = std::decay_t<Event>;
        auto payload = std::make_shared<T>(std::forward<Event>(event));
        return state->enqueue(typeid(T), std::move(payload), true);
    }

    /**
     * @brief 非阻塞发布一个异步事件。
     * @tparam Event 事件类型。
     * @param event 要发布的事件；右值会移动到异步队列。
     * @return 异步发布结果；队列无空间时返回 QUEUE_FULL 或 DROPPED。
     */
    template <typename Event>
    AsyncPublishStatus tryPublishAsync(Event &&event)
    {
        const auto state = state_;
        using T = std::decay_t<Event>;
        auto payload = std::make_shared<T>(std::forward<Event>(event));
        return state->enqueue(typeid(T), std::move(payload), false);
    }

    /**
     * @brief 停止接受新事件并关闭总线。
     * @param mode DRAIN 执行已接受事件，DISCARD 丢弃尚未开始的事件。
     * @throws std::logic_error 从回调中请求 DRAIN 关闭时抛出。
     */
    void shutdown(ShutdownMode mode = ShutdownMode::DRAIN);
    /** @brief 返回总线当前是否接受新事件。 */
    bool accepting() const noexcept;
    /** @brief 返回总线统计快照。 */
    EventBusStats stats() const noexcept;

private:
    std::shared_ptr<detail::State> state_;
};

}  // namespace eventbus
NOVAINFRA_NAMESPACE_END
