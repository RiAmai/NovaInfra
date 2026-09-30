#include <sqlite_pool/connection_pool.h>
#include <sqlite_pool/sqlite_error.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using novainfra::Uint64;

constexpr std::size_t kMeasuredSamples = 11;

class TempDatabase
{
public:
    TempDatabase()
    {
        static std::atomic<Uint64> sequence{0};
        path_ = std::filesystem::temp_directory_path() /
                ("novainfra_sqlite_benchmark_" + std::to_string(Clock::now().time_since_epoch().count()) +
                 "_" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) + ".db");
    }

    ~TempDatabase()
    {
        std::error_code error;
        std::filesystem::remove(path_, error);
        std::filesystem::remove(path_.string() + "-wal", error);
        std::filesystem::remove(path_.string() + "-shm", error);
    }

    std::string string() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

struct SampleSummary {
    double median;
    double p95;
};

double ElapsedSeconds(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

SampleSummary Summarize(std::vector<double> samples)
{
    std::sort(samples.begin(), samples.end());
    const auto p95Index = (samples.size() * 95 + 99) / 100 - 1;
    return {samples[samples.size() / 2], samples[std::min(p95Index, samples.size() - 1)]};
}

void PrintResult(const std::string &name, Uint64 operations, const SampleSummary &summary)
{
    const auto medianNs = summary.median * 1'000'000'000.0 / static_cast<double>(operations);
    const auto p95Ns = summary.p95 * 1'000'000'000.0 / static_cast<double>(operations);
    const auto millionPerSecond = static_cast<double>(operations) / summary.median / 1'000'000.0;
    std::cerr << std::left << std::setw(34) << name << std::right << std::fixed << std::setprecision(2)
              << std::setw(12) << medianNs << " ns/op" << std::setw(12) << p95Ns << " p95" << std::setw(12)
              << millionPerSecond << " Mops/s\n";
}

template <typename Function>
void RunBenchmark(const std::string &name, Uint64 operations, Function &&function)
{
    function(std::max<Uint64>(operations / 20, 1));

    std::vector<double> samples;
    samples.reserve(kMeasuredSamples);
    for (std::size_t sample = 0; sample < kMeasuredSamples; ++sample) {
        samples.push_back(function(operations));
    }
    PrintResult(name, operations, Summarize(std::move(samples)));
}

novainfra::sqlitepool::PoolConfig PoolConfig(novainfra::Uint32 readers = 1)
{
    novainfra::sqlitepool::PoolConfig config;
    config.readConnections = readers;
    config.healthCheckIntervalMs = 0;
    return config;
}

double BenchmarkReadLease(Uint64 operations)
{
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), PoolConfig(4));
    const auto start = Clock::now();
    for (Uint64 index = 0; index < operations; ++index) {
        auto reader = pool.read();
        if (!reader.handle()) {
            std::abort();
        }
    }
    return ElapsedSeconds(start);
}

double BenchmarkPreparedWrites(Uint64 operations)
{
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), PoolConfig());
    auto writer = pool.write();
    writer.exec("CREATE TABLE benchmark_values(value INTEGER NOT NULL)");
    auto statement = writer.prepare("INSERT INTO benchmark_values(value) VALUES(?)");

    const auto start = Clock::now();
    writer.begin();
    for (Uint64 index = 0; index < operations; ++index) {
        statement.bind(1, static_cast<novainfra::Int64>(index)).exec();
    }
    writer.commit();
    return ElapsedSeconds(start);
}

double BenchmarkManualTransaction(Uint64 operations)
{
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), PoolConfig());
    auto writer = pool.write();
    const auto start = Clock::now();
    for (Uint64 index = 0; index < operations; ++index) {
        writer.begin();
        writer.commit();
    }
    return ElapsedSeconds(start);
}

double BenchmarkTransactionCallback(Uint64 operations)
{
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), PoolConfig());
    auto writer = pool.write();
    const auto start = Clock::now();
    for (Uint64 index = 0; index < operations; ++index) {
        writer.transaction([](novainfra::sqlitepool::WriteConn &) {});
    }
    return ElapsedSeconds(start);
}

