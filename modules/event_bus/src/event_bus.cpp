#include <event_bus/event_bus.h>

#include <condition_variable>
#include <algorithm>
#include <cstdio>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>

NOVAINFRA_NAMESPACE_BEGIN
namespace eventbus {
namespace detail {

namespace {
struct DispatchScope {
    explicit DispatchScope(const void *owner) noexcept : owner(owner), previous(current) { current = this; }
    ~DispatchScope() { current = previous; }

    static bool contains(const void *owner) noexcept
    {
        for (auto scope = current; scope; scope = scope->previous) {
            if (scope->owner == owner) {
                return true;
            }
        }
        return false;
    }

    const void *owner;
    const DispatchScope *previous;
    static thread_local const DispatchScope *current;
};

thread_local const DispatchScope *DispatchScope::current = nullptr;
}

struct State::Impl {
    struct Envelope {
        std::shared_ptr<const void> payload;
        std::vector<std::shared_ptr<Slot>> slots;
    };
    struct Channel {
        std::vector<std::shared_ptr<Slot>> slots;
        std::deque<Envelope> queue;
        bool scheduled = false;
    };

    explicit Impl(EventBusOptions value) : options(std::move(value))
    {
        if (options.queueCapacity == 0) {
            NOVAINFRA_THROW(std::invalid_argument("EventBus queue capacity must be greater than zero"));
        }
    }

    EventBusOptions options;
    mutable std::mutex mutex;
    std::condition_variable capacity;
    std::condition_variable idle;
    std::unordered_map<std::type_index, std::shared_ptr<Channel>> channels;
    std::uint64_t nextId = 1;
    EventBusStats stats;
    bool accepting = true;
};

State::State(EventBusOptions options) : impl_(std::make_shared<Impl>(std::move(options))) {}

std::shared_ptr<Slot> State::subscribe(std::type_index type, std::function<void(const void *)> callback)
{
    auto slot = std::make_shared<Slot>(0, std::move(callback));
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->accepting) {
        NOVAINFRA_THROW(std::runtime_error("EventBus is shut down"));
    }
    auto &channel = impl_->channels[type];
    if (!channel) {
        channel = std::make_shared<Impl::Channel>();
    }
    auto &slots = channel->slots;
    slots.erase(std::remove_if(
                    slots.begin(), slots.end(),
                    [](const auto &existing) { return !existing->active.load(std::memory_order_acquire); }),
                slots.end());
    slot->id = impl_->nextId++;
    channel->slots.push_back(slot);
    return slot;
}

DispatchReport State::publish(std::type_index type, const void *event)
{
    std::vector<std::shared_ptr<Slot>> slots;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->accepting) {
            NOVAINFRA_THROW(std::runtime_error("EventBus is shut down"));
        }
        auto found = impl_->channels.find(type);
        if (found == impl_->channels.end()) {
            return {};
        }
        auto &registered = found->second->slots;
        registered.erase(
            std::remove_if(registered.begin(), registered.end(),
                           [](const auto &slot) { return !slot->active.load(std::memory_order_acquire); }),
            registered.end());
        slots = registered;
    }

    DispatchReport report;
    for (const auto &slot : slots) {
        if (!slot->active.load(std::memory_order_acquire)) {
            continue;
        }
        DispatchScope scope(impl_.get());
        NOVAINFRA_TRY
        {
            slot->callback(event);
            ++report.delivered;
        }
        NOVAINFRA_CATCH_ALL_BODY({
            ++report.failed;
            CallbackError error{type, slot->id, std::current_exception()};
            const auto handler = impl_->options.onError;
            if (handler) {
                NOVAINFRA_TRY
                {
                    handler(error);
                }
                NOVAINFRA_CATCH_ALL_BODY({})
            } else {
                std::fprintf(stderr, "[EventBus] callback %llu failed\n",
                             static_cast<unsigned long long>(slot->id));
            }
        })
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->stats.delivered += report.delivered;
    impl_->stats.failed += report.failed;
    return report;
}

