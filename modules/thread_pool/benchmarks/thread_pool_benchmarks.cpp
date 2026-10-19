#include <thread_pool/thread_pool.h>
#include "../../../benchmarks/metrics.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

template <typename Submit>
void runBenchmark(const char *name, std::size_t operations,
                  const novainfra::threadpool::ThreadPoolOptions &options, Submit submit)
{
    constexpr int warmupRuns = 1;
    constexpr int measuredRuns = 5;
    for (int run = 0; run < warmupRuns; ++run) {
        novainfra::threadpool::ThreadPool pool(options);
        submit(pool);
        pool.shutdown();
    }

    std::array<double, measuredRuns> throughputs{};
    for (int run = 0; run < measuredRuns; ++run) {
        novainfra::threadpool::ThreadPool pool(options);
        const auto start = Clock::now();
        submit(pool);
        const auto elapsed = std::chrono::duration<double>(Clock::now() - start).count();
        throughputs[run] = static_cast<double>(operations) / elapsed;
        pool.shutdown();
    }
    std::sort(throughputs.begin(), throughputs.end());
    std::cout << std::left << std::setw(30) << name << std::right << std::fixed << std::setprecision(0)
              << "median=" << throughputs[measuredRuns / 2] << " min=" << throughputs.front()
              << " max=" << throughputs.back() << " tasks/s\n";
}
}  // namespace

int main(int argc, char **argv)
{
    const std::size_t operations =
        std::max<std::size_t>(1, argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 200000);
    const auto detected = std::thread::hardware_concurrency();
    const std::size_t workerCount = detected == 0 ? 1 : detected;
    novainfra::threadpool::ThreadPoolOptions options;
    options.workerCount = std::min<std::size_t>(workerCount, 8);
    options.queueCapacity = 1024;

    std::cout << "operations=" << operations << " workers=" << options.workerCount
              << " queue=" << options.queueCapacity << " release benchmark, 5 samples\n";

    runBenchmark("post empty task", operations, options, [&](auto &pool) {
        for (std::size_t index = 0; index < operations; ++index) {
            pool.post([] {});
        }
        pool.waitIdle();
    });

    runBenchmark("submit with future", operations, options, [&](auto &pool) {
        std::vector<std::future<std::size_t>> futures;
        futures.reserve(operations);
        for (std::size_t index = 0; index < operations; ++index) {
            futures.emplace_back(pool.submit([index] { return index + 1; }));
        }
        for (auto &future : futures) {
            future.get();
        }
    });

    runBenchmark("four producers", operations, options, [&](auto &pool) {
        std::vector<std::thread> producers;
        constexpr std::size_t producerCount = 4;
        const std::size_t tasksPerProducer = operations / producerCount;
        const std::size_t remainder = operations % producerCount;
        for (std::size_t producer = 0; producer < producerCount; ++producer) {
            const std::size_t producerTasks = tasksPerProducer + (producer < remainder ? 1 : 0);
            producers.emplace_back([&pool, producerTasks] {
                for (std::size_t index = 0; index < producerTasks; ++index) {
                    pool.post([] {});
                }
            });
        }
        for (auto &producer : producers) {
            producer.join();
        }
        pool.waitIdle();
    });

    auto tryPostOptions = options;
    tryPostOptions.queueCapacity = operations;
    runBenchmark("tryPost no rejection", operations, tryPostOptions, [&](auto &pool) {
        std::size_t accepted = 0;
        for (std::size_t index = 0; index < operations; ++index) {
            accepted += pool.tryPost([] {}) ? 1 : 0;
        }
        pool.waitIdle();
        if (accepted != operations) {
            std::abort();
        }
    });

    for (const auto workers : {std::size_t{1}, options.workerCount}) {
        for (const auto capacity : {std::size_t{8}, std::size_t{1024}}) {
            options.workerCount = workers;
            options.queueCapacity = capacity;
            const auto label =
                std::string("post w=") + std::to_string(workers) + " q=" + std::to_string(capacity);
            runBenchmark(label.c_str(), operations, options, [&](auto &pool) {
                for (std::size_t index = 0; index < operations; ++index) {
                    pool.post([] {});
                }
                pool.waitIdle();
            });
        }
    }

    options.workerCount = std::min<std::size_t>(workerCount, 8);
    options.queueCapacity = 1024;
    runBenchmark("post large capture", operations, options, [&](auto &pool) {
        const std::array<std::size_t, 32> capture{};
        std::vector<std::size_t> results(operations);
        for (std::size_t index = 0; index < operations; ++index) {
            pool.post([capture, &results, index] { results[index] = capture[0] + index + 1; });
        }
        pool.waitIdle();
        if (results.back() != operations) {
            std::abort();
        }
    });
    for (const auto workers : {std::size_t{1}, std::min<std::size_t>(workerCount, 8)}) {
        for (const auto capacity : {std::size_t{8}, std::size_t{1024}}) {
            options.workerCount = workers;
            options.queueCapacity = capacity;
            novainfra::threadpool::ThreadPool pool(options);
            const auto count = std::min<std::size_t>(operations, 20000);
            std::vector<double> latencies(count);
            std::vector<std::uint64_t> results(count);
            const auto start = Clock::now();
            for (std::size_t i = 0; i < count; ++i) {
                const auto sent = Clock::now();
                pool.post([&, i, sent] {
                    std::uint64_t value = i + 1;
                    for (int j = 0; j < 1000; ++j)
                        value = value * 1664525 + 1013904223;
                    results[i] = value;
                    latencies[i] = benchmark::microseconds(Clock::now() - sent);
                });
            }
            pool.waitIdle();
            benchmark::report("CPU task enqueue-to-completion w=" + std::to_string(workers) +
                                  " q=" + std::to_string(capacity),
                              std::move(latencies), Clock::now() - start, count);
            if (results.back() == 0)
                return 1;
        }
    }
}
