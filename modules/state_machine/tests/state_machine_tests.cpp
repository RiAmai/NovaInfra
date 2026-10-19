#include <state_machine/runtime.h>
#include <state_machine/state_machine.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
// 测试用状态、事件和依赖对象。
struct Idle {
};
struct Running {
};
struct Done {
};
struct Start {
};
struct Finish {
    int value = 0;
};
struct Unknown {
};
struct Trace {
    std::vector<int> values;
};
struct PollTick {
};

struct PollTrace {
    std::atomic<int> active{0};
    std::atomic<int> maxActive{0};
};

struct Definition {
    auto operator()() const
    {
        using namespace novainfra::statemachine;
        // 通过 guard 验证事件载荷，再由 action 记录转移顺序。
        const auto validFinish = [](const Finish &event) { return event.value > 0; };
        return make_transition_table(
            *state<Idle> + event<Start> / [](Trace &trace) { trace.values.push_back(1); } = state<Running>,
            state<Running> + event<Finish>[validFinish] / [](Trace &trace) { trace.values.push_back(2); } =
                state<Done>);
    }
};
using Machine = novainfra::statemachine::StateMachine<Definition>;

struct PollDefinition {
    auto operator()() const
    {
        using namespace novainfra::statemachine;
        return make_transition_table(*state<Idle> + event<PollTick> / [](PollTrace &trace) {
            const int active = trace.active.fetch_add(1) + 1;
            int observed = trace.maxActive.load();
            while (observed < active && !trace.maxActive.compare_exchange_weak(observed, active)) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            trace.active.fetch_sub(1);
        } = state<Idle>);
    }
};
}

TEST(StateMachine, TypedEventsAndGuards)
{
    // 验证 typed event、guard、依赖注入和状态查询。
    Trace trace;
    Machine machine{trace};
    EXPECT_TRUE(machine.is(novainfra::statemachine::state<Idle>));
    EXPECT_TRUE(machine.process_event(Start{}));
    EXPECT_TRUE(machine.is(novainfra::statemachine::state<Running>));
    EXPECT_FALSE(machine.process_event(Finish{0}));
    EXPECT_TRUE(machine.process_event(Finish{1}));
    EXPECT_TRUE(machine.is(novainfra::statemachine::state<Done>));
    EXPECT_EQ(trace.values, (std::vector<int>{1, 2}));
}

TEST(StateMachine, UnhandledEvents)
{
    // 未匹配事件必须返回 false，且不能改变当前状态。
    Trace trace;
    Machine machine{trace};
    EXPECT_FALSE(machine.process_event(Unknown{}));
}

TEST(StateMachineRuntime, SerializesPostsThroughInjectedExecutor)
{
    // 多个事件只安排一个 drain 任务，并按投递顺序执行。
    Trace trace;
    Machine machine{trace};
    std::deque<novainfra::statemachine::EventTask> tasks;
    novainfra::statemachine::RuntimeOptions options;
    options.executor = [&tasks](novainfra::statemachine::EventTask task) {
        tasks.push_back(std::move(task));
        return true;
    };
    novainfra::statemachine::StateMachineRuntime runtime(machine, options);
    EXPECT_EQ(runtime.post(Start{}), novainfra::statemachine::PostStatus::ACCEPTED);
    EXPECT_EQ(runtime.post(Finish{1}), novainfra::statemachine::PostStatus::ACCEPTED);
    ASSERT_EQ(tasks.size(), 1U);
    auto task = std::move(tasks.front());
    tasks.pop_front();
    task();
    EXPECT_TRUE(tasks.empty());
    EXPECT_TRUE(machine.is(novainfra::statemachine::state<Done>));
    EXPECT_EQ(runtime.stats().processed, 2U);
}

TEST(StateMachineRuntime, BoundedQueueRejectsAndPolls)
{
    // 无 executor 时由 poll() 驱动，并验证有界队列的拒绝策略。
    Trace trace;
    Machine machine{trace};
    novainfra::statemachine::RuntimeOptions options;
    options.capacity = 1;
    novainfra::statemachine::StateMachineRuntime runtime(machine, options);
    EXPECT_EQ(runtime.post(Start{}), novainfra::statemachine::PostStatus::ACCEPTED);
    EXPECT_EQ(runtime.post(Finish{1}), novainfra::statemachine::PostStatus::QUEUE_FULL);
    EXPECT_EQ(runtime.poll(), 1U);
    EXPECT_TRUE(machine.is(novainfra::statemachine::state<Running>));
}

TEST(StateMachineRuntime, DefaultPollDrainsAllAndZeroPollDoesNothing)
{
    Trace trace;
    Machine machine{trace};
    novainfra::statemachine::RuntimeOptions options;
    options.capacity = 2;
    novainfra::statemachine::StateMachineRuntime runtime(machine, options);
    EXPECT_EQ(runtime.post(Start{}), novainfra::statemachine::PostStatus::ACCEPTED);
    EXPECT_EQ(runtime.post(Finish{1}), novainfra::statemachine::PostStatus::ACCEPTED);
    EXPECT_EQ(runtime.poll(0), 0U);
    EXPECT_EQ(runtime.stats().queued, 2U);
    EXPECT_EQ(runtime.poll(), 2U);
    EXPECT_TRUE(machine.is(novainfra::statemachine::state<Done>));
}

