#pragma once

#include "error.h"
#include "value.h"

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

NOVAINFRA_NAMESPACE_BEGIN
namespace config {

class NOVAINFRA_LIB_PORT Snapshot
{
public:
    Snapshot() : data_(emptyData()) {}
    explicit Snapshot(Value root) : Snapshot(std::move(root), {}) {}

    const Value &root() const noexcept { return data_ ? data_->root : emptyData()->root; }
    const Value *find(const std::string &path) const noexcept { return root().find(path); }
    bool contains(const std::string &path) const noexcept { return find(path) != nullptr; }
    std::string dump(bool redact = true) const
    {
        if (!redact || !data_ || data_->secrets.empty())
            return root().toString(redact);
        Value masked = root();
        redactSecrets(masked, {}, data_->secrets);
        return masked.toString(true);
    }

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
    friend class ConfigManager;
    struct Data {
        Value root;
        std::unordered_set<std::string> secrets;
    };
    Snapshot(Value root, std::vector<std::string> secrets)
        : data_(std::make_shared<const Data>(Data{
              std::move(root),
              {secrets.begin(), secrets.end()}
    }))
    {
    }
    static const std::shared_ptr<const Data> &emptyData()
    {
        static const auto empty = std::make_shared<const Data>(Data{Value(Value::Object{}), {}});
        return empty;
    }
    static void redactSecrets(Value &value, const std::string &prefix,
                              const std::unordered_set<std::string> &secrets)
    {
        if (secrets.find(prefix) != secrets.end()) {
            value = Value("<redacted>");
            return;
        }
        // Match Schema's traversal, including literal TOML keys containing dots.
        if (auto object = std::get_if<Value::Object>(&value.storage()))
            for (auto &entry : *object)
                redactSecrets(entry.second, prefix.empty() ? entry.first : prefix + "." + entry.first,
                              secrets);
    }
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
            long double numeric = 0;
            if (auto p = std::get_if<double>(&value.storage()))
                numeric = *p;
            else if (auto p = std::get_if<std::int64_t>(&value.storage()))
                numeric = *p;
            else if (auto p = std::get_if<std::uint64_t>(&value.storage()))
                numeric = *p;
            else
                return fail();
            if (!std::isfinite(numeric) ||
                numeric > static_cast<long double>(std::numeric_limits<T>::max()) ||
                numeric < -static_cast<long double>(std::numeric_limits<T>::max()))
                return fail();
            const auto converted = static_cast<T>(numeric);
            if (!std::isfinite(converted) || (numeric != 0 && converted == 0))
                return fail();
            return converted;
        } else {
            static_assert(!sizeof(T), "Snapshot::get<T> needs a supported scalar type");
        }
    }

    std::shared_ptr<const Data> data_;
};

}  // namespace config
NOVAINFRA_NAMESPACE_END
