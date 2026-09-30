#include <sqlite_pool/write_conn.h>
#include <sqlite_pool/sqlite_error.h>

#include <utility>

NOVAINFRA_NAMESPACE_BEGIN
namespace sqlitepool {

WriteConn::WriteConn(sqlite3 *db, std::shared_ptr<void> lease) : db_(db), lease_(std::move(lease)) {}

WriteConn::WriteConn(WriteConn &&o) NOVAINFRA_NOEXCEPT : db_(o.db_), lease_(std::move(o.lease_))
{
    o.db_ = nullptr;
}

WriteConn &WriteConn::operator=(WriteConn &&o) NOVAINFRA_NOEXCEPT
{
    if (this != &o) {
        db_ = o.db_;
        lease_ = std::move(o.lease_);
        o.db_ = nullptr;
    }
    return *this;
}

WriteConn::~WriteConn() = default;

void WriteConn::exec(const std::string &sql)
{
    char *err = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        const std::string msg = err ? err : sqlite3_errstr(rc);
        sqlite3_free(err);
        NOVAINFRA_THROW(SqliteError("exec: " + msg, rc));
    }
}

Statement WriteConn::prepare(const std::string &sql)
{
    sqlite3_stmt *s = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.c_str(), static_cast<int>(sql.size()), &s, nullptr);
    if (rc != SQLITE_OK) {
        NOVAINFRA_THROW(SqliteError(std::string("prepare: ") + sqlite3_errmsg(db_), rc));
    }
    return Statement(s, lease_);
}

void WriteConn::begin()
{
    exec("BEGIN");
}
void WriteConn::commit()
{
    exec("COMMIT");
}
void WriteConn::rollback()
{
    exec("ROLLBACK");
}

void WriteConn::transaction(std::function<void(WriteConn &)> f)
{
    begin();
#ifdef NOVAINFRA_NO_EXCEPTIONS
    f(*this);
    commit();
#else
    NOVAINFRA_TRY
    {
        f(*this);
        commit();
    }
    NOVAINFRA_CATCH_ALL_BODY({
        NOVAINFRA_TRY
        {
            rollback();
        }
        NOVAINFRA_CATCH_ALL_BODY({})
        NOVAINFRA_RETHROW_CURRENT();
    })
#endif
}

}  // namespace sqlitepool
NOVAINFRA_NAMESPACE_END
