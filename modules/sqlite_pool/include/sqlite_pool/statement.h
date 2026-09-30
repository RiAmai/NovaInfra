#pragma once

#include <sqlite_pool/types.h>
#include <core/macros.h>

#include <memory>
#include <string_view>

NOVAINFRA_NAMESPACE_BEGIN
namespace sqlitepool {

class Statement
{
public:
    Statement() = default;
    NOVAINFRA_DISABLE_COPY(Statement)
    Statement(Statement &&) NOVAINFRA_NOEXCEPT;
    Statement &operator=(Statement &&) NOVAINFRA_NOEXCEPT;
    ~Statement();

    Statement &bind(int idx, int val);
    Statement &bind(int idx, Int64 val);
    Statement &bind(int idx, double val);
    Statement &bind(int idx, const std::string &val);
    Statement &bind(int idx, std::nullptr_t);

    Statement &bind(const std::string &name, int val);
    Statement &bind(const std::string &name, Int64 val);
    Statement &bind(const std::string &name, double val);
    Statement &bind(const std::string &name, const std::string &val);
    Statement &bind(const std::string &name, std::nullptr_t);

    void exec();
    RowSet query();

    bool step();
    void reset();
    bool valid() const NOVAINFRA_NOEXCEPT { return stmt_ != nullptr; }

    // Column accessors (call after step() == true)
    int columnInt(int col) const;
    Int64 columnInt64(int col) const;
    double columnDouble(int col) const;
    std::string columnText(int col) const;
    // The view remains valid until the next step(), reset(), or statement destruction.
    std::string_view columnTextView(int col) const;
    bool columnNull(int col) const;
    int columnCount() const;
    std::string columnName(int col) const;

private:
    friend class WriteConn;
    friend class ReadConn;
    explicit Statement(sqlite3_stmt *s, std::shared_ptr<void> lease = {}) : stmt_(s), lease_(std::move(lease))
    {
    }
    void checkRc(int rc, const char *ctx) const;

    sqlite3_stmt *stmt_{nullptr};
    std::shared_ptr<void> lease_{};
};

}  // namespace sqlitepool
NOVAINFRA_NAMESPACE_END
