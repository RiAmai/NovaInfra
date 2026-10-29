#pragma once

#include <core/export.h>
#include <core/macros.h>
#include <core/namespace.h>

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

NOVAINFRA_NAMESPACE_BEGIN
namespace config {

class NOVAINFRA_LIB_PORT Value
{
public:
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;
    using Storage =
        std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double, std::string, Array, Object>;

    enum class Type {
        NULL_VALUE,
        BOOLEAN,
        SIGNED_INTEGER,
        UNSIGNED_INTEGER,
        FLOATING,
        STRING,
        ARRAY,
        OBJECT
    };

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool value) : storage_(value) {}
    Value(std::int64_t value) : storage_(value) {}
    Value(std::uint64_t value) : storage_(value) {}
    Value(double value) : storage_(value) {}
    Value(const char *value) : storage_(std::string(value)) {}
    Value(std::string value) : storage_(std::move(value)) {}
    Value(Array value) : storage_(std::move(value)) {}
    Value(Object value) : storage_(std::move(value)) {}

    Type type() const NOVAINFRA_NOEXCEPT;
    bool isScalar() const NOVAINFRA_NOEXCEPT;
    const Storage &storage() const NOVAINFRA_NOEXCEPT { return storage_; }
    Storage &storage() NOVAINFRA_NOEXCEPT { return storage_; }

    const Value *find(const std::string &path) const NOVAINFRA_NOEXCEPT;
    Value *find(const std::string &path) NOVAINFRA_NOEXCEPT;
    const Value &at(const std::string &path) const;
    Value &operator[](const std::string &path);
    void merge(const Value &overlay);
    std::string toString(bool redact = false) const;

private:
    Storage storage_;
};

}  // namespace config
NOVAINFRA_NAMESPACE_END
