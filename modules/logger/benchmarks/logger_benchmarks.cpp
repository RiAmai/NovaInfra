#include <logger/async/async_log_thread_pool.h>
#include <logger/async/async_logger.h>
#include <logger/formatters/pattern_formatter.h>
#include <logger/logger.h>
#include <logger/novalog.h>
#include <logger/sinks/sink.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kMeasuredSamples = 7;
constexpr std::size_t kAsyncQueueCapacity = 8192;
constexpr std::size_t kAsyncLoggerCount = 64;

class CountingSink final : public nova::log::sinks::ISink
{
public:
    void log(const nova::log::LogMsg &) override { ++count_; }
    void setPattern(const std::string &) override {}
    void setFormatter(std::unique_ptr<nova::log::formatters::IFormatter>) override {}
    void flush() override {}

    bool isThreadSafe() const noexcept override { return true; }

    nova::Uint64 count() const { return count_.load(std::memory_order_relaxed); }

private:
    std::atomic<nova::Uint64> count_{0};
};

double ElapsedSeconds(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double Median(std::vector<double> samples)
{
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

void PrintResult(const std::string &name, nova::Uint64 operations, double medianSeconds)
{
    const auto nsPerOperation = medianSeconds * 1'000'000'000.0 / static_cast<double>(operations);
    const auto millionPerSecond = static_cast<double>(operations) / medianSeconds / 1'000'000.0;
    std::cerr << std::left << std::setw(34) << name << std::right << std::fixed << std::setprecision(2)
              << std::setw(12) << nsPerOperation << " ns/op" << std::setw(12) << millionPerSecond
              << " Mops/s\n";
}

template <typename Function>
void RunBenchmark(const std::string &name, nova::Uint64 operations, Function &&function)
{
    function(std::max<nova::Uint64>(operations / 20, 1));

    std::vector<double> samples;
    samples.reserve(kMeasuredSamples);
    for (std::size_t sample = 0; sample < kMeasuredSamples; ++sample) {
        samples.push_back(function(operations));
    }
    PrintResult(name, operations, Median(std::move(samples)));
}

double BenchmarkFiltered(nova::Uint64 operations)
{
    nova::log::Logger logger("filtered");
    logger.setLevel(nova::log::Level::OFF);
    const auto start = Clock::now();
    for (nova::Uint64 index = 0; index < operations; ++index) {
        logger.info("value {}", index);
    }
    return ElapsedSeconds(start);
}

double BenchmarkSynchronousLiteral(nova::Uint64 operations)
{
    auto sink = std::make_shared<CountingSink>();
    nova::log::Logger logger("sync-literal", sink);
    const auto start = Clock::now();
    for (nova::Uint64 index = 0; index < operations; ++index) {
        logger.info("short literal message");
    }
    const auto elapsed = ElapsedSeconds(start);
    if (sink->count() != operations) {
        std::abort();
    }
    return elapsed;
}

double BenchmarkSynchronousFormatted(nova::Uint64 operations)
{
    auto sink = std::make_shared<CountingSink>();
    nova::log::Logger logger("sync-formatted", sink);
    const auto start = Clock::now();
    for (nova::Uint64 index = 0; index < operations; ++index) {
        logger.info("value {}", index);
    }
    const auto elapsed = ElapsedSeconds(start);
    if (sink->count() != operations) {
        std::abort();
    }
    return elapsed;
}

double BenchmarkSynchronousLong(nova::Uint64 operations)
{
    auto sink = std::make_shared<CountingSink>();
    nova::log::Logger logger("sync-long", sink);
    const auto start = Clock::now();
    for (nova::Uint64 index = 0; index < operations; ++index) {
        logger.info("value {:0240}", index);
    }
    const auto elapsed = ElapsedSeconds(start);
    if (sink->count() != operations) {
        std::abort();
    }
    return elapsed;
}

double BenchmarkGlobalLiteral(nova::Uint64 operations)
{
    auto sink = std::make_shared<CountingSink>();
    auto logger = std::make_shared<nova::log::Logger>("benchmark-global", sink);
    nova::log::setDefaultLogger(logger);

    const auto start = Clock::now();
    for (nova::Uint64 index = 0; index < operations; ++index) {
        nova::log::info("short literal message");
    }
    const auto elapsed = ElapsedSeconds(start);
    if (sink->count() != operations) {
        std::abort();
    }
    return elapsed;
}

double BenchmarkPatternFormatter(nova::Uint64 operations, const std::string &pattern)
{
    nova::log::formatters::PatternFormatter formatter(pattern);
    nova::log::LogMsg message{nova::core::Now(), nova::log::Level::INFO, nova::core::ThreadId(), {},
                              "benchmark",       "short literal message"};
    std::string output;
    const auto start = Clock::now();
    for (nova::Uint64 index = 0; index < operations; ++index) {
        output.clear();
        formatter.format(message, output);
    }
    const auto elapsed = ElapsedSeconds(start);
    if (output.empty()) {
        std::abort();
    }
    return elapsed;
}

struct AsyncTimings {
    double producerSeconds;
    double endToEndSeconds;
};

AsyncTimings BenchmarkAsync(nova::Uint64 operations, std::size_t producerCount)
{
    auto sink = std::make_shared<CountingSink>();
    auto worker = std::make_shared<nova::log::async::AsyncLogThreadPool>(kAsyncQueueCapacity, 4);
    std::vector<std::shared_ptr<nova::log::async::AsyncLogger>> loggers;
    loggers.reserve(kAsyncLoggerCount);
    for (std::size_t index = 0; index < kAsyncLoggerCount; ++index) {
        loggers.push_back(
            std::make_shared<nova::log::async::AsyncLogger>("async-" + std::to_string(index), sink, worker));
    }

    const auto start = Clock::now();
    std::vector<std::thread> producers;
    producers.reserve(producerCount);
    for (std::size_t producer = 0; producer < producerCount; ++producer) {
        producers.emplace_back([&, producer] {
            for (nova::Uint64 index = producer; index < operations; index += producerCount) {
                loggers[index % loggers.size()]->info("value {:0240}", index);
            }
        });
    }
    for (auto &producer : producers) {
        producer.join();
    }
    const auto producerSeconds = ElapsedSeconds(start);

    loggers.clear();
    worker.reset();
    const auto endToEndSeconds = ElapsedSeconds(start);
    if (sink->count() != operations) {
        std::abort();
    }
    return {producerSeconds, endToEndSeconds};
}

void RunAsyncBenchmark(const std::string &name, nova::Uint64 operations, std::size_t producerCount)
{
    BenchmarkAsync(std::max<nova::Uint64>(operations / 20, producerCount), producerCount);

    std::vector<double> producerSamples;
    std::vector<double> endToEndSamples;
    producerSamples.reserve(kMeasuredSamples);
    endToEndSamples.reserve(kMeasuredSamples);
    for (std::size_t sample = 0; sample < kMeasuredSamples; ++sample) {
        const auto timings = BenchmarkAsync(operations, producerCount);
        producerSamples.push_back(timings.producerSeconds);
        endToEndSamples.push_back(timings.endToEndSeconds);
    }

    PrintResult(name + " producer", operations, Median(std::move(producerSamples)));
    PrintResult(name + " end-to-end", operations, Median(std::move(endToEndSamples)));
}

void PrintConfiguration()
{
    constexpr auto queueSlots = kAsyncQueueCapacity + 1;
    constexpr auto queuePayloadBytes = queueSlots * sizeof(nova::log::details::AsyncLogMsg);

    std::cerr << "Nova logger microbenchmarks\n";
    std::cerr << "  compiler: " << __VERSION__ << '\n';
#ifdef NDEBUG
    std::cerr << "  build: Release (NDEBUG)\n";
#else
    std::cerr << "  build: non-Release (assertions enabled)\n";
#endif
#ifdef NOVA_NO_THREAD_ID
    std::cerr << "  thread id: disabled\n";
#else
    std::cerr << "  thread id: enabled\n";
#endif
    std::cerr << "  samples: " << kMeasuredSamples << " measured after one warmup\n";
    std::cerr << "  sizeof(LogMsg): " << sizeof(nova::log::LogMsg) << " bytes\n";
    std::cerr << "  sizeof(AsyncLogMsg): " << sizeof(nova::log::details::AsyncLogMsg) << " bytes\n";
    std::cerr << "  async queue: " << kAsyncQueueCapacity << " usable slots, " << queueSlots
              << " allocated slots, about " << std::fixed << std::setprecision(2)
              << static_cast<double>(queuePayloadBytes) / (1024.0 * 1024.0) << " MiB payload storage\n\n";
}

}  // namespace

int main()
{
    constexpr nova::Uint64 kFastOperations = 2'000'000;
    constexpr nova::Uint64 kAsyncOperations = 400'000;

    PrintConfiguration();
    RunBenchmark("filtered formatted/local", kFastOperations, BenchmarkFiltered);
    RunBenchmark("sync literal/local", kFastOperations, BenchmarkSynchronousLiteral);
    RunBenchmark("sync formatted/local", kFastOperations, BenchmarkSynchronousFormatted);
    RunBenchmark("sync 240-byte/local", kFastOperations, BenchmarkSynchronousLong);
    RunBenchmark("sync literal/global snapshot", kFastOperations, BenchmarkGlobalLiteral);
    RunBenchmark("pattern message-only", kFastOperations,
                 [](nova::Uint64 operations) { return BenchmarkPatternFormatter(operations, "%v"); });
    RunBenchmark("pattern common datetime", kFastOperations, [](nova::Uint64 operations) {
        return BenchmarkPatternFormatter(operations, "%Y-%m-%d %H:%M:%S.%e [%l] [%n] [%t] %v");
    });
    RunBenchmark("pattern default %+", kFastOperations,
                 [](nova::Uint64 operations) { return BenchmarkPatternFormatter(operations, "%+"); });
    RunAsyncBenchmark("async 1p/64l/240-byte", kAsyncOperations, 1);
    RunAsyncBenchmark("async 4p/64l/240-byte", kAsyncOperations, 4);
    return 0;
}
