#include <event_bus/event_bus.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
struct Number {
    int value = 0;
};
struct OtherNumber {
    int value = 0;
};
}

using novainfra::eventbus::AsyncPublishStatus;
using novainfra::eventbus::EventBus;
using novainfra::eventbus::EventBusOptions;
using novainfra::eventbus::QueueFullPolicy;
using novainfra::eventbus::ShutdownMode;

TEST(EventBus, PublishesTypedEventsInRegistrationOrder)
{
    EventBus bus;
    std::vector<int> values;
    auto first = bus.subscribe<Number>([&](const Number &event) { values.push_back(event.value); });
    auto second = bus.subscribe<Number>([&](const Number &event) { values.push_back(event.value + 1); });
    auto report = bus.publish(Number{41});
    ASSERT_EQ(report.delivered, 2U);
    EXPECT_EQ(values, (std::vector<int>{41, 42}));
    EXPECT_TRUE(first.active());
    EXPECT_TRUE(second.active());
}

TEST(EventBus, SubscriptionIsRaIIAndCancellationIsSafe)
{
    EventBus bus;
    int count = 0;
    auto subscription = bus.subscribe<Number>([&](const Number &) { ++count; });
    subscription.cancel();
    subscription.cancel();
    bus.publish(Number{1});
    EXPECT_EQ(count, 0);
    EXPECT_FALSE(subscription.active());
}

TEST(EventBus, ReentrantPublishPreservesDispatchContext)
{
    EventBus bus;
    int calls = 0;
    auto subscription = bus.subscribe<Number>([&](const Number &event) {
        ++calls;
        if (event.value == 1) {
            bus.publish(Number{2});
        }
    });
    bus.publish(Number{1});
    EXPECT_EQ(calls, 2);
}

TEST(EventBus, CallbackFailureDoesNotStopOtherSubscribers)
{
    EventBusOptions options;
    std::promise<void> reported;
    options.onError = [&](const auto &error) {
        EXPECT_EQ(error.eventType, std::type_index(typeid(Number)));
        reported.set_value();
    };
    EventBus bus(options);
    auto failing = bus.subscribe<Number>([](const Number &) { throw std::runtime_error("failure"); });
    int count = 0;
    auto succeeding = bus.subscribe<Number>([&](const Number &) { ++count; });
    auto report = bus.publish(Number{1});
    EXPECT_EQ(report.failed, 1U);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(reported.get_future().wait_for(std::chrono::seconds(1)), std::future_status::ready);
}

TEST(EventBus, AsyncDispatchUsesExecutorAndPreservesTypeOrder)
{
    std::mutex mutex;
    std::vector<int> values;
    std::vector<std::function<void()>> tasks;
    EventBusOptions options;
    options.executor = [&](std::function<void()> task) {
        std::lock_guard<std::mutex> lock(mutex);
        tasks.push_back(std::move(task));
        return true;
    };
    EventBus bus(options);
    auto subscription = bus.subscribe<Number>([&](const Number &event) {
        std::lock_guard<std::mutex> lock(mutex);
        values.push_back(event.value);
    });
    EXPECT_EQ(bus.publishAsync(Number{1}), AsyncPublishStatus::ACCEPTED);
    EXPECT_EQ(bus.publishAsync(Number{2}), AsyncPublishStatus::ACCEPTED);
    ASSERT_EQ(tasks.size(), 1U);
    auto task = std::move(tasks.front());
    task();
    EXPECT_EQ(values, (std::vector<int>{1, 2}));
}

TEST(EventBus, TryPublishReportsFullQueue)
{
    std::promise<void> release;
    auto gate = release.get_future().share();
    EventBusOptions options;
    options.queueCapacity = 1;
    options.executor = [&](std::function<void()> task) {
        std::thread([task = std::move(task)]() mutable { task(); }).detach();
        return true;
    };
    EventBus bus(options);
    auto subscription = bus.subscribe<Number>([gate](const Number &) { gate.wait(); });
    EXPECT_EQ(bus.publishAsync(Number{1}), AsyncPublishStatus::ACCEPTED);
    while (bus.stats().active == 0) {
        std::this_thread::yield();
    }
    EXPECT_EQ(bus.publishAsync(Number{2}), AsyncPublishStatus::ACCEPTED);
    EXPECT_EQ(bus.tryPublishAsync(Number{3}), AsyncPublishStatus::QUEUE_FULL);
    release.set_value();
    bus.shutdown();
}

TEST(EventBus, QueueCapacityIsSharedAcrossEventTypes)
{
    std::vector<std::function<void()>> tasks;
    EventBusOptions options;
    options.queueCapacity = 1;
    options.queueFullPolicy = QueueFullPolicy::REJECT;
    options.executor = [&tasks](std::function<void()> task) {
        tasks.push_back(std::move(task));
        return true;
    };
    EventBus bus(options);
    auto numberSubscription = bus.subscribe<Number>([](const Number &) {});
    auto otherSubscription = bus.subscribe<OtherNumber>([](const OtherNumber &) {});

    EXPECT_EQ(bus.publishAsync(Number{1}), AsyncPublishStatus::ACCEPTED);
    EXPECT_EQ(bus.publishAsync(OtherNumber{2}), AsyncPublishStatus::QUEUE_FULL);
    ASSERT_EQ(tasks.size(), 1U);
    tasks.front()();
    EXPECT_EQ(bus.stats().queued, 0U);
}

