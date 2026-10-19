#include <sqlite_pool/connection_pool.h>
#include <sqlite_pool/sqlite_error.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <string>
#include <stdexcept>
#include <system_error>
#include <thread>

namespace {

class TempDatabase
{
public:
    TempDatabase()
        : path_(std::filesystem::temp_directory_path() /
                ("novainfra_sqlite_pool_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db"))
    {
    }
    ~TempDatabase()
    {
        std::error_code error;
        std::filesystem::remove(path_, error);
        std::filesystem::remove(path_.string() + "-wal", error);
        std::filesystem::remove(path_.string() + "-shm", error);
    }
    const std::string &string() const { return pathString_; }
    const std::filesystem::path &path() const { return path_; }

private:
    std::filesystem::path path_;
    std::string pathString_{path_.string()};
};

novainfra::sqlitepool::PoolConfig OneReader()
{
    novainfra::sqlitepool::PoolConfig config;
    config.readConnections = 1;
    return config;
}

int DenyAllAuthorizer(void *, int, const char *, const char *, const char *, const char *)
{
    return SQLITE_DENY;
}

TEST(SqlitePool, DefaultAndExplicitReaderPoolSizes)
{
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool defaults(database.string());
    EXPECT_GE(defaults.readPoolSize(), 4U);

    auto config = OneReader();
    novainfra::sqlitepool::ConnectionPool explicitPool(database.string(), config);
    EXPECT_EQ(explicitPool.readPoolSize(), 1U);
}

TEST(SqlitePool, TransactionCommitsAndRollsBack)
{
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), OneReader());
    pool.write().exec("CREATE TABLE values_table(value INTEGER NOT NULL)");

    pool.write().transaction([](novainfra::sqlitepool::WriteConn &db) {
        db.prepare("INSERT INTO values_table(value) VALUES(?)").bind(1, 7).exec();
    });
    EXPECT_THROW(pool.write().transaction([](novainfra::sqlitepool::WriteConn &db) {
        db.prepare("INSERT INTO values_table(value) VALUES(?)").bind(1, 9).exec();
        throw std::runtime_error("rollback");
    }),
                 std::runtime_error);

    const auto rows = pool.read().query("SELECT value FROM values_table ORDER BY value");
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].at("value"), "7");
}

TEST(SqlitePool, BindingAndColumnAccessWork)
{
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), OneReader());
    auto db = pool.write();
    db.exec("CREATE TABLE items(id INTEGER, name TEXT, score REAL, note TEXT)");
    auto insert = db.prepare("INSERT INTO items VALUES(:id, :name, :score, :note)");
    insert.bind(":id", 42)
        .bind(":name", std::string("answer"))
        .bind(":score", 2.5)
        .bind(":note", nullptr)
        .exec();

    auto statement = pool.read().prepare("SELECT id, name, score, note FROM items");
    ASSERT_TRUE(statement.step());
    EXPECT_EQ(statement.columnInt(0), 42);
    EXPECT_EQ(statement.columnText(1), "answer");
    EXPECT_DOUBLE_EQ(statement.columnDouble(2), 2.5);
    EXPECT_TRUE(statement.columnNull(3));
    EXPECT_EQ(statement.columnCount(), 4);
    EXPECT_EQ(statement.columnName(1), "name");
    EXPECT_FALSE(statement.step());
}

TEST(SqlitePool, TextAccessPreservesEmbeddedNulls)
{
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), OneReader());
    pool.write().exec("CREATE TABLE text_values(value TEXT)");
    pool.write().exec("INSERT INTO text_values(value) VALUES(CAST(X'610062' AS TEXT)), (NULL)");

    auto statement = pool.read().prepare("SELECT value FROM text_values ORDER BY rowid");
    ASSERT_TRUE(statement.step());
    const auto view = statement.columnTextView(0);
    EXPECT_EQ(view.size(), 3U);
    EXPECT_EQ(view, std::string_view("a\0b", 3));
    EXPECT_EQ(statement.columnText(0), std::string("a\0b", 3));

    ASSERT_TRUE(statement.step());
    EXPECT_TRUE(statement.columnNull(0));
    EXPECT_TRUE(statement.columnTextView(0).empty());
    EXPECT_FALSE(statement.step());
}

