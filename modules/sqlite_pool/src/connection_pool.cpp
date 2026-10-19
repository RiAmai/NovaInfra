#include <sqlite_pool/connection_pool.h>
#include <sqlite_pool/sqlite_error.h>

#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>
#include <utility>

NOVAINFRA_NAMESPACE_BEGIN
namespace sqlitepool {

struct PoolState {
    ~PoolState()
    {
        for (sqlite3 *db : readDbs) {
            if (db) {
                sqlite3_close_v2(db);
            }
        }
        if (writeDb) {
            sqlite3_close_v2(writeDb);
        }
    }
    sqlite3 *writeDb{nullptr};
    std::mutex writeMtx;
    std::mutex writeAvailabilityMtx;
    std::condition_variable writeAvailabilityCv;
    std::vector<sqlite3 *> readDbs;
    std::vector<Uint8> readBusy;
    std::mutex slotMtx;
    std::condition_variable slotCv;
};

namespace {
struct ReadLease {
    std::shared_ptr<PoolState> state;
    Size index{0};
    ReadLease(std::shared_ptr<PoolState> pool, Size slot) : state(std::move(pool)), index(slot) {}
    ~ReadLease()
    {
        if (!state) {
            return;
        }
        std::lock_guard<std::mutex> lock(state->slotMtx);
        if (index < state->readBusy.size()) {
            state->readBusy[index] = false;
        }
        state->slotCv.notify_one();
    }
};

struct WriteLease {
    std::shared_ptr<PoolState> state;
    std::unique_lock<std::mutex> lock;
    WriteLease(std::shared_ptr<PoolState> s, std::unique_lock<std::mutex> l)
        : state(std::move(s)), lock(std::move(l))
    {
    }
};
}  // namespace

ConnectionPool::ConnectionPool(const std::string &path, PoolConfig config)
    : path_(path), config_(std::move(config)), state_(std::make_shared<PoolState>())
{
    if (config_.readConnections == 0) {
        const Uint32 hw = std::thread::hardware_concurrency();
        config_.readConnections = hw > 0 ? std::max(hw, 4U) : 4U;
    }
    NOVAINFRA_TRY
    {
        state_->writeDb = openDb(false);
        applyConfig(state_->writeDb, true);
        state_->readDbs.reserve(config_.readConnections);
        state_->readBusy.assign(config_.readConnections, false);
        for (Uint32 i = 0; i < config_.readConnections; ++i) {
            sqlite3 *db = openDb(true);
            NOVAINFRA_TRY
            {
                applyConfig(db, false);
            }
            NOVAINFRA_CATCH_ALL_BODY({
                sqlite3_close_v2(db);
                NOVAINFRA_RETHROW_CURRENT();
            })
            state_->readDbs.push_back(db);
        }
        if (config_.healthCheckIntervalMs != 0) {
            healthThread_ = std::thread(&ConnectionPool::healthLoop, this);
        }
    }
    NOVAINFRA_CATCH_ALL_BODY({
        state_.reset();
        NOVAINFRA_RETHROW_CURRENT();
    })
}

ConnectionPool::~ConnectionPool()
{
    stopHealthThread();
}

Size ConnectionPool::readPoolSize() const NOVAINFRA_NOEXCEPT
{
    return state_ ? state_->readDbs.size() : 0;
}

sqlite3 *ConnectionPool::openDb(bool readonly)
{
    const int flags = readonly ? (SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX)
                               : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX);
    sqlite3 *db = nullptr;
    const int rc = sqlite3_open_v2(path_.c_str(), &db, flags, nullptr);
    if (rc != SQLITE_OK) {
        const std::string msg = db ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
        if (db) {
            sqlite3_close_v2(db);
        }
        NOVAINFRA_THROW(SqliteError("open[" + path_ + "]: " + msg, rc));
    }
    return db;
}

void ConnectionPool::applyConfig(sqlite3 *db, bool isWriter)
{
    if (config_.postOpen) {
        config_.postOpen(db, isWriter);
    }
    if (sqlite3_busy_timeout(db, config_.busyTimeoutMs) != SQLITE_OK) {
        NOVAINFRA_THROW(SqliteError("busy_timeout"));
    }
    if (isWriter) {
        execPragma(db, "PRAGMA journal_mode=" + config_.journalMode + ";");
        execPragma(db, "PRAGMA synchronous=NORMAL;");
        execPragma(db, "PRAGMA wal_autocheckpoint=1000;");
    }
    execPragma(db, "PRAGMA cache_size=" + std::to_string(config_.pageCacheSize) + ";");
    execPragma(db, "PRAGMA temp_store=MEMORY;");
    execPragma(db, "PRAGMA mmap_size=268435456;");
    for (const auto &p : config_.pragmas) {
        execPragma(db, "PRAGMA " + p + ";");
    }
}

