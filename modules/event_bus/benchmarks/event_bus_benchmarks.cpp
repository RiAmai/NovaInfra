#include <event_bus/event_bus.h>

#include <chrono>
#include <iostream>

struct Tick {
    std::uint64_t value = 0;
};

int main()
{
    constexpr std::size_t count = 100000;
    novainfra::eventbus::EventBus bus;
    std::size_t received = 0;
    auto subscription = bus.subscribe<Tick>([&](const Tick &) { ++received; });
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < count; ++i) {
        bus.publish(Tick{static_cast<std::uint64_t>(i)});
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    std::cout << "events=" << received
              << " us=" << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() << '\n';
}
