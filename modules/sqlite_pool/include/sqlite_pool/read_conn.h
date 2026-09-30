#pragma once


#include <sqlite_pool/types.h>
#include <sqlite_pool/statement.h>

#include <core/macros.h>
#include <mutex>
#include <condition_variable>
#include <memory>


NOVAINFRA_NAMESPACE_BEGIN
namespace sqlitepool {

class ReadConn
{
public:
    ReadConn() = delete;
    NOVAINFRA_DISABLE_COPY(ReadConn)
    ReadConn(ReadConn &&) NOVAINFRA_NOEXCEPT;
    ReadConn &operator=(ReadConn &&) NOVAINFRA_NOEXCEPT;
    ~ReadConn();

    RowSet query(const std::string &sql);
    Statement prepare(const std::string &sql);
    sqlite3 *handle() const NOVAINFRA_NOEXCEPT { return db_; }

private:
    friend class ConnectionPool;
    ReadConn(sqlite3 *db, std::shared_ptr<void> lease);

    sqlite3 *db_{nullptr};
    std::shared_ptr<void> lease_{};
};

}  // namespace sqlitepool
NOVAINFRA_NAMESPACE_END
