#pragma once

// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2016-present Gabi Melman and spdlog contributors
// SPDX-FileCopyrightText: 2026 Ariyali_
//
// Portions derived from spdlog.
// Distributed under the MIT License; see THIRD_PARTY_NOTICES.md.

#include <core/namespace.h>
#include <core/macros.h>

#include <cassert>
#include <vector>

// circular quque view of std::vector.
NOVAINFRA_NAMESPACE_BEGIN

template <typename T>
class CircularQueue
{
    size_t maxItems_ = 0;
    typename std::vector<T>::size_type head_ = 0;
    typename std::vector<T>::size_type tail_ = 0;
    size_t overrunCnt_ = 0;
    std::vector<T> v_;

public:
    using ValueType = T;

    // empty ctor - create a disabled queue with no elements allocated at all
    CircularQueue() = default;

    explicit CircularQueue(size_t maxItems)
        : maxItems_(maxItems + 1),  // one item is reserved as marker for full queue
          v_(maxItems_)
    {
    }
    CircularQueue(const CircularQueue &other) = default;
    CircularQueue &operator=(const CircularQueue &ohter) = default;

    // move cannot be default,
    // since we need to reset head_, tail_, etc to zero in the moved object
    CircularQueue(CircularQueue &&other) NOVAINFRA_NOEXCEPT { copyMoveable(std::move(other)); }
    CircularQueue &operator=(CircularQueue &&other) NOVAINFRA_NOEXCEPT
    {
        copyMoveable(std::move(other));
        return *this;
    }

    // push back, overrun (oldest) item if no room left
    void pushBack(T &&item)
    {
        if (maxItems_ > 0) {
            v_[tail_] = std::move(item);
            tail_ = (tail_ + 1) % maxItems_;

            if (tail_ == head_)  // overrun last item if full
            {
                v_[head_] = T{};
                head_ = (head_ + 1) % maxItems_;
                ++overrunCnt_;
            }
        }
    }

    // Return reference to the front item.
    // If there are no elements in the container, the behavior is undefined.
    const T &front() const { return v_[head_]; }

    T &front() { return v_[head_]; }

    // Return number of elements actually stored
    size_t size() const
    {
        if (tail_ >= head_) {
            return tail_ - head_;
        } else {
            return maxItems_ - (head_ - tail_);
        }
    }

    // Return const reference to item by index.
    // If index is out of range 0…size()-1, the behavior is undefined.
    const T &at(size_t i) const
    {
        assert(i < size());
        return v_[(head_ + i) % maxItems_];
    }

    // Pop item from front.
    // If there are no elements in the container, the behavior is undefined.
    void popFront()
    {
        v_[head_] = T{};
        head_ = (head_ + 1) % maxItems_;
    }

    bool empty() const { return tail_ == head_; }

    bool full() const
    {
        // head is ahead of the tail by 1
        if (maxItems_ > 0) {
            return ((tail_ + 1) % maxItems_) == head_;
        }
        return false;
    }

    size_t overrunCounter() const { return overrunCnt_; }

    void resetOverrunCounter() { overrunCnt_ = 0; }

private:
    // copy from other&& and reset it to disabled state
    void copyMoveable(CircularQueue &&other) NOVAINFRA_NOEXCEPT
    {
        maxItems_ = other.maxItems_;
        head_ = other.head_;
        tail_ = other.tail_;
        overrunCnt_ = other.overrunCnt_;
        v_ = std::move(other.v_);

        // put &&other in disabled, but valid state
        other.maxItems_ = 0;
        other.head_ = other.tail_ = 0;
        other.overrunCnt_ = 0;
    }
};

NOVAINFRA_NAMESPACE_END  // namespace novainfra