TEST(StateMachineRuntime, BoundedPollLeavesRemainingEventsQueued)
{
    Trace trace;
    Machine machine{trace};
    novainfra::statemachine::RuntimeOptions options;
    options.capacity = 2;
    novainfra::statemachine::StateMachineRuntime runtime(machine, options);
    EXPECT_EQ(runtime.post(Start{}), novainfra::statemachine::PostStatus::ACCEPTED);
    EXPECT_EQ(runtime.post(Finish{1}), novainfra::statemachine::PostStatus::ACCEPTED);
    EXPECT_EQ(runtime.poll(1), 1U);
    EXPECT_EQ(runtime.stats().queued, 1U);
    EXPECT_TRUE(machine.is(novainfra::statemachine::state<Running>));
    EXPECT_EQ(runtime.poll(), 1U);
    EXPECT_TRUE(machine.is(novainfra::statemachine::state<Done>));
}

TEST(StateMachineRuntime, ConcurrentPollCallsRemainSerialized)
{
    PollTrace trace;
    novainfra::statemachine::StateMachine<PollDefinition> machine{trace};
    novainfra::statemachine::RuntimeOptions options;
    options.capacity = 2;
    novainfra::statemachine::StateMachineRuntime runtime(machine, options);
    EXPECT_EQ(runtime.post(PollTick{}), novainfra::statemachine::PostStatus::ACCEPTED);
    EXPECT_EQ(runtime.post(PollTick{}), novainfra::statemachine::PostStatus::ACCEPTED);

    std::promise<void> firstReady;
    std::promise<void> secondReady;
    auto firstReadyFuture = firstReady.get_future();
    auto secondReadyFuture = secondReady.get_future();
    auto start = std::make_shared<std::promise<void>>();
    auto gate = start->get_future().share();
    std::thread first([&runtime, &firstReady, gate] {
        firstReady.set_value();
        gate.wait();
        runtime.poll();
    });
    std::thread second([&runtime, &secondReady, gate] {
        secondReady.set_value();
        gate.wait();
        runtime.poll();
    });
    firstReadyFuture.wait();
    secondReadyFuture.wait();
    start->set_value();
    first.join();
    second.join();
    EXPECT_EQ(trace.maxActive.load(), 1);
}

TEST(StateMachineRuntime, CallbackFailureStopsRuntime)
{
    // 回调异常必须被 Runtime 捕获，并使 Runtime 停止接收新事件。
    struct FailingDefinition {
        auto operator()() const
        {
            using namespace novainfra::statemachine;
            return make_transition_table(*state<Idle> + event<Start> / [](Trace &) {
                throw std::runtime_error("failure");
            } = state<Running>);
        }
    };
    Trace trace;
    novainfra::statemachine::StateMachine<FailingDefinition> machine{trace};
    std::deque<novainfra::statemachine::EventTask> tasks;
    std::exception_ptr error;
    novainfra::statemachine::RuntimeOptions options;
    options.executor = [&tasks](novainfra::statemachine::EventTask task) {
        tasks.push_back(std::move(task));
        return true;
    };
    options.onError = [&error](const novainfra::statemachine::RuntimeError &diagnostic) {
        error = diagnostic.exception;
    };
    novainfra::statemachine::StateMachineRuntime runtime(machine, options);
    EXPECT_EQ(runtime.post(Start{}), novainfra::statemachine::PostStatus::ACCEPTED);
    auto task = std::move(tasks.front());
    tasks.pop_front();
    task();
    EXPECT_EQ(runtime.stats().status, novainfra::statemachine::RuntimeStatus::FAILED);
    EXPECT_TRUE(error != nullptr);
    EXPECT_EQ(runtime.post(Start{}), novainfra::statemachine::PostStatus::STOPPED);
}

TEST(StateMachineRuntime, ExecutorRejectionIsCounted)
{
    Trace trace;
    Machine machine{trace};
    novainfra::statemachine::RuntimeOptions options;
    options.executor = [](novainfra::statemachine::EventTask) { return false; };
    novainfra::statemachine::StateMachineRuntime runtime(machine, options);

    EXPECT_EQ(runtime.post(Start{}), novainfra::statemachine::PostStatus::EXECUTOR_REJECTED);
    const auto stats = runtime.stats();
    EXPECT_EQ(stats.status, novainfra::statemachine::RuntimeStatus::FAILED);
    EXPECT_EQ(stats.executorFailures, 1U);
    EXPECT_EQ(stats.accepted, 1U);
    EXPECT_EQ(stats.dropped, 1U);
    EXPECT_EQ(stats.queued, 0U);
}

