#include <sqlite_pool/statement.h>
#include <sqlite_pool/sqlite_error.h>

#include <array>
#include <charconv>
#include <utility>

NOVAINFRA_NAMESPACE_BEGIN
namespace sqlitepool {

static std::string colToString(sqlite3_stmt *s, int c)
{
    switch (sqlite3_column_type(s, c)) {
        case SQLITE_INTEGER: {
            std::array<char, 32> buffer{};
            const auto result =
                std::to_chars(buffer.data(), buffer.data() + buffer.size(), sqlite3_column_int64(s, c));
            return std::string(buffer.data(), result.ptr);
        }
        case SQLITE_FLOAT:
            return std::to_string(sqlite3_column_double(s, c));
        case SQLITE_NULL:
            return "";
        default: {
            const unsigned char *t = sqlite3_column_text(s, c);
            const int size = sqlite3_column_bytes(s, c);
            return t ? std::string(reinterpret_cast<const char *>(t), static_cast<std::size_t>(size)) : "";
        }
    }
}

static int namedIdx(sqlite3_stmt *s, const std::string &name)
{
    int idx = sqlite3_bind_parameter_index(s, name.c_str());
    if (idx == 0) {
        NOVAINFRA_THROW(SqliteError("Unknown bind parameter: " + name));
    }
    return idx;
}

Statement::Statement(Statement &&o) NOVAINFRA_NOEXCEPT : stmt_(o.stmt_), lease_(std::move(o.lease_))
{
    o.stmt_ = nullptr;
}

Statement &Statement::operator=(Statement &&o) NOVAINFRA_NOEXCEPT
{
    if (this != &o) {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
        stmt_ = o.stmt_;
        lease_ = std::move(o.lease_);
        o.stmt_ = nullptr;
    }
    return *this;
}

Statement::~Statement()
{
    if (stmt_) {
        sqlite3_finalize(stmt_);
    }
}

void Statement::checkRc(int rc, const char *ctx) const
{
    if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE) {
        sqlite3 *db = sqlite3_db_handle(stmt_);
        std::string m = std::string(ctx) + ": " + (db ? sqlite3_errmsg(db) : sqlite3_errstr(rc));
        NOVAINFRA_THROW(SqliteError(m, rc));
    }
}

Statement &Statement::bind(int idx, int val)
{
    checkRc(sqlite3_bind_int(stmt_, idx, val), "bind int");
    return *this;
}

Statement &Statement::bind(int idx, Int64 val)
{
    checkRc(sqlite3_bind_int64(stmt_, idx, val), "bind int64");
    return *this;
}

Statement &Statement::bind(int idx, double val)
{
    checkRc(sqlite3_bind_double(stmt_, idx, val), "bind double");
    return *this;
}

Statement &Statement::bind(int idx, const std::string &val)
{
    checkRc(sqlite3_bind_text(stmt_, idx, val.data(), static_cast<int>(val.size()), SQLITE_TRANSIENT),
            "bind text");
    return *this;
}

Statement &Statement::bind(int idx, std::nullptr_t)
{
    checkRc(sqlite3_bind_null(stmt_, idx), "bind null");
    return *this;
}

Statement &Statement::bind(const std::string &name, int val)
{
    return bind(namedIdx(stmt_, name), val);
}

Statement &Statement::bind(const std::string &name, Int64 val)
{
    return bind(namedIdx(stmt_, name), val);
}

Statement &Statement::bind(const std::string &name, double val)
{
    return bind(namedIdx(stmt_, name), val);
}

Statement &Statement::bind(const std::string &name, const std::string &val)
{
    return bind(namedIdx(stmt_, name), val);
}

Statement &Statement::bind(const std::string &name, std::nullptr_t)
{
    return bind(namedIdx(stmt_, name), nullptr);
}

void Statement::exec()
{
    int rc = sqlite3_step(stmt_);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        checkRc(rc, "exec");
    }
    checkRc(sqlite3_reset(stmt_), "reset after exec");
}

RowSet Statement::query()
{
    RowSet rows;
    const int ncols = sqlite3_column_count(stmt_);
    std::vector<std::string> columnNames;
    columnNames.reserve(static_cast<std::size_t>(ncols));
    for (int c = 0; c < ncols; ++c) {
        const char *name = sqlite3_column_name(stmt_, c);
        columnNames.emplace_back(name ? name : "");
    }
    int rc;
    while ((rc = sqlite3_step(stmt_)) == SQLITE_ROW) {
        Row row;
        row.reserve(static_cast<std::size_t>(ncols));
        for (int c = 0; c < ncols; ++c) {
            row.insert_or_assign(columnNames[static_cast<std::size_t>(c)], colToString(stmt_, c));
        }
        rows.emplace_back(std::move(row));
    }
    if (rc != SQLITE_DONE) {
        checkRc(rc, "query step");
    }
    checkRc(sqlite3_reset(stmt_), "reset after query");
    return rows;
}

bool Statement::step()
{
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    checkRc(rc, "step");
    return false;
}

void Statement::reset()
{
    if (stmt_) {
        checkRc(sqlite3_reset(stmt_), "reset");
    }
}

int Statement::columnInt(int col) const
{
    return sqlite3_column_int(stmt_, col);
}

Int64 Statement::columnInt64(int col) const
{
    return sqlite3_column_int64(stmt_, col);
}

double Statement::columnDouble(int col) const
{
    return sqlite3_column_double(stmt_, col);
}

std::string Statement::columnText(int col) const
{
    const auto view = columnTextView(col);
    return view.empty() ? std::string{} : std::string(view.data(), view.size());
}

std::string_view Statement::columnTextView(int col) const
{
    const unsigned char *t = sqlite3_column_text(stmt_, col);
    if (!t) {
        return {};
    }
    const int size = sqlite3_column_bytes(stmt_, col);
    return {reinterpret_cast<const char *>(t), static_cast<std::size_t>(size)};
}

bool Statement::columnNull(int col) const
{
    return (sqlite3_column_type(stmt_, col) == SQLITE_NULL);
}

int Statement::columnCount() const
{
    return sqlite3_column_count(stmt_);
}

std::string Statement::columnName(int col) const
{
    const char *n = sqlite3_column_name(stmt_, col);
    return n ? n : "";
}


}  // namespace sqlitepool
NOVAINFRA_NAMESPACE_END
