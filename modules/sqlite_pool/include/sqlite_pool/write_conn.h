#pragma once

#include <sqlite_pool/types.h>
#include <sqlite_pool/statement.h>

#include <core/macros.h>
#include <mutex>
#include <memory>


NOVAINFRA_NAMESPACE_BEGIN
namespace sqlitepool {

class WriteConn
{
public:
    WriteConn() = delete;
    NOVAINFRA_DISABLE_COPY(WriteConn)
    WriteConn(WriteConn &&) NOVAINFRA_NOEXCEPT;
    WriteConn &operator=(WriteConn &&) NOVAINFRA_NOEXCEPT;
    ~WriteConn();

    void exec(const std::string &sql);
    Statement prepare(const std::string &sql);
    void begin();
    void commit();
    void rollback();

    // Runs f() in BEGIN/COMMIT; auto-rollback on exception
    void transaction(std::function<void(WriteConn &)> f);

    sqlite3 *handle() const NOVAINFRA_NOEXCEPT { return db_; }

private:
    friend class ConnectionPool;
    WriteConn(sqlite3 *db, std::shared_ptr<void> lease);

    sqlite3 *db_{nullptr};
    std::shared_ptr<void> lease_{};
};

}  // namespace sqlitepool
NOVAINFRA_NAMESPACE_END
