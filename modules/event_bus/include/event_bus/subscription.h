#pragma once

#include <core/export.h>
#include <core/macros.h>
#include <core/namespace.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

NOVAINFRA_NAMESPACE_BEGIN
namespace eventbus {
namespace detail {
struct Slot {
    explicit Slot(std::uint64_t value, std::function<void(const void *)> function)
        : id(value), callback(std::move(function))
    {
    }
    std::uint64_t id;
    std::function<void(const void *)> callback;
    std::atomic<bool> active{true};
};
}  // namespace detail

/**
 * @brief EventBus 订阅句柄。
 *
 * 句柄采用 RAII 语义，析构时自动取消订阅。对象不可复制但可以移动。
 */
class NOVAINFRA_LIB_PORT Subscription
{
public:
    /** @brief 创建空订阅句柄。 */
    Subscription() = default;
    /** @brief 从内部订阅槽创建句柄。 @param slot 内部订阅槽。 */
    explicit Subscription(std::shared_ptr<detail::Slot> slot) : slot_(std::move(slot)) {}
    /** @brief 析构并自动取消订阅。 */
    ~Subscription() { cancel(); }

    Subscription(const Subscription &) = delete;
    Subscription &operator=(const Subscription &) = delete;
    /** @brief 移动构造订阅句柄。 @param other 要转移的句柄。 */
    Subscription(Subscription &&other) NOVAINFRA_NOEXCEPT : slot_(std::move(other.slot_)) {}
    /**
     * @brief 移动赋值订阅句柄。
     * @param other 要转移的句柄；赋值前当前订阅会被取消。
     * @return 当前句柄。
     */
    Subscription &operator=(Subscription &&other) NOVAINFRA_NOEXCEPT
    {
        if (this != &other) {
            cancel();
            slot_ = std::move(other.slot_);
        }
        return *this;
    }

    /** @brief 取消订阅；重复调用是安全的。 */
    void cancel() NOVAINFRA_NOEXCEPT
    {
        if (slot_) {
            slot_->active.store(false, std::memory_order_release);
            slot_.reset();
        }
    }
    /** @brief 返回订阅当前是否仍有效。 */
    bool active() const NOVAINFRA_NOEXCEPT { return slot_ && slot_->active.load(std::memory_order_acquire); }
    /** @brief 返回订阅唯一 ID；无效句柄返回 0。 */
    std::uint64_t id() const NOVAINFRA_NOEXCEPT { return slot_ ? slot_->id : 0; }

private:
    std::shared_ptr<detail::Slot> slot_;
};

}  // namespace eventbus
NOVAINFRA_NAMESPACE_END
