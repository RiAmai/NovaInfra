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
    EXPECT_EQ(rows[0].at("real_value"), "2.500000");
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

}  // namespace
