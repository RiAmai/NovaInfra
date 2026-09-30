#include <sqlite_pool/read_conn.h>
#include <sqlite_pool/sqlite_error.h>

NOVAINFRA_NAMESPACE_BEGIN
namespace sqlitepool {

ReadConn::ReadConn(sqlite3 *db, std::shared_ptr<void> lease) : db_(db), lease_(std::move(lease)) {}

ReadConn::ReadConn(ReadConn &&o) NOVAINFRA_NOEXCEPT : db_(o.db_), lease_(std::move(o.lease_))
{
    o.db_ = nullptr;
}

ReadConn &ReadConn::operator=(ReadConn &&o) NOVAINFRA_NOEXCEPT
{
    if (this != &o) {
        db_ = o.db_;
        lease_ = std::move(o.lease_);
        o.db_ = nullptr;
    }
    return *this;
}

ReadConn::~ReadConn() = default;

RowSet ReadConn::query(const std::string &sql)
{
    return prepare(sql).query();
}

Statement ReadConn::prepare(const std::string &sql)
{
    sqlite3_stmt *s = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.c_str(), static_cast<int>(sql.size()), &s, nullptr);
    if (rc != SQLITE_OK) {
        NOVAINFRA_THROW(SqliteError(std::string("prepare: ") + sqlite3_errmsg(db_), rc));
    }
    return Statement(s, lease_);
}

}  // namespace sqlitepool
NOVAINFRA_NAMESPACE_END