TEST(SqlitePool, MaterializedRowsPreserveNamesValuesAndEmbeddedNulls)
{
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), OneReader());
    const auto rows = pool.read().query("SELECT 9223372036854775807 AS integer_value, 2.5 AS real_value, "
                                        "CAST(X'780079' AS TEXT) AS text_value, NULL AS null_value");

    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].at("integer_value"), "9223372036854775807");
    EXPECT_EQ(rows[0].at("real_value"), "2.5");
    EXPECT_EQ(rows[0].at("text_value"), std::string("x\0y", 3));
    EXPECT_EQ(rows[0].at("null_value"), "");
}

TEST(SqlitePool, MaterializedRowsKeepLastDuplicateColumnName)
{
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), OneReader());
    const auto rows = pool.read().query("SELECT 1 AS duplicate, 2 AS duplicate");

    ASSERT_EQ(rows.size(), 1U);
    ASSERT_EQ(rows[0].size(), 1U);
    EXPECT_EQ(rows[0].at("duplicate"), "2");
}

TEST(SqlitePool, ReadSlotIsReleasedAndStatementKeepsLease)
{
    TempDatabase database;
    novainfra::sqlitepool::Statement statement;
    {
        novainfra::sqlitepool::ConnectionPool pool(database.string(), OneReader());
        pool.write().exec("CREATE TABLE items(value INTEGER)");
        auto reader = pool.read();
        statement = reader.prepare("SELECT 1 AS value");
    }
    ASSERT_TRUE(statement.step());
    EXPECT_EQ(statement.columnInt(0), 1);
    EXPECT_FALSE(statement.step());
}

TEST(SqlitePool, SecondReaderWaitsForFirstReader)
{
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), OneReader());
    std::future<std::size_t> waiting;
    {
        auto first = pool.read();
        waiting = std::async(std::launch::async, [&pool] {
            auto second = pool.read();
            return second.query("SELECT 1").size();
        });
        EXPECT_EQ(waiting.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
    }
    EXPECT_EQ(waiting.get(), 1U);
}

TEST(SqlitePool, InvalidSqlReportsSqliteError)
{
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), OneReader());
    EXPECT_THROW(pool.read().query("SELECT definitely_missing"), novainfra::sqlitepool::SqliteError);
    EXPECT_THROW(pool.write().prepare("SELECT :missing").bind(":unknown", 1),
                 novainfra::sqlitepool::SqliteError);
}

TEST(SqlitePool, IntegrityAndPostOpenAreAvailable)
{
    TempDatabase database;
    std::atomic<int> opened{0};
    auto config = OneReader();
    config.postOpen = [&opened](sqlite3 *, bool) { ++opened; };
    novainfra::sqlitepool::ConnectionPool pool(database.string(), config);
    EXPECT_EQ(opened.load(), 2);
    EXPECT_NO_THROW(pool.integrityCheck());
    EXPECT_NO_THROW(pool.optimize());
    EXPECT_NO_THROW(pool.checkpoint());
}

TEST(SqlitePool, HealthCheckReplacesUnhealthyReadAndWriteConnections)
{
    TempDatabase database;
    auto config = OneReader();
    config.healthCheckIntervalMs = 0;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), config);

    {
        auto reader = pool.read();
        ASSERT_EQ(sqlite3_set_authorizer(reader.handle(), DenyAllAuthorizer, nullptr), SQLITE_OK);
    }
    {
        auto writer = pool.write();
        ASSERT_EQ(sqlite3_set_authorizer(writer.handle(), DenyAllAuthorizer, nullptr), SQLITE_OK);
    }

    const auto result = pool.checkHealth();
    EXPECT_EQ(result.checked, 2U);
    EXPECT_EQ(result.healthy, 0U);
    EXPECT_EQ(result.replaced, 2U);
    EXPECT_EQ(result.failed, 0U);

    auto reader = pool.read();
    EXPECT_NO_THROW(reader.query("SELECT 1"));
    auto writer = pool.write();
    EXPECT_NO_THROW(writer.exec("SELECT 1"));
    EXPECT_EQ(pool.lastHealthCheck().replaced, 2U);
}

TEST(SqlitePool, HealthCheckSkipsBusyReaders)
{
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), OneReader());
    auto reader = pool.read();

    const auto result = pool.checkHealth();
    EXPECT_EQ(result.skippedBusy, 1U);
    EXPECT_EQ(result.checked, 1U);  // the idle writer
    EXPECT_EQ(result.healthy, 1U);
}