void ConnectionPool::execPragma(sqlite3 *db, const std::string &sql)
{
    char *err = nullptr;
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        const std::string msg = err ? err : sqlite3_errstr(rc);
        sqlite3_free(err);
        NOVAINFRA_THROW(SqliteError("pragma [" + sql + "]: " + msg, rc));
    }
}

WriteConn ConnectionPool::write()
{
    for (;;) {
        {
            std::unique_lock<std::mutex> availabilityLock(state_->writeAvailabilityMtx);
            state_->writeAvailabilityCv.wait(availabilityLock, [this] { return state_->writeDb != nullptr; });
        }
        std::unique_lock<std::mutex> lock(state_->writeMtx);
        std::unique_lock<std::mutex> availabilityLock(state_->writeAvailabilityMtx);
        if (state_->writeDb != nullptr) {
            sqlite3 *db = state_->writeDb;
            availabilityLock.unlock();
            auto lease = std::make_shared<WriteLease>(state_, std::move(lock));
            return WriteConn(db, std::move(lease));
        }
        availabilityLock.unlock();
    }
}

ReadConn ConnectionPool::read()
{
    std::unique_lock<std::mutex> lock(state_->slotMtx);
    state_->slotCv.wait(lock, [this] {
        for (Size i = 0; i < state_->readBusy.size(); ++i) {
            if (!state_->readBusy[i] && state_->readDbs[i] != nullptr) {
                return true;
            }
        }
        return false;
    });
    for (Size i = 0; i < state_->readBusy.size(); ++i) {
        if (!state_->readBusy[i] && state_->readDbs[i] != nullptr) {
            state_->readBusy[i] = true;
            auto lease = std::make_shared<ReadLease>(state_, i);
            return ReadConn(state_->readDbs[i], std::move(lease));
        }
    }
    NOVAINFRA_THROW(SqliteError("read: no free slot (internal error)"));
}

bool ConnectionPool::probe(sqlite3 *db) const
{
    if (!db) {
        return false;
    }
    sqlite3_stmt *stmt = nullptr;
    const int prepareRc = sqlite3_prepare_v2(db, "SELECT 1", -1, &stmt, nullptr);
    if (prepareRc != SQLITE_OK) {
        if (stmt) {
            sqlite3_finalize(stmt);
        }
        return false;
    }
    const int stepRc = sqlite3_step(stmt);
    const bool healthy = stepRc == SQLITE_ROW || stepRc == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return healthy;
}