TEST(EventBus, ShutdownRejectsNewEvents)
{
    EventBus bus;
    bus.shutdown();
    EXPECT_FALSE(bus.accepting());
    EXPECT_EQ(bus.publishAsync(Number{1}), AsyncPublishStatus::STOPPED);
}

TEST(EventBus, ShutdownRejectsSynchronousPublish)
{
    EventBus bus;
    int count = 0;
    auto subscription = bus.subscribe<Number>([&](const Number &) { ++count; });
    bus.shutdown();
    EXPECT_THROW(bus.publish(Number{1}), std::runtime_error);
    EXPECT_EQ(count, 0);
}

TEST(EventBus, NestedCrossBusDrainShutdownFailsFast)
{
    std::promise<void> callbackFinished;
    auto finished = callbackFinished.get_future();

    EventBusOptions options;
    options.executor = [&](std::function<void()> task) {
        std::thread([task = std::move(task)]() mutable { task(); }).detach();
        return true;
    };
    EventBus busA(options);
    EventBus busB;
    auto bSubscription = busB.subscribe<Number>([&](const Number &) {
        EXPECT_THROW(busA.shutdown(), std::logic_error);
        callbackFinished.set_value();
    });
    auto aSubscription = busA.subscribe<Number>([&](const Number &event) { busB.publish(event); });
    EXPECT_EQ(busA.publishAsync(Number{1}), AsyncPublishStatus::ACCEPTED);
    EXPECT_EQ(finished.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    busA.shutdown();
    busB.shutdown();
}

TEST(EventBus, DiscardAndExecutorRejectionReleasePayloadOutsideMutex)
{
    for (bool reject : {false, true}) {
        EventBusOptions options;
        options.executor = [reject](auto) { return !reject; };
        EventBus bus(options);
        bool released = false;
        auto subscription = bus.subscribe<std::shared_ptr<int>>([](const auto &) {});
        auto payload = std::shared_ptr<int>(new int(1), [&](int *p) {
            (void)bus.stats();
            released = true;
            delete p;
        });
        bus.publishAsync(std::move(payload));
        bus.shutdown(ShutdownMode::DISCARD);
        EXPECT_TRUE(released);
    }
}

TEST(EventBus, PruningCancelledCallbackReleasesCaptureOutsideMutex)
{
    EventBus bus;
    bool released = false;
    auto payload = std::shared_ptr<int>(new int(1), [&](int *p) {
        (void)bus.stats();
        released = true;
        delete p;
    });
    auto subscription = bus.subscribe<Number>([payload = std::move(payload)](const auto &) {});
    subscription.cancel();
    bus.publish(Number{});
    EXPECT_TRUE(released);
}

TEST(EventBus, ConsecutiveAsyncPayloadCleanupCanReenterStats)
{
    std::function<void()> drain;
    EventBusOptions options;
    options.executor = [&](auto task) {
        drain = std::move(task);
        return true;
    };
    EventBus bus(options);
    int released = 0;
    auto subscription = bus.subscribe<std::shared_ptr<int>>([](const auto &) {});
    for (int i = 0; i < 2; ++i) {
        bus.publishAsync(std::shared_ptr<int>(new int(i), [&](int *p) {
            (void)bus.stats();
            ++released;
            delete p;
        }));
    }
    drain();
    EXPECT_EQ(released, 2);
    EXPECT_EQ(bus.stats().delivered, 2U);
}


TEST(EventBus, SynchronousCallbackMayDestroyOwningBus)
{
    auto bus = std::make_unique<EventBus>();
    int calls = 0;
    auto first = bus->subscribe<Number>([&](const Number &) { bus.reset(); });
    auto second = bus->subscribe<Number>([&](const Number &) { ++calls; });
    const auto report = bus->publish(Number{1});
    EXPECT_EQ(bus, nullptr);
    EXPECT_EQ(report.delivered, 2U);
    EXPECT_EQ(calls, 1);
}

TEST(EventBus, ErrorHandlerMayDestroyOwningBus)
{
    std::unique_ptr<EventBus> bus;
    EventBusOptions options;
    options.onError = [&](const auto &) { bus.reset(); };
    bus = std::make_unique<EventBus>(std::move(options));
    auto first = bus->subscribe<Number>([](const Number &) { throw std::runtime_error("failure"); });
    int calls = 0;
    auto second = bus->subscribe<Number>([&](const Number &) { ++calls; });
    const auto report = bus->publish(Number{1});
    EXPECT_EQ(bus, nullptr);
    EXPECT_EQ(report.failed, 1U);
    EXPECT_EQ(report.delivered, 1U);
    EXPECT_EQ(calls, 1);
}

TEST(EventBus, InlineExecutorCallbackMayDestroyOwningBus)
{
    for (bool nonblocking : {false, true}) {
        EventBusOptions options;
        options.executor = [](auto task) {
            task();
            return false;
        };
        auto bus = std::make_unique<EventBus>(std::move(options));
        auto subscription = bus->subscribe<Number>([&](const Number &) { bus.reset(); });
        const auto status = nonblocking ? bus->tryPublishAsync(Number{1}) : bus->publishAsync(Number{1});
        EXPECT_EQ(status, AsyncPublishStatus::EXECUTOR_REJECTED);
        EXPECT_EQ(bus, nullptr);
    }
}
