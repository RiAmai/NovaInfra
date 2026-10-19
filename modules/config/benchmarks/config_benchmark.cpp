#include <config/config.h>
#include "../../../benchmarks/metrics.h"

#include <atomic>
#include <fstream>
#include <thread>
#include <unistd.h>

namespace {
using benchmark::Clock;
struct Fixture {
    std::filesystem::path path;
    novainfra::config::Schema schema;
    explicit Fixture(int keys)
    {
        static unsigned sequence = 0;
        path = std::filesystem::temp_directory_path() /
               ("novainfra-config-benchmark-" + std::to_string(getpid()) + "-" + std::to_string(++sequence) +
                ".toml");
        std::ofstream out(path);
        out << "[benchmark]\n";
        for (int i = 0; i < keys; ++i) {
            out << "key_" << i << " = " << i << '\n';
            schema.required<std::int64_t>("benchmark.key_" + std::to_string(i));
        }
        out << "[strings]\n";
        for (int i = 0; i < keys / 10; ++i) {
            out << "str_" << i << " = \"value_" << i << "\"\n";
            schema.required<std::string>("strings.str_" + std::to_string(i));
        }
        for (int i = 0; i < keys / 20; ++i) {
            out << "[nested.section_" << i << "]\nvalue = " << i << '\n';
            schema.required<std::int64_t>("nested.section_" + std::to_string(i) + ".value");
        }
    }
    ~Fixture()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

template <typename Operation>
bool measure(const std::string &name, int iterations, Operation operation)
{
    std::vector<double> samples;
    samples.reserve(iterations);
    const auto begin = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        const auto start = Clock::now();
        if (!operation(i))
            return false;
        samples.push_back(benchmark::microseconds(Clock::now() - start));
    }
    benchmark::report(name, std::move(samples), Clock::now() - begin, iterations);
    return true;
}

bool run(int keys)
{
    Fixture fixture(keys);
    auto loaded = novainfra::config::ConfigManager::load(fixture.schema, {
                                                                             {fixture.path, true}
    });
    if (!loaded) {
        std::cerr << loaded.error().message() << '\n';
        return false;
    }
    auto manager = std::move(loaded.value());
    const auto suffix = " keys=" + std::to_string(keys);
    const auto snapshot = manager.snapshot();
    std::int64_t checksum = 0;
    if (!measure("load" + suffix, keys >= 1000 ? 10 : 100, [&](int) {
            auto result = novainfra::config::ConfigManager::load(fixture.schema, {
                                                                                     {fixture.path, true}
            });
            if (!result)
                std::cerr << result.error().message() << '\n';
            return bool(result);
        }))
        return false;
    if (!measure("snapshot acquisition" + suffix, 10000, [&](int) {
            auto acquired = manager.snapshot();
            return &acquired.root() == &snapshot.root();
        }))
        return false;
    if (!measure("cached snapshot get" + suffix, 10000, [&](int i) {
            auto value = snapshot.get<int>("benchmark.key_" + std::to_string(i % keys));
            if (value)
                checksum += value.value();
            return bool(value);
        }))
        return false;
    if (!measure("reload" + suffix, 10, [&](int) {
            auto result = manager.reload();
            if (!result)
                std::cerr << result.error().message() << '\n';
            return bool(result);
        }))
        return false;
    std::atomic<std::int64_t> total{0};
    std::vector<std::thread> readers;
    std::vector<std::vector<double>> concurrentLatencies(4);
    const auto start = Clock::now();
    for (int t = 0; t < 4; ++t)
        readers.emplace_back([&, t] {
            auto &samples = concurrentLatencies[static_cast<std::size_t>(t)];
            samples.reserve(10000);
            std::int64_t sum = 0;
            for (int i = 0; i < 10000; ++i) {
                const auto operationStart = Clock::now();
                sum += manager.snapshot().get<int>("benchmark.key_0").value();
                samples.push_back(benchmark::microseconds(Clock::now() - operationStart));
            }
            total.fetch_add(sum);
        });
    for (auto &reader : readers)
        reader.join();
    std::vector<double> concurrentSamples;
    concurrentSamples.reserve(40000);
    for (auto &samples : concurrentLatencies)
        concurrentSamples.insert(concurrentSamples.end(), samples.begin(), samples.end());
    benchmark::report("concurrent acquisition+get threads=4" + suffix, std::move(concurrentSamples),
                      Clock::now() - start, 40000);
    std::cout << "checksum=" << checksum + total.load() << '\n';
    return true;
}
}

int main()
{
    std::cout
        << "Per-operation wall latency includes clock instrumentation; RSS is process high-water mark.\n";
    for (int keys : {10, 100, 1000})
        if (!run(keys))
            return 1;
    return 0;
}