HealthCheckResult ConnectionPool::checkHealth()
{
    std::lock_guard<std::mutex> runLock(healthMtx_);
    HealthCheckResult result;

    // Reserve one idle slot at a time. Probe/open/postOpen can be slow and
    // must not hold the mutex used by all readers and lease destructors.
    for (Size i = 0; i < state_->readDbs.size(); ++i) {
        sqlite3 *db = nullptr;
        {
            std::lock_guard<std::mutex> lock(state_->slotMtx);
            if (state_->readBusy[i]) {
                ++result.skippedBusy;
                continue;
            }
            state_->readBusy[i] = true;
            db = state_->readDbs[i];
        }
        ++result.checked;
        if (probe(db)) {
            ++result.healthy;
        } else {
            if (db) {
                sqlite3_close_v2(db);
            }
            db = nullptr;
            NOVAINFRA_TRY
            {
                sqlite3 *replacement = openDb(true);
                NOVAINFRA_TRY
                {
                    applyConfig(replacement, false);
                }
                NOVAINFRA_CATCH_ALL_BODY({
                    sqlite3_close_v2(replacement);
                    NOVAINFRA_RETHROW_CURRENT();
                })
                db = replacement;
                ++result.replaced;
            }
            NOVAINFRA_CATCH_ALL_BODY({ ++result.failed; })
        }
        {
            std::lock_guard<std::mutex> lock(state_->slotMtx);
            state_->readDbs[i] = db;
            state_->readBusy[i] = false;
        }
        state_->slotCv.notify_all();
    }

    // Try-lock the writer so health checks never delay an active write.
    if (!state_->writeMtx.try_lock()) {
        ++result.skippedBusy;
    } else {
        sqlite3 *db = nullptr;
        {
            std::lock_guard<std::mutex> availabilityLock(state_->writeAvailabilityMtx);
            db = state_->writeDb;
        }
        ++result.checked;
        if (probe(db)) {
            ++result.healthy;
        } else {
            {
                std::lock_guard<std::mutex> availabilityLock(state_->writeAvailabilityMtx);
                state_->writeDb = nullptr;
            }
            if (db) {
                sqlite3_close_v2(db);
            }
            NOVAINFRA_TRY
            {
                sqlite3 *replacement = openDb(false);
                NOVAINFRA_TRY
                {
                    applyConfig(replacement, true);
                }
                NOVAINFRA_CATCH_ALL_BODY({
                    sqlite3_close_v2(replacement);
                    NOVAINFRA_RETHROW_CURRENT();
                })
                {
                    std::lock_guard<std::mutex> availabilityLock(state_->writeAvailabilityMtx);
                    state_->writeDb = replacement;
                }
                ++result.replaced;
            }
            NOVAINFRA_CATCH_ALL_BODY({ ++result.failed; })
        }
        state_->writeMtx.unlock();
        state_->writeAvailabilityCv.notify_all();
    }

    lastHealthResult_ = result;
    return result;
}

HealthCheckResult ConnectionPool::lastHealthCheck() const
{
    std::lock_guard<std::mutex> lock(healthMtx_);
    return lastHealthResult_;
}

void ConnectionPool::healthLoop()
{
    std::unique_lock<std::mutex> lock(healthMtx_);
    while (!healthStop_) {
        const auto interval = std::chrono::milliseconds(config_.healthCheckIntervalMs);
        if (healthCv_.wait_for(lock, interval, [this] { return healthStop_; })) {
            break;
        }
        lock.unlock();
        NOVAINFRA_TRY
        {
            checkHealth();
        }
        NOVAINFRA_CATCH_ALL_BODY({
            // Keep the monitor alive if an unexpected exception escapes a
            // connection-specific probe or replacement operation.
        })
        lock.lock();
    }
}

void ConnectionPool::stopHealthThread() NOVAINFRA_NOEXCEPT
{
    {
        std::lock_guard<std::mutex> lock(healthMtx_);
        healthStop_ = true;
    }
    healthCv_.notify_all();
    if (healthThread_.joinable()) {
        healthThread_.join();
    }
}

void ConnectionPool::integrityCheck()
{
    auto rows = read().query("PRAGMA integrity_check;");
    if (rows.size() != 1 || rows[0].at("integrity_check") != "ok") {
        std::ostringstream oss;
        oss << "integrity_check failed:";
        for (const auto &row : rows)
            oss << " " << row.at("integrity_check");
        NOVAINFRA_THROW(SqliteError(oss.str()));
    }
}

void ConnectionPool::optimize()
{
    write().exec("PRAGMA optimize;");
}

void ConnectionPool::checkpoint(int mode)
{
    {
        std::unique_lock<std::mutex> lock(state_->slotMtx);
        state_->slotCv.wait(lock, [this] {
            return std::all_of(state_->readBusy.begin(), state_->readBusy.end(),
                               [](Uint8 busy) { return !busy; });
        });
    }
    std::unique_lock<std::mutex> writeLock;
    for (;;) {
        {
            std::unique_lock<std::mutex> availabilityLock(state_->writeAvailabilityMtx);
            state_->writeAvailabilityCv.wait(availabilityLock, [this] { return state_->writeDb != nullptr; });
        }
        writeLock = std::unique_lock<std::mutex>(state_->writeMtx);
        std::lock_guard<std::mutex> availabilityLock(state_->writeAvailabilityMtx);
        if (state_->writeDb != nullptr) {
            break;
        }
        writeLock.unlock();
    }
    const int rc = sqlite3_wal_checkpoint_v2(state_->writeDb, nullptr, mode, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        NOVAINFRA_THROW(SqliteError("checkpoint", rc));
    }
}

}  // namespace sqlitepool
NOVAINFRA_NAMESPACE_END
