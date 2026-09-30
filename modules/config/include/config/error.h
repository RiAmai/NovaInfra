#pragma once

#include "types.h"

#include <core/namespace.h>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

NOVAINFRA_NAMESPACE_BEGIN
namespace config {

struct NOVAINFRA_LIB_PORT Error {
    std::vector<Diagnostic> diagnostics;

    Error() = default;
    explicit Error(Diagnostic diagnostic) : diagnostics{std::move(diagnostic)} {}

    bool empty() const noexcept { return diagnostics.empty(); }
    std::string message() const;
};

template <typename T>
class Result
{
public:
    Result(const T &value) : value_(value) {}
    Result(T &&value) : value_(std::move(value)) {}
    Result(const Error &error) : error_(error) {}
    Result(Error &&error) : error_(std::move(error)) {}

    bool hasValue() const noexcept { return value_.has_value(); }
    explicit operator bool() const noexcept { return hasValue(); }
    T &value() & { return *value_; }
    const T &value() const & { return *value_; }
    T &&value() && { return std::move(*value_); }
    const Error &error() const noexcept { return error_; }

private:
    std::optional<T> value_;
    Error error_;
};

template <>
class Result<void>
{
public:
    Result() = default;
    Result(const Error &error) : error_(error) {}
    Result(Error &&error) : error_(std::move(error)) {}

    bool hasValue() const noexcept { return error_.empty(); }
    explicit operator bool() const noexcept { return hasValue(); }
    const Error &error() const noexcept { return error_; }

private:
    Error error_;
};

}  // namespace config
NOVAINFRA_NAMESPACE_END
