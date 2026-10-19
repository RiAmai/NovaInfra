#include <logger/async/async_log_thread_pool.h>
#include <logger/async/async_logger.h>
#include <logger/formatters/pattern_formatter.h>
#include <logger/logger.h>
#include <logger/log.h>
#include <logger/sinks/sink.h>

#include "../../../benchmarks/metrics.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kMeasuredSamples = 11;
constexpr std::size_t kAsyncQueueCapacity = 8192;
constexpr std::size_t kAsyncLoggerCount = 64;

class CountingSink NOVAINFRA_FINAL : public novainfra::log::sinks::ISink
{
public:
    void log(const novainfra::log::LogMsg &) NOVAINFRA_OVERRIDE { ++count_; }
    void setPattern(const std::string &) NOVAINFRA_OVERRIDE {}
    void setFormatter(std::unique_ptr<novainfra::log::formatters::IFormatter>) NOVAINFRA_OVERRIDE {}
    void flush() NOVAINFRA_OVERRIDE {}

    bool isThreadSafe() const noexcept NOVAINFRA_OVERRIDE { return true; }

    novainfra::Uint64 count() const { return count_.load(std::memory_order_relaxed); }

private:
    std::atomic<novainfra::Uint64> count_{0};
};

double ElapsedSeconds(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

struct SampleSummary {
    double median;
    double p95;
};

SampleSummary Summarize(std::vector<double> samples)
{
    std::sort(samples.begin(), samples.end());
    const auto p95Index = (samples.size() * 95 + 99) / 100 - 1;
    return {samples[samples.size() / 2], samples[std::min(p95Index, samples.size() - 1)]};
}

void PrintResult(const std::string &name, novainfra::Uint64 operations, const SampleSummary &summary)
{
    const auto medianNs = summary.median * 1'000'000'000.0 / static_cast<double>(operations);
    const auto p95Ns = summary.p95 * 1'000'000'000.0 / static_cast<double>(operations);
    const auto millionPerSecond = static_cast<double>(operations) / summary.median / 1'000'000.0;
    std::cerr << std::left << std::setw(34) << name << std::right << std::fixed << std::setprecision(2)
              << std::setw(12) << medianNs << " ns/op" << std::setw(12) << p95Ns << " batch-mean p95"
              << std::setw(12) << millionPerSecond << " Mops/s\n";
}

template <typename Function>
void RunBenchmark(const std::string &name, novainfra::Uint64 operations, Function &&function)
{
    function(std::max<novainfra::Uint64>(operations / 20, 1));

    std::vector<double> samples;
    samples.reserve(kMeasuredSamples);
    for (std::size_t sample = 0; sample < kMeasuredSamples; ++sample) {
        samples.push_back(function(operations));
    }
    PrintResult(name, operations, Summarize(std::move(samples)));
}

double BenchmarkFiltered(novainfra::Uint64 operations)
{
    novainfra::log::Logger logger("filtered");
    logger.setLevel(novainfra::log::Level::OFF);
    const auto start = Clock::now();
    for (novainfra::Uint64 index = 0; index < operations; ++index) {
        logger.info("value {}", index);
    }
    return ElapsedSeconds(start);
}

double BenchmarkSynchronousLiteral(novainfra::Uint64 operations)
{
    auto sink = std::make_shared<CountingSink>();
    novainfra::log::Logger logger("sync-literal", sink);
    const auto start = Clock::now();
    for (novainfra::Uint64 index = 0; index < operations; ++index) {
        logger.info("short literal message");
    }
    const auto elapsed = ElapsedSeconds(start);
    if (sink->count() != operations) {
        std::abort();
    }
    return elapsed;
}

double BenchmarkSynchronousFormatted(novainfra::Uint64 operations)
{
    auto sink = std::make_shared<CountingSink>();
    novainfra::log::Logger logger("sync-formatted", sink);
    const auto start = Clock::now();
    for (novainfra::Uint64 index = 0; index < operations; ++index) {
        logger.info("value {}", index);
    }
    const auto elapsed = ElapsedSeconds(start);
    if (sink->count() != operations) {
        std::abort();
    }
    return elapsed;
}

double BenchmarkSynchronousLong(novainfra::Uint64 operations)
{
    auto sink = std::make_shared<CountingSink>();
    novainfra::log::Logger logger("sync-long", sink);
    const auto start = Clock::now();
    for (novainfra::Uint64 index = 0; index < operations; ++index) {
        logger.info("value {:0240}", index);
    }
    const auto elapsed = ElapsedSeconds(start);
    if (sink->count() != operations) {
        std::abort();
    }
    return elapsed;
}

double BenchmarkSynchronousMessage(novainfra::Uint64 operations, std::size_t messageSize)
{
    auto sink = std::make_shared<CountingSink>();
    novainfra::log::Logger logger("sync-message", sink);
    const std::string message(messageSize, 'x');
    const auto start = Clock::now();
    for (novainfra::Uint64 index = 0; index < operations; ++index) {
        logger.info(std::string_view(message));
    }
    const auto elapsed = ElapsedSeconds(start);
    if (sink->count() != operations) {
        std::abort();
    }
    return elapsed;
}

double BenchmarkGlobalLiteral(novainfra::Uint64 operations)
{
    auto sink = std::make_shared<CountingSink>();
    auto logger = std::make_shared<novainfra::log::Logger>("benchmark-global", sink);
    novainfra::log::setDefaultLogger(logger);

    const auto start = Clock::now();
    for (novainfra::Uint64 index = 0; index < operations; ++index) {
        novainfra::log::info("short literal message");
    }
    const auto elapsed = ElapsedSeconds(start);
    if (sink->count() != operations) {
        std::abort();
    }
    return elapsed;
}

double BenchmarkCachedGlobalLiteral(novainfra::Uint64 operations)
{
    auto sink = std::make_shared<CountingSink>();
    novainfra::log::setDefaultLogger(std::make_shared<novainfra::log::Logger>("benchmark-cached", sink));
    const auto logger = novainfra::log::defaultLogger();

    const auto start = Clock::now();
    for (novainfra::Uint64 index = 0; index < operations; ++index) {
        logger->info("short literal message");
    }
    const auto elapsed = ElapsedSeconds(start);
    if (sink->count() != operations) {
        std::abort();
    }
    return elapsed;
}

#ifndef NOVAINFRA_NO_EXCEPTIONS
double BenchmarkThrowCatch(novainfra::Uint64 operations)
{
    volatile novainfra::Uint64 caught = 0;
    const auto start = Clock::now();
    for (novainfra::Uint64 index = 0; index < operations; ++index) {
        try {
            throw std::runtime_error("benchmark failure");
        } catch (const std::runtime_error &) {
            ++caught;
        }
    }
    const auto elapsed = ElapsedSeconds(start);
    if (caught != operations) {
        std::abort();
    }
    return elapsed;
}
#endif

double BenchmarkPatternFormatter(novainfra::Uint64 operations, const std::string &pattern)
{
    novainfra::log::formatters::PatternFormatter formatter(pattern);
    novainfra::log::LogMsg message{
        novainfra::core::Now(), novainfra::log::Level::INFO, novainfra::core::ThreadId(), {}, "benchmark",
        "short literal message"};
    std::string output;
    const auto start = Clock::now();
    for (novainfra::Uint64 index = 0; index < operations; ++index) {
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

AsyncTimings BenchmarkAsync(novainfra::Uint64 operations, std::size_t producerCount)
{
    auto sink = std::make_shared<CountingSink>();
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(kAsyncQueueCapacity, 4);
    std::vector<std::shared_ptr<novainfra::log::async::AsyncLogger>> loggers;
    loggers.reserve(kAsyncLoggerCount);
    for (std::size_t index = 0; index < kAsyncLoggerCount; ++index) {
        loggers.push_back(std::make_shared<novainfra::log::async::AsyncLogger>(
            "async-" + std::to_string(index), sink, worker));
    }

    const auto start = Clock::now();
    std::vector<std::thread> producers;
    producers.reserve(producerCount);
    for (std::size_t producer = 0; producer < producerCount; ++producer) {
        producers.emplace_back([&, producer] {
            for (novainfra::Uint64 index = producer; index < operations; index += producerCount) {
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

void RunAsyncBenchmark(const std::string &name, novainfra::Uint64 operations, std::size_t producerCount)
{
    BenchmarkAsync(std::max<novainfra::Uint64>(operations / 20, producerCount), producerCount);

    std::vector<double> producerSamples;
    std::vector<double> endToEndSamples;
    producerSamples.reserve(kMeasuredSamples);
    endToEndSamples.reserve(kMeasuredSamples);
    for (std::size_t sample = 0; sample < kMeasuredSamples; ++sample) {
        const auto timings = BenchmarkAsync(operations, producerCount);
        producerSamples.push_back(timings.producerSeconds);
        endToEndSamples.push_back(timings.endToEndSeconds);
    }

    PrintResult(name + " producer", operations, Summarize(std::move(producerSamples)));
    PrintResult(name + " end-to-end", operations, Summarize(std::move(endToEndSamples)));
}

class LatencySink final : public novainfra::log::sinks::ISink
{
public:
    explicit LatencySink(std::size_t count) : sent(count), latency(count) {}
    void log(const novainfra::log::LogMsg &msg) override
    {
        std::size_t index = 0;
        const auto parsed =
            std::from_chars(msg.message.data(), msg.message.data() + msg.message.size(), index);
        if (parsed.ec != std::errc{} || index >= latency.size())
            std::abort();
        latency[index] = benchmark::microseconds(Clock::now() - sent[index]);
        ++received;
    }
    void setPattern(const std::string &) override {}
    void setFormatter(std::unique_ptr<novainfra::log::formatters::IFormatter>) override {}
    void flush() override {}
    std::vector<Clock::time_point> sent;
    std::vector<double> latency;
    std::size_t received = 0;
};

void RunMessageLatency(std::size_t count)
{
    auto sink = std::make_shared<LatencySink>(count);
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(8192, 1);
    auto logger = std::make_shared<novainfra::log::async::AsyncLogger>("latency", sink, worker);
    const auto start = Clock::now();
    for (std::size_t i = 0; i < count; ++i) {
        sink->sent[i] = Clock::now();
        logger->info("{}", i);
    }
    worker.reset();
    if (sink->received != count)
        std::abort();
    benchmark::report("async formatted enqueue-to-sink (includes index parsing)", std::move(sink->latency),
                      Clock::now() - start, count);
}

void PrintConfiguration()
{
    constexpr auto queueSlots = kAsyncQueueCapacity + 1;
    constexpr auto queuePayloadBytes = queueSlots * sizeof(novainfra::log::details::AsyncLogMsg);

    std::cerr << "Nova logger microbenchmarks\n";
    std::cerr << "  compiler: " << __VERSION__ << '\n';
#ifdef NDEBUG
    std::cerr << "  build: Release (NDEBUG)\n";
#else
    std::cerr << "  build: non-Release (assertions enabled)\n";
#endif
#ifdef NOVAINFRA_NO_THREAD_ID
    std::cerr << "  thread id: disabled\n";
#else
    std::cerr << "  thread id: enabled\n";
#endif
    std::cerr << "  samples: " << kMeasuredSamples << " measured after one warmup\n";
    std::cerr << "  sizeof(LogMsg): " << sizeof(novainfra::log::LogMsg) << " bytes\n";
    std::cerr << "  sizeof(AsyncLogMsg): " << sizeof(novainfra::log::details::AsyncLogMsg) << " bytes\n";
    std::cerr << "  async queue: " << kAsyncQueueCapacity << " usable slots, " << queueSlots
              << " allocated slots, about " << std::fixed << std::setprecision(2)
              << static_cast<double>(queuePayloadBytes) / (1024.0 * 1024.0) << " MiB payload storage\n\n";
}

}  // namespace

int main(int argc, char **argv)
{
    const novainfra::Uint64 kFastOperations =
        argc > 1 ? std::max<unsigned long long>(1, std::strtoull(argv[1], nullptr, 10)) : 2'000'000;
    const novainfra::Uint64 kAsyncOperations = std::max<novainfra::Uint64>(4, kFastOperations / 5);
    const novainfra::Uint64 kExceptionOperations = std::max<novainfra::Uint64>(1, kFastOperations / 20);

    PrintConfiguration();
    RunMessageLatency(std::min<novainfra::Uint64>(kAsyncOperations, 20000));
    RunBenchmark("filtered formatted/local", kFastOperations, BenchmarkFiltered);
    RunBenchmark("sync literal/local", kFastOperations, BenchmarkSynchronousLiteral);
    RunBenchmark("sync formatted/local", kFastOperations, BenchmarkSynchronousFormatted);
    RunBenchmark("sync 32-byte/local", kFastOperations,
                 [](novainfra::Uint64 operations) { return BenchmarkSynchronousMessage(operations, 32); });
    RunBenchmark("sync 240-byte/formatted", kFastOperations, BenchmarkSynchronousLong);
    RunBenchmark("sync 240-byte/local", kFastOperations,
                 [](novainfra::Uint64 operations) { return BenchmarkSynchronousMessage(operations, 240); });
    RunBenchmark("sync 1024-byte/local", kFastOperations,
                 [](novainfra::Uint64 operations) { return BenchmarkSynchronousMessage(operations, 1024); });
    RunBenchmark("sync literal/global lookup", kFastOperations, BenchmarkGlobalLiteral);
    RunBenchmark("sync literal/cached global", kFastOperations, BenchmarkCachedGlobalLiteral);
    RunBenchmark("pattern message-only", kFastOperations,
                 [](novainfra::Uint64 operations) { return BenchmarkPatternFormatter(operations, "%v"); });
    RunBenchmark("pattern common datetime", kFastOperations, [](novainfra::Uint64 operations) {
        return BenchmarkPatternFormatter(operations, "%Y-%m-%d %H:%M:%S.%e [%l] [%n] [%t] %v");
    });
    RunBenchmark("pattern default %+", kFastOperations,
                 [](novainfra::Uint64 operations) { return BenchmarkPatternFormatter(operations, "%+"); });
#ifndef NOVAINFRA_NO_EXCEPTIONS
    RunBenchmark("throw/catch runtime_error", kExceptionOperations, BenchmarkThrowCatch);
#endif
    RunAsyncBenchmark("async 1p/64l/240-byte", kAsyncOperations, 1);
    RunAsyncBenchmark("async 4p/64l/240-byte", kAsyncOperations, 4);
    return 0;
}
