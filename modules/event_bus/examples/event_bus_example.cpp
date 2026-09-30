#include <event_bus/event_bus.h>

#include <iostream>

struct DeviceState {
    int value = 0;
};

int main()
{
    novainfra::eventbus::EventBusOptions options;
    options.executor = [](novainfra::eventbus::EventTask task) {
        task();
        return true;
    };
    novainfra::eventbus::EventBus bus(options);
    auto subscription = bus.subscribe<DeviceState>(
        [](const DeviceState &state) { std::cout << "device value=" << state.value << '\n'; });
    bus.publish(DeviceState{1});
    bus.publishAsync(DeviceState{2});
    bus.shutdown();
}