AsyncPublishStatus State::enqueue(std::type_index type, std::shared_ptr<const void> event, bool mayBlock)
{
    std::shared_ptr<Impl::Channel> channel;
    bool schedule = false;
    {
        std::unique_lock<std::mutex> lock(impl_->mutex);
        if (!impl_->accepting) {
            return AsyncPublishStatus::STOPPED;
        }
        if (!impl_->options.executor) {
            return AsyncPublishStatus::NO_EXECUTOR;
        }
        auto &entry = impl_->channels[type];
        if (!entry) {
            entry = std::make_shared<Impl::Channel>();
        }
        channel = entry;
        std::vector<std::shared_ptr<Slot>> slots;
        channel->slots.erase(
            std::remove_if(channel->slots.begin(), channel->slots.end(),
                           [](const auto &slot) { return !slot->active.load(std::memory_order_acquire); }),
            channel->slots.end());
        for (const auto &slot : channel->slots) {
            if (slot->active.load(std::memory_order_acquire)) {
                slots.push_back(slot);
            }
        }
        if (slots.empty()) {
            return AsyncPublishStatus::NO_SUBSCRIBERS;
        }

        if (impl_->stats.queued >= impl_->options.queueCapacity) {
            if (mayBlock && impl_->options.queueFullPolicy == QueueFullPolicy::BLOCK) {
                if (DispatchScope::contains(impl_.get())) {
                    NOVAINFRA_THROW(std::logic_error("EventBus callback cannot block on a full queue"));
                }
                impl_->capacity.wait(lock, [this] {
                    return !impl_->accepting || impl_->stats.queued < impl_->options.queueCapacity;
                });
                if (!impl_->accepting) {
                    return AsyncPublishStatus::STOPPED;
                }
            } else if (impl_->options.queueFullPolicy == QueueFullPolicy::DROP_NEWEST) {
                ++impl_->stats.dropped;
                return AsyncPublishStatus::DROPPED;
            } else {
                return AsyncPublishStatus::QUEUE_FULL;
            }
        }

        channel->queue.push_back({std::move(event), std::move(slots)});
        ++impl_->stats.accepted;
        ++impl_->stats.queued;
        if (!channel->scheduled) {
            channel->scheduled = true;
            schedule = true;
        }
    }

    if (!schedule) {
        return AsyncPublishStatus::ACCEPTED;
    }

    auto weakImpl = std::weak_ptr<Impl>(impl_);
    bool accepted = false;
    NOVAINFRA_TRY
    {
        accepted = impl_->options.executor([weakImpl, type] {
            if (auto state = weakImpl.lock()) {
                State::Impl::Envelope item;
                for (;;) {
                    {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        auto found = state->channels.find(type);
                        if (found == state->channels.end() || found->second->queue.empty()) {
                            if (found != state->channels.end()) {
                                found->second->scheduled = false;
                            }
                            state->capacity.notify_all();
                            state->idle.notify_all();
                            return;
                        }
                        item = std::move(found->second->queue.front());
                        found->second->queue.pop_front();
                        --state->stats.queued;
                        ++state->stats.active;
                    }
                    state->capacity.notify_all();

                    for (const auto &slot : item.slots) {
                        if (!slot->active.load(std::memory_order_acquire)) {
                            continue;
                        }
                        DispatchScope scope(state.get());
                        NOVAINFRA_TRY
                        {
                            slot->callback(item.payload.get());
                            std::lock_guard<std::mutex> lock(state->mutex);
                            ++state->stats.delivered;
                        }
                        NOVAINFRA_CATCH_ALL_BODY({
                            {
                                std::lock_guard<std::mutex> lock(state->mutex);
                                ++state->stats.failed;
                            }
                            CallbackError error{type, slot->id, std::current_exception()};
                            const auto handler = state->options.onError;
                            if (handler) {
                                NOVAINFRA_TRY
                                {
                                    handler(error);
                                }
                                NOVAINFRA_CATCH_ALL_BODY({})
                            } else {
                                std::fprintf(stderr, "[EventBus] callback %llu failed\n",
                                             static_cast<unsigned long long>(slot->id));
                            }
                        })
                    }
                    {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        --state->stats.active;
                        if (state->stats.queued == 0 && state->stats.active == 0) {
                            state->idle.notify_all();
                        }
                    }
                }
            }
        });
    }
    NOVAINFRA_CATCH_ALL_BODY({ accepted = false; })

    if (!accepted) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        auto found = impl_->channels.find(type);
        if (found != impl_->channels.end()) {
            impl_->stats.dropped += found->second->queue.size();
            impl_->stats.queued -= found->second->queue.size();
            found->second->queue.clear();
            found->second->scheduled = false;
        }
        ++impl_->stats.executorFailures;
        impl_->capacity.notify_all();
        impl_->idle.notify_all();
        return AsyncPublishStatus::EXECUTOR_REJECTED;
    }
    return AsyncPublishStatus::ACCEPTED;
}

void State::shutdown(ShutdownMode mode)
{
    if (mode == ShutdownMode::DRAIN && DispatchScope::contains(impl_.get())) {
        NOVAINFRA_THROW(std::logic_error("EventBus shutdown cannot drain from a callback"));
    }
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->accepting = false;
    impl_->stats.accepting = false;
    if (mode == ShutdownMode::DISCARD) {
        for (auto &entry : impl_->channels) {
            impl_->stats.dropped += entry.second->queue.size();
            entry.second->queue.clear();
        }
        impl_->stats.queued = 0;
        impl_->capacity.notify_all();
        impl_->idle.notify_all();
        return;
    }
    impl_->capacity.notify_all();
    impl_->idle.wait(lock, [this] { return impl_->stats.queued == 0 && impl_->stats.active == 0; });
}

bool State::accepting() const noexcept
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->accepting;
}

EventBusStats State::stats() const noexcept
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->stats;
}

}  // namespace detail

EventBus::EventBus(EventBusOptions options) : state_(std::make_shared<detail::State>(std::move(options))) {}
EventBus::~EventBus() noexcept
{
    if (state_) {
        NOVAINFRA_TRY
        {
            state_->shutdown(ShutdownMode::DISCARD);
        }
        NOVAINFRA_CATCH_ALL_BODY({})
    }
}
void EventBus::shutdown(ShutdownMode mode)
{
    state_->shutdown(mode);
}
bool EventBus::accepting() const noexcept
{
    return state_->accepting();
}
EventBusStats EventBus::stats() const noexcept
{
    return state_->stats();
}

}  // namespace eventbus
NOVAINFRA_NAMESPACE_END