TEST(StateMachineRuntime, ExecutorExceptionIsCounted)
{
    Trace trace;
    Machine machine{trace};
    std::exception_ptr error;
    novainfra::statemachine::RuntimeOptions options;
    options.executor = [](novainfra::statemachine::EventTask) -> bool {
        throw std::runtime_error("executor failure");
    };
    options.onError = [&error](const novainfra::statemachine::RuntimeError &diagnostic) {
        error = diagnostic.exception;
    };
    novainfra::statemachine::StateMachineRuntime runtime(machine, options);

    EXPECT_EQ(runtime.post(Start{}), novainfra::statemachine::PostStatus::EXECUTOR_REJECTED);
    const auto stats = runtime.stats();
    EXPECT_EQ(stats.status, novainfra::statemachine::RuntimeStatus::FAILED);
    EXPECT_EQ(stats.executorFailures, 1U);
    EXPECT_EQ(stats.accepted, 1U);
    EXPECT_EQ(stats.dropped, 1U);
    EXPECT_EQ(stats.queued, 0U);
    EXPECT_TRUE(error != nullptr);
}

namespace {
struct RuntimeProbeMachine {
    std::function<void()> action;
    template <typename Event>
    bool process_event(const Event &)
    {
        if (action)
            action();
        return true;
    }
    void flush_queue() {}
};
}

TEST(StateMachineRuntime, OwnsMoveOnlyEvents)
{
    RuntimeProbeMachine machine;
    novainfra::statemachine::StateMachineRuntime runtime(machine);
    auto payload = std::make_unique<int>(42);
    EXPECT_EQ(runtime.post(std::move(payload)), novainfra::statemachine::PostStatus::ACCEPTED);
    EXPECT_EQ(payload, nullptr);
    EXPECT_EQ(runtime.poll(), 1U);
}

TEST(StateMachineRuntime, BoundedDrainStaysStoppingUntilQueueIsEmpty)
{
    RuntimeProbeMachine machine;
    novainfra::statemachine::StateMachineRuntime runtime(machine);
    runtime.post(1);
    runtime.post(2);
    runtime.shutdown();
    EXPECT_EQ(runtime.poll(1), 1U);
    EXPECT_EQ(runtime.stats().status, novainfra::statemachine::RuntimeStatus::STOPPING);
    EXPECT_EQ(runtime.poll(1), 1U);
    EXPECT_EQ(runtime.stats().status, novainfra::statemachine::RuntimeStatus::STOPPED);
}

TEST(StateMachineRuntime, DiscardAndFailureReleasePayloadsOutsideMutex)
{
    for (bool fail : {false, true}) {
        RuntimeProbeMachine machine;
        novainfra::statemachine::StateMachineRuntime runtime(machine);
        bool released = false;
        auto payload = std::shared_ptr<int>(new int(1), [&](int *p) {
            EXPECT_FALSE(runtime.accepting());
            released = true;
            delete p;
        });
        if (fail) {
            machine.action = [] { throw std::runtime_error("event failed"); };
            runtime.post(0);
        }
        runtime.post(std::move(payload));
        if (fail)
            runtime.poll();
        else
            runtime.shutdown(novainfra::statemachine::ShutdownMode::DISCARD);
        EXPECT_TRUE(released);
    }
}

TEST(StateMachineRuntime, DestructorCancelsDelayedExecutorWithoutWaiting)
{
    RuntimeProbeMachine machine;
    int calls = 0;
    machine.action = [&] { ++calls; };
    novainfra::statemachine::EventTask delayed;
    novainfra::statemachine::RuntimeOptions options;
    options.executor = [&](auto task) {
        delayed = std::move(task);
        return true;
    };
    {
        novainfra::statemachine::StateMachineRuntime runtime(machine, options);
        runtime.post(1);
    }
    delayed();
    EXPECT_EQ(calls, 0);
}

TEST(StateMachineRuntime, DestructorWaitsForRunningEvent)
{
    std::promise<void> entered, release, destroying, destroyed;
    auto gate = release.get_future().share();
    auto complete = destroyed.get_future();
    RuntimeProbeMachine machine;
    machine.action = [&] {
        entered.set_value();
        gate.wait();
    };
    std::thread worker;
    novainfra::statemachine::RuntimeOptions options;
    options.executor = [&](auto task) {
        worker = std::thread(std::move(task));
        return true;
    };
    using Runtime = novainfra::statemachine::StateMachineRuntime<RuntimeProbeMachine>;
    auto runtime = std::make_unique<Runtime>(machine, options);
    runtime->post(1);
    entered.get_future().wait();
    std::thread disposer([&] {
        destroying.set_value();
        runtime.reset();
        destroyed.set_value();
    });
    destroying.get_future().wait();
    EXPECT_EQ(complete.wait_for(std::chrono::milliseconds(30)), std::future_status::timeout);
    release.set_value();
    disposer.join();
    worker.join();
    EXPECT_EQ(complete.wait_for(std::chrono::seconds(1)), std::future_status::ready);
}

TEST(StateMachineRuntime, DestructionFromOwnPollCallbackFailsFast)
{
    EXPECT_DEATH(
        {
            RuntimeProbeMachine machine;
            using Runtime = novainfra::statemachine::StateMachineRuntime<RuntimeProbeMachine>;
            auto runtime = std::make_unique<Runtime>(machine);
            machine.action = [&] { runtime.reset(); };
            runtime->post(1);
            runtime->poll();
        },
        "");
}
