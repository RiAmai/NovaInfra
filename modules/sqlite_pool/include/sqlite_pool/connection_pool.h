#pragma once

#include <sqlite_pool/types.h>
#include <sqlite_pool/statement.h>
#include <sqlite_pool/write_conn.h>
#include <sqlite_pool/read_conn.h>

#include <core/macros.h>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <thread>


NOVAINFRA_NAMESPACE_BEGIN
namespace sqlitepool {

struct PoolState;

class ConnectionPool
{
public:
    explicit ConnectionPool(const std::string &path, PoolOptions config = {});
    ~ConnectionPool();
    NOVAINFRA_DISABLE_COPY_MOVE(ConnectionPool)

    // Acquire the exclusive write handle (blocks while another thread holds it)
    WriteConn write();
    // Acquire a free read handle (blocks until a slot becomes available)
    ReadConn read();

    Size readPoolSize() const NOVAINFRA_NOEXCEPT;
    const std::string &path() const NOVAINFRA_NOEXCEPT { return path_; }

    // PRAGMA integrityCheck - throws on failure
    void integrityCheck();
    // PRAGMA optimize
    void optimize();
    // WAL checkpoint
    void checkpoint(int mode = SQLITE_CHECKPOINT_PASSIVE);

    // Check idle connections and replace unhealthy handles.
    HealthCheckResult checkHealth();
    // Return the most recent manual or background health-check result.
    HealthCheckResult lastHealthCheck() const;

private:
    sqlite3 *openDb(bool readonly);
    void applyConfig(sqlite3 *db, bool isWriter);
    void execPragma(sqlite3 *db, const std::string &sql);
    bool probe(sqlite3 *db) const;
    void healthLoop();
    void stopHealthThread() NOVAINFRA_NOEXCEPT;

    std::string path_;
    PoolOptions config_;
    std::shared_ptr<PoolState> state_;
    std::thread healthThread_;
    mutable std::mutex healthMtx_;
    std::condition_variable healthCv_;
    bool healthStop_{false};
    HealthCheckResult lastHealthResult_{};
};

}  // namespace sqlitepool
NOVAINFRA_NAMESPACE_END