TEST(SqlitePool, HealthCheckRetriesFailedReplacement)
{
    TempDatabase database;
    std::atomic<bool> failOpen{false};
    auto config = OneReader();
    config.healthCheckIntervalMs = 0;
    config.postOpen = [&failOpen](sqlite3 *, bool) {
        if (failOpen.load()) {
            throw std::runtime_error("injected replacement failure");
        }
    };
    novainfra::sqlitepool::ConnectionPool pool(database.string(), config);
    {
        auto reader = pool.read();
        ASSERT_EQ(sqlite3_set_authorizer(reader.handle(), DenyAllAuthorizer, nullptr), SQLITE_OK);
    }

    failOpen.store(true);
    const auto failed = pool.checkHealth();
    EXPECT_EQ(failed.failed, 1U);
    EXPECT_EQ(failed.replaced, 0U);

    failOpen.store(false);
    const auto recovered = pool.checkHealth();
    EXPECT_EQ(recovered.replaced, 1U);
    EXPECT_NO_THROW(pool.read().query("SELECT 1"));
}

TEST(SqlitePool, BackgroundHealthCheckRunsAtConfiguredInterval)
{
    TempDatabase database;
    auto config = OneReader();
    config.healthCheckIntervalMs = 10;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), config);
    {
        auto reader = pool.read();
        ASSERT_EQ(sqlite3_set_authorizer(reader.handle(), DenyAllAuthorizer, nullptr), SQLITE_OK);
    }

    bool replaced = false;
    for (int i = 0; i < 200; ++i) {
        if (pool.lastHealthCheck().replaced > 0U) {
            replaced = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(replaced);
}

TEST(SqlitePool, ConcurrentReads)
{
    TempDatabase database;
    auto config = OneReader();
    config.readConnections = 4;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), config);
    pool.write().exec("CREATE TABLE data(id INTEGER PRIMARY KEY, value TEXT)");
    for (int i = 0; i < 100; ++i) {
        pool.write().exec("INSERT INTO data(id, value) VALUES(" + std::to_string(i) + ", 'item_" +
                          std::to_string(i) + "')");
    }

    constexpr int numReaders = 8;
    constexpr int readsPerThread = 50;
    std::vector<std::thread> readers;
    std::atomic<int> totalReads{0};
    std::atomic<int> errors{0};

    for (int i = 0; i < numReaders; ++i) {
        readers.emplace_back([&pool, &totalReads, &errors]() {
            for (int j = 0; j < readsPerThread; ++j) {
                try {
                    auto rows = pool.read().query("SELECT COUNT(*) as count FROM data");
                    if (rows.size() == 1 && rows[0].at("count") == "100") {
                        ++totalReads;
                    }
                } catch (...) {
                    ++errors;
                }
            }
        });
    }

    for (auto &t : readers) {
        t.join();
    }

    EXPECT_EQ(totalReads.load(), numReaders * readsPerThread);
    EXPECT_EQ(errors.load(), 0);
}

TEST(SqlitePool, ConcurrentWritesAndReads)
{
    TempDatabase database;
    auto config = OneReader();
    config.readConnections = 4;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), config);
    pool.write().exec("CREATE TABLE counters(id INTEGER PRIMARY KEY, value INTEGER DEFAULT 0)");
    pool.write().exec("INSERT INTO counters(id, value) VALUES(1, 0)");

    constexpr int numWriters = 3;
    constexpr int numReaders = 3;
    constexpr int writesPerThread = 20;
    constexpr int readsPerThread = 30;

    std::atomic<int> successfulWrites{0};
    std::atomic<int> successfulReads{0};

    std::vector<std::thread> writers;
    for (int i = 0; i < numWriters; ++i) {
        writers.emplace_back([&pool, &successfulWrites]() {
            for (int j = 0; j < writesPerThread; ++j) {
                try {
                    pool.write().exec("UPDATE counters SET value = value + 1 WHERE id = 1");
                    ++successfulWrites;
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                } catch (...) {
                    // Write failed
                }
            }
        });
    }

    std::vector<std::thread> readers;
    for (int i = 0; i < numReaders; ++i) {
        readers.emplace_back([&pool, &successfulReads]() {
            for (int j = 0; j < readsPerThread; ++j) {
                try {
                    auto rows = pool.read().query("SELECT value FROM counters WHERE id = 1");
                    if (rows.size() == 1) {
                        ++successfulReads;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                } catch (...) {
                    // Read failed
                }
            }
        });
    }

    for (auto &t : writers) {
        t.join();
    }
    for (auto &t : readers) {
        t.join();
    }

    EXPECT_EQ(successfulWrites.load(), numWriters * writesPerThread);
    EXPECT_EQ(successfulReads.load(), numReaders * readsPerThread);

    // Verify final counter value
    auto rows = pool.read().query("SELECT value FROM counters WHERE id = 1");
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].at("value"), std::to_string(numWriters * writesPerThread));
}

