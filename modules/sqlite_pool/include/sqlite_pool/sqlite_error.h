#pragma once

#include <sqlite_pool/types.h>

#include <core/macros.h>
#include <stdexcept>

NOVAINFRA_NAMESPACE_BEGIN
namespace sqlitepool {

class SqliteError : public std::runtime_error
{
public:
    explicit SqliteError(const std::string &msg, int code = SQLITE_ERROR)
        : std::runtime_error(msg), code_(code)
    {
    }

    int code() const NOVAINFRA_NOEXCEPT { return code_; }

private:
    int code_;
};

}  // namespace sqlitepool
NOVAINFRA_NAMESPACE_END