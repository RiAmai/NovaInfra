#include <event_bus/event_bus.h>
#include "../../../benchmarks/metrics.h"
#ifdef NOVAINFRA_BENCHMARK_THREAD_POOL
#  include <thread_pool/thread_pool.h>
#endif
#include <cstdlib>

struct Tick {
    benchmark::Clock::time_point sent;
};

int main(int argc, char **argv)
{
    const std::size_t count =
        std::max<std::size_t>(1, argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 20000);
    std::cout << "Per-event latency includes instrumentation; RSS is process high-water mark.\n";
    for (int subscribers : {1, 8}) {
        novainfra::eventbus::EventBus bus;
        std::vector<novainfra::eventbus::Subscription> subscriptions;
        std::vector<double> latencies;
        latencies.reserve(count * subscribers);
        for (int i = 0; i < subscribers; ++i)
            subscriptions.push_back(bus.subscribe<Tick>([&](const Tick &tick) {
                latencies.push_back(benchmark::microseconds(benchmark::Clock::now() - tick.sent));
            }));
        const auto start = benchmark::Clock::now();
        for (std::size_t i = 0; i < count; ++i)
            bus.publish(Tick{benchmark::Clock::now()});
        benchmark::report("sync subscribers=" + std::to_string(subscribers), std::move(latencies),
                          benchmark::Clock::now() - start, count);
        if (bus.stats().delivered != count * subscribers)
            return 1;
    }
#ifdef NOVAINFRA_BENCHMARK_THREAD_POOL
    using namespace novainfra::eventbus;
    for (std::size_t capacity : {8U, 1024U}) {
        for (auto policy : {QueueFullPolicy::BLOCK, QueueFullPolicy::REJECT, QueueFullPolicy::DROP_NEWEST}) {
            novainfra::threadpool::ThreadPoolOptions poolOptions;
            poolOptions.workerCount = 2;
            poolOptions.queueCapacity = 64;
            novainfra::threadpool::ThreadPool pool(poolOptions);
            EventBusOptions options;
            options.queueCapacity = capacity;
            options.queueFullPolicy = policy;
            options.executor = [&](auto task) { return pool.tryPost(std::move(task)); };
            EventBus bus(options);
            std::vector<double> latencies;
            latencies.reserve(count);
            std::uint64_t checksum = 0;
            auto subscription = bus.subscribe<Tick>([&](const Tick &tick) {
                // Small CPU task, rather than an empty callback.
                std::uint64_t value = 123;
                for (int j = 0; j < 1000; ++j)
                    value = value * 1664525 + 1013904223;
                checksum += value;
                latencies.push_back(benchmark::microseconds(benchmark::Clock::now() - tick.sent));
            });
            std::size_t accepted = 0, rejected = 0;
            const auto start = benchmark::Clock::now();
            for (std::size_t i = 0; i < count; ++i) {
                const auto result = bus.publishAsync(Tick{benchmark::Clock::now()});
                if (result == AsyncPublishStatus::ACCEPTED)
                    ++accepted;
                else if (result == AsyncPublishStatus::QUEUE_FULL || result == AsyncPublishStatus::DROPPED)
                    ++rejected;
                else
                    return 1;
            }
            bus.shutdown();
            pool.waitIdle();
            const char *name = policy == QueueFullPolicy::BLOCK    ? "BLOCK"
                               : policy == QueueFullPolicy::REJECT ? "REJECT"
                                                                   : "DROP_NEWEST";
            benchmark::report("async CPU callback q=" + std::to_string(capacity) + " policy=" + name,
                              std::move(latencies), benchmark::Clock::now() - start, accepted);
            std::cout << "attempts=" << count << " rejected_or_dropped=" << rejected
                      << " dropped=" << bus.stats().dropped << " checksum=" << checksum << '\n';
            if (bus.stats().delivered != accepted || accepted + rejected != count)
                return 1;
        }
    }
#endif
    return 0;
}