TEST(SqlitePool, ConcurrentTransactions)
{
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool pool(database.string());
    pool.write().exec("CREATE TABLE accounts(id INTEGER PRIMARY KEY, balance INTEGER)");
    pool.write().exec("INSERT INTO accounts(id, balance) VALUES(1, 1000), (2, 1000)");

    constexpr int numTransfers = 50;
    std::atomic<int> successfulTransfers{0};
    std::atomic<int> failedTransfers{0};

    std::vector<std::thread> transferThreads;
    for (int i = 0; i < numTransfers; ++i) {
        transferThreads.emplace_back([&pool, &successfulTransfers, &failedTransfers, i]() {
            try {
                pool.write().transaction([i](novainfra::sqlitepool::WriteConn &db) {
                    // Transfer 10 from account 1 to account 2
                    db.exec("UPDATE accounts SET balance = balance - 10 WHERE id = 1");
                    db.exec("UPDATE accounts SET balance = balance + 10 WHERE id = 2");
                    if (i % 10 == 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                });
                ++successfulTransfers;
            } catch (...) {
                ++failedTransfers;
            }
        });
    }

    for (auto &t : transferThreads) {
        t.join();
    }

    EXPECT_EQ(successfulTransfers.load(), numTransfers);
    EXPECT_EQ(failedTransfers.load(), 0);

    // Verify balances
    auto rows = pool.read().query("SELECT id, balance FROM accounts ORDER BY id");
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].at("balance"), std::to_string(1000 - numTransfers * 10));
    EXPECT_EQ(rows[1].at("balance"), std::to_string(1000 + numTransfers * 10));
}

TEST(SqlitePool, StressTestReadPoolExhaustion)
{
    TempDatabase database;
    auto config = OneReader();
    config.readConnections = 2;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), config);
    pool.write().exec("CREATE TABLE items(id INTEGER)");
    pool.write().exec("INSERT INTO items VALUES(1), (2), (3)");

    constexpr int numThreads = 10;
    constexpr int operationsPerThread = 20;
    std::atomic<int> completedOps{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&pool, &completedOps]() {
            for (int j = 0; j < operationsPerThread; ++j) {
                auto reader = pool.read();  // Will block if pool is exhausted
                auto rows = reader.query("SELECT COUNT(*) FROM items");
                if (rows.size() == 1) {
                    ++completedOps;
                }
            }
        });
    }

    for (auto &t : threads) {
        t.join();
    }

    EXPECT_EQ(completedOps.load(), numThreads * operationsPerThread);
}

TEST(SqlitePool, MaterializedDoublePreservesRoundTripPrecision)
{
    TempDatabase database;
    novainfra::sqlitepool::ConnectionPool pool(database.string(), OneReader());
    for (double value : {1.2345678901234567, 1e-200, 1e200, -1.2345678901234567}) {
        auto statement = pool.read().prepare("SELECT ? AS value");
        auto rows = statement.bind(1, value).query();
        ASSERT_EQ(rows.size(), 1U);
        EXPECT_EQ(std::stod(rows.front().at("value")), value);
    }
}

TEST(SqlitePool, SlowReadReplacementDoesNotBlockOtherReaderSlots)
{
    TempDatabase database;
    auto config = OneReader();
    config.readConnections = 2;
    std::atomic<bool> replacing{false};
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    config.postOpen = [&](sqlite3 *, bool writer) {
        if (!writer && replacing.load()) {
            entered.set_value();
            gate.wait();
        }
    };
    novainfra::sqlitepool::ConnectionPool pool(database.string(), config);
    {
        auto reader = pool.read();
        sqlite3_set_authorizer(reader.handle(), DenyAllAuthorizer, nullptr);
    }
    replacing.store(true);
    auto health = std::async(std::launch::async, [&] { return pool.checkHealth(); });
    entered.get_future().wait();
    auto query = std::async(std::launch::async, [&] { return pool.read().query("SELECT 42 AS value"); });
    const auto ready = query.wait_for(std::chrono::seconds(1));
    release.set_value();
    EXPECT_EQ(ready, std::future_status::ready);
    EXPECT_EQ(query.get().front().at("value"), "42");
    EXPECT_EQ(health.get().replaced, 1U);
}

}  // namespace
