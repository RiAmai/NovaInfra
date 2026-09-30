#include <state_machine/runtime.h>
#include <state_machine/state_machine.h>

#include <deque>
#include <iostream>
#include <string>

// 连接流程的四个稳定状态。
struct Disconnected {
};
struct Connecting {
};
struct Connected {
};
struct Failed {
};
// 外部系统投递给状态机的事件类型。
struct Connect {
};
struct ConnectedReply {
    std::string peer;
};
struct ConnectFailed {
    std::string reason;
};
struct Disconnect {
};

/** @brief 使用 typed event 描述设备连接流程。 */
struct ConnectionMachine {
    auto operator()() const
    {
        using namespace novainfra::statemachine;
        // 只有携带非空设备标识的连接响应才允许进入 Connected。
        const auto validReply = [](const ConnectedReply &reply) { return !reply.peer.empty(); };
        return make_transition_table(*state<Disconnected> + event<Connect> = state<Connecting>,
                                     state<Connecting> + event<ConnectedReply>[validReply] = state<Connected>,
                                     state<Connecting> + event<ConnectFailed> = state<Failed>,
                                     state<Connected> + event<Disconnect> = state<Disconnected>,
                                     state<Failed> + event<Disconnect> = state<Disconnected>);
    }
};

int main()
{
    using Machine = novainfra::statemachine::StateMachine<ConnectionMachine>;
    Machine machine;
    // 用本地队列模拟应用已有的事件循环；生产代码可替换为 ThreadPool。
    std::deque<novainfra::statemachine::EventTask> executorQueue;
    novainfra::statemachine::RuntimeOptions options;
    options.executor = [&executorQueue](novainfra::statemachine::EventTask task) {
        executorQueue.push_back(std::move(task));
        return true;
    };
    novainfra::statemachine::StateMachineRuntime runtime(machine, options);
    runtime.post(Connect{});
    runtime.post(ConnectedReply{"sensor-01"});
    // executor 每次只执行一个 drain 任务，Runtime 会继续排空 FIFO 队列。
    while (!executorQueue.empty()) {
        auto task = std::move(executorQueue.front());
        executorQueue.pop_front();
        task();
    }
    std::cout << (machine.is(novainfra::statemachine::state<Connected>) ? "connected\n" : "not connected\n");
}