void Populate(novainfra::sqlitepool::ConnectionPool &pool, Uint64 rows)
{
    auto writer = pool.write();
    writer.exec("CREATE TABLE benchmark_values(id INTEGER NOT NULL, value TEXT NOT NULL)");
    auto statement = writer.prepare("INSERT INTO benchmark_values VALUES(?, ?)");
    const std::string value(32, 'x');
    writer.begin();
    for (Uint64 index = 0; index < rows; ++index) {
        statement.bind(1, static_cast<novainfra::Int64>(index)).bind(2, value).exec();
    }
    writer.commit();
}

double BenchmarkTypedRead(Uint64 operations)
{
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), PoolConfig());
    Populate(pool, operations);
    auto statement = pool.read().prepare("SELECT id, value FROM benchmark_values");
    Uint64 count = 0;
    Uint64 checksum = 0;
    const auto start = Clock::now();
    while (statement.step()) {
        checksum += static_cast<Uint64>(statement.columnInt64(0));
        checksum += statement.columnTextView(1).size();
        ++count;
    }
    const auto elapsed = ElapsedSeconds(start);
    if (count != operations || (operations != 0 && checksum == 0)) {
        std::abort();
    }
    return elapsed;
}

double BenchmarkMaterializedRead(Uint64 operations)
{
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), PoolConfig());
    Populate(pool, operations);
    auto reader = pool.read();
    const auto start = Clock::now();
    const auto rows = reader.query("SELECT id, value FROM benchmark_values");
    const auto elapsed = ElapsedSeconds(start);
    if (rows.size() != operations) {
        std::abort();
    }
    return elapsed;
}

double BenchmarkConcurrentReads(Uint64 operations)
{
    constexpr std::size_t kThreads = 4;
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), PoolConfig(kThreads));
    std::atomic<Uint64> completed{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    const auto start = Clock::now();
    for (std::size_t thread = 0; thread < kThreads; ++thread) {
        threads.emplace_back([&, thread] {
            auto statement = pool.read().prepare("SELECT 1");
            for (Uint64 index = thread; index < operations; index += kThreads) {
                if (!statement.step() || statement.columnInt(0) != 1) {
                    std::abort();
                }
                statement.reset();
                completed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }
    const auto elapsed = ElapsedSeconds(start);
    if (completed.load(std::memory_order_relaxed) != operations) {
        std::abort();
    }
    return elapsed;
}

#ifndef NOVAINFRA_NO_EXCEPTIONS
double BenchmarkSqliteError(Uint64 operations)
{
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), PoolConfig());
    auto reader = pool.read();
    Uint64 caught = 0;
    const auto start = Clock::now();
    for (Uint64 index = 0; index < operations; ++index) {
        try {
            reader.prepare("SELECT definitely_missing_column");
        } catch (const novainfra::sqlitepool::SqliteError &) {
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

}  // namespace

int main()
{
    constexpr Uint64 kLeaseOperations = 500'000;
    constexpr Uint64 kWriteOperations = 50'000;
    constexpr Uint64 kTransactionOperations = 2'000;
    constexpr Uint64 kReadRows = 50'000;
    constexpr Uint64 kConcurrentOperations = 200'000;
#ifndef NOVAINFRA_NO_EXCEPTIONS
    constexpr Uint64 kErrorOperations = 10'000;
#endif

    std::cerr << "Nova sqlite_pool microbenchmarks\n";
    std::cerr << "  compiler: " << __VERSION__ << '\n';
#ifdef NDEBUG
    std::cerr << "  build: Release (NDEBUG)\n";
#else
    std::cerr << "  build: non-Release (assertions enabled)\n";
#endif
    std::cerr << "  samples: " << kMeasuredSamples << " measured after one warmup\n\n";

    RunBenchmark("read connection lease", kLeaseOperations, BenchmarkReadLease);
    RunBenchmark("prepared insert/transaction", kWriteOperations, BenchmarkPreparedWrites);
    RunBenchmark("manual BEGIN/COMMIT", kTransactionOperations, BenchmarkManualTransaction);
    RunBenchmark("transaction std::function", kTransactionOperations, BenchmarkTransactionCallback);
    RunBenchmark("typed step read", kReadRows, BenchmarkTypedRead);
    RunBenchmark("RowSet materialization", kReadRows, BenchmarkMaterializedRead);
    RunBenchmark("read 4 threads", kConcurrentOperations, BenchmarkConcurrentReads);
#ifndef NOVAINFRA_NO_EXCEPTIONS
    RunBenchmark("SQLite error throw/catch", kErrorOperations, BenchmarkSqliteError);
#endif
    return 0;
}
