#pragma once

#include "error.h"
#include "value.h"

#include <cmath>
#include <limits>
#include <string>
#include <type_traits>

NOVAINFRA_NAMESPACE_BEGIN
namespace config {

class NOVAINFRA_LIB_PORT Snapshot
{
public:
    Snapshot() = default;
    explicit Snapshot(Value root) : root_(std::move(root)) {}

    const Value &root() const noexcept { return root_; }
    const Value *find(const std::string &path) const noexcept { return root_.find(path); }
    bool contains(const std::string &path) const noexcept { return find(path) != nullptr; }
    std::string dump(bool redact = true) const { return root_.toString(redact); }

    template <typename T>
    Result<T> get(const std::string &path) const
    {
        const auto *value = find(path);
        if (!value)
            return Error(Diagnostic{Severity::ERROR, ErrorCode::MISSING_REQUIRED, path, "snapshot", 0, 0,
                                    "configuration key is missing"});
        return convert<T>(*value, path);
    }

private:
    template <typename T>
    static Result<T> convert(const Value &value, const std::string &path)
    {
        const auto fail = [&] {
            return Result<T>(Error(Diagnostic{Severity::ERROR, ErrorCode::TYPE_MISMATCH, path, "snapshot", 0,
                                              0, "configuration value has an incompatible type"}));
        };
        if constexpr (std::is_same<T, Value>::value)
            return value;
        else if constexpr (std::is_same<T, std::string>::value) {
            if (auto p = std::get_if<std::string>(&value.storage()))
                return *p;
            return fail();
        } else if constexpr (std::is_same<T, bool>::value) {
            if (auto p = std::get_if<bool>(&value.storage()))
                return *p;
            return fail();
        } else if constexpr (std::is_integral<T>::value && std::is_signed<T>::value) {
            if (auto p = std::get_if<std::int64_t>(&value.storage())) {
                if (*p >= static_cast<std::int64_t>(std::numeric_limits<T>::min()) &&
                    *p <= static_cast<std::int64_t>(std::numeric_limits<T>::max()))
                    return static_cast<T>(*p);
            }
            if (auto p = std::get_if<std::uint64_t>(&value.storage());
                p && *p <= static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
                return static_cast<T>(*p);
            return fail();
        } else if constexpr (std::is_integral<T>::value) {
            if (auto p = std::get_if<std::uint64_t>(&value.storage());
                p && *p <= static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
                return static_cast<T>(*p);
            if (auto p = std::get_if<std::int64_t>(&value.storage());
                p && *p >= 0 &&
                static_cast<std::uint64_t>(*p) <= static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
                return static_cast<T>(*p);
            return fail();
        } else if constexpr (std::is_floating_point<T>::value) {
            if (auto p = std::get_if<double>(&value.storage()))
                return static_cast<T>(*p);
            if (auto p = std::get_if<std::int64_t>(&value.storage()))
                return static_cast<T>(*p);
            if (auto p = std::get_if<std::uint64_t>(&value.storage()))
                return static_cast<T>(*p);
            return fail();
        } else {
            static_assert(!sizeof(T), "Snapshot::get<T> needs a supported scalar type");
        }
    }

    Value root_{Value::Object{}};
};

}  // namespace config
NOVAINFRA_NAMESPACE_END
