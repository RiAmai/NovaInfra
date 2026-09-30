#pragma once

// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2016-present Gabi Melman and spdlog contributors
// SPDX-FileCopyrightText: 2026 Ariyali_
//
// Portions derived from spdlog.
// Distributed under the MIT License; see THIRD_PARTY_NOTICES.md.

// multi producer-multi consumer blocking queue.
// enqueue(..) - will block until room found to put the new message.
// enqueueNowait(..) - enqueue immediately. overruns oldest message if no
// room left.
// dequeueFor(..) - will block until the queue is not empty or timeout have
// passed.

#include <core/circular_queue.h>
#include <core/namespace.h>
#include <core/macros.h>

#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>

NOVAINFRA_NAMESPACE_BEGIN

template <typename T>
class MpmcBlockingQueue
{
public:
    using ItemType = T;
    explicit MpmcBlockingQueue(size_t maxItems) : q_(validateCapacity(maxItems)) {}

#ifndef __MINGW32__
    // try to enqueue and block if no room left
    void enqueue(T &&item)
    {
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            popCv_.wait(lock, [this] { return !this->q_.full(); });
            q_.pushBack(std::move(item));
        }
        pushCv_.notify_one();
    }

    // enqueue immediately. overrun oldest message in the queue if no room left.
    void enqueueNoWait(T &&item)
    {
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            q_.pushBack(std::move(item));
        }
        pushCv_.notify_one();
    }

    // Enqueue with an overflow guard. Returns false when the queue is full and
    // the oldest item is protected by the caller (for example, a control item).
    template <typename CanOverrun>
    bool enqueueNoWait(T &&item, CanOverrun &&canOverrun)
    {
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            if (q_.full() && !canOverrun(q_.front())) {
                return false;
            }
            q_.pushBack(std::move(item));
        }
        pushCv_.notify_one();
        return true;
    }

    void enqueueIfHaveRoom(T &&item)
    {
        bool pushed = false;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            if (!q_.full()) {
                q_.pushBack(std::move(item));
                pushed = true;
            }
        }

        if (pushed) {
            pushCv_.notify_one();
        } else {
            ++discardCounter_;
        }
    }

    // dequeue with a timeout.
    // Return true, if succeeded dequeue item, false otherwise
    bool dequeueFor(T &poppedItem, std::chrono::milliseconds waitDuration)
    {
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            if (!pushCv_.wait_for(lock, waitDuration, [this] { return !this->q_.empty(); })) {
                return false;
            }
            poppedItem = std::move(q_.front());
            q_.popFront();
        }
        popCv_.notify_one();
        return true;
    }

    // blocking dequeue without a timeout.
    void dequeue(T &poppedItem)
    {
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            pushCv_.wait(lock, [this] { return !this->q_.empty(); });
            poppedItem = std::move(q_.front());
            q_.popFront();
        }
        popCv_.notify_one();
    }

#else
    // apparently mingw deadlocks if the mutex is released before cv.notify_one(),
    // so release the mutex at the very end each function.

    // try to enqueue and block if no room left
    void enqueue(T &&item)
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        popCv_.wait(lock, [this] { return !this->q_.full(); });
        q_.pushBack(std::move(item));
        pushCv_.notify_one();
    }

    // enqueue immediately. overrun oldest message in the queue if no room left.
    void enqueueNoWait(T &&item)
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        q_.pushBack(std::move(item));
        pushCv_.notify_one();
    }

    template <typename CanOverrun>
    bool enqueueNoWait(T &&item, CanOverrun &&canOverrun)
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        if (q_.full() && !canOverrun(q_.front())) {
            return false;
        }
        q_.pushBack(std::move(item));
        pushCv_.notify_one();
        return true;
    }

    void enqueueIfHaveRoom(T &&item)
    {
        bool pushed = false;
        std::unique_lock<std::mutex> lock(queueMutex_);
        if (!q_.full()) {
            q_.pushBack(std::move(item));
            pushed = true;
        }

        if (pushed) {
            pushCv_.notify_one();
        } else {
            ++discardCounter_;
        }
    }

    // dequeue with a timeout.
    // Return true, if succeeded dequeue item, false otherwise
    bool dequeueFor(T &poppedItem, std::chrono::milliseconds waitDuration)
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        if (!pushCv_.wait_for(lock, waitDuration, [this] { return !this->q_.empty(); })) {
            return false;
        }
        poppedItem = std::move(q_.front());
        q_.popFront();
        popCv_.notify_one();
        return true;
    }

    // blocking dequeue without a timeout.
    void dequeue(T &poppedItem)
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        pushCv_.wait(lock, [this] { return !this->q_.empty(); });
        poppedItem = std::move(q_.front());
        q_.popFront();
        popCv_.notify_one();
    }

#endif

    size_t overrunCounter()
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        return q_.overrunCounter();
    }

    size_t discardCounter() const { return discardCounter_.load(std::memory_order_relaxed); }

    size_t size()
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        return q_.size();
    }

    void resetOverrunCounter()
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        q_.resetOverrunCounter();
    }

    void resetDiscardCounter() { discardCounter_.store(0, std::memory_order_relaxed); }

private:
    static size_t validateCapacity(size_t maxItems)
    {
        if (maxItems == 0 || maxItems == std::numeric_limits<size_t>::max()) {
            NOVAINFRA_THROW(
                std::invalid_argument("MpmcBlockingQueue capacity must be between 1 and SIZE_MAX - 1"));
        }
        return maxItems;
    }

    std::mutex queueMutex_;
    std::condition_variable pushCv_;
    std::condition_variable popCv_;
    CircularQueue<T> q_;
    std::atomic<size_t> discardCounter_{0};
};

NOVAINFRA_NAMESPACE_END
