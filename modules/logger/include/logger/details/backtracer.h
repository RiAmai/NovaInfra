#pragma once

#include <logger/details/log_msg_buffer.h>

#include <core/namespace.h>
#include <core/circular_queue.h>
#include <core/export.h>
#include <atomic>
#include <mutex>
#include <vector>

NOVAINFRA_NAMESPACE_BEGIN
namespace log::details {

class NOVAINFRA_LIB_PORT BackTracer
{
public:
    BackTracer() = default;
    BackTracer(const BackTracer &other)
    {
        std::lock_guard<std::mutex> lock(other.mutex_);
        enabled_.store(other.enabled_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        messages_ = other.messages_;
    }
    BackTracer(BackTracer &&other) NOVAINFRA_NOEXCEPT
    {
        std::lock_guard<std::mutex> lock(other.mutex_);
        enabled_.store(other.enabled_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        messages_ = std::move(other.messages_);
        other.enabled_.store(false, std::memory_order_relaxed);
    }
    BackTracer &operator=(const BackTracer &other)
    {
        if (this == &other) {
            return *this;
        }
        std::scoped_lock lock(mutex_, other.mutex_);
        enabled_.store(other.enabled_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        messages_ = other.messages_;
        return *this;
    }
    BackTracer &operator=(BackTracer &&other) NOVAINFRA_NOEXCEPT
    {
        if (this == &other) {
            return *this;
        }
        std::scoped_lock lock(mutex_, other.mutex_);
        enabled_.store(other.enabled_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        messages_ = std::move(other.messages_);
        other.enabled_.store(false, std::memory_order_relaxed);
        return *this;
    }

    void enable(std::size_t size)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_.store(size != 0, std::memory_order_relaxed);
        messages_ = size == 0 ? CircularQueue<LogMsgBuffer>{} : CircularQueue<LogMsgBuffer>(size);
    }
    void disable()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_.store(false, std::memory_order_relaxed);
    }
    bool enabled() const { return enabled_.load(std::memory_order_relaxed); }
    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_.empty();
    }
    void pushBack(const LogMsg &msg)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (enabled_.load(std::memory_order_relaxed)) {
            messages_.pushBack(LogMsgBuffer(msg));
        }
    }

    std::vector<LogMsgBuffer> drain()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<LogMsgBuffer> result;
        result.reserve(messages_.size());
        while (!messages_.empty()) {
            result.push_back(std::move(messages_.front()));
            messages_.popFront();
        }
        return result;
    }

private:
    mutable std::mutex mutex_;
    std::atomic<bool> enabled_{false};
    CircularQueue<LogMsgBuffer> messages_;
};

}  // namespace log::details
NOVAINFRA_NAMESPACE_END
