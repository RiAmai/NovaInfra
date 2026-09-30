#pragma once

#include "error.h"
#include "types.h"
#include "value.h"

#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

NOVAINFRA_NAMESPACE_BEGIN
namespace config {

class NOVAINFRA_LIB_PORT Schema
{
public:
    using Validator = std::function<std::optional<std::string>(const Value &)>;

    Schema() = default;

    template <typename T>
    Schema &required(const std::string &path, FieldConstraints constraints = {}, bool secret = false,
                     Validator validator = {})
    {
        add(path, valueType<T>(), std::nullopt, true, constraints, secret, std::move(validator));
        return *this;
    }

    template <typename T>
    Schema &optional(const std::string &path, T defaultValue, FieldConstraints constraints = {},
                     bool secret = false, Validator validator = {})
    {
        add(path, valueType<T>(), Value(makeValue(std::move(defaultValue))), false, constraints, secret,
            std::move(validator));
        return *this;
    }

    Schema &allowUnknown(const std::string &prefix);
    Result<Value> parse(const std::string &path, const std::string &raw) const;
    Result<void> validate(Value &root) const;
    bool knows(const std::string &path) const;
    bool isSecret(const std::string &path) const;

private:
    struct Field {
        std::string path;
        Value::Type type;
        std::optional<Value> defaultValue;
        bool required;
        FieldConstraints constraints;
        bool secret;
        Validator validator;
    };

    template <typename T>
    static Value makeValue(T value)
    {
        if constexpr (std::is_same<T, bool>::value)
            return Value(value);
        else if constexpr (std::is_integral<T>::value && std::is_signed<T>::value)
            return Value(static_cast<std::int64_t>(value));
        else if constexpr (std::is_integral<T>::value)
            return Value(static_cast<std::uint64_t>(value));
        else if constexpr (std::is_floating_point<T>::value)
            return Value(static_cast<double>(value));
        else
            return Value(std::move(value));
    }

    template <typename T>
    static Value::Type valueType()
    {
        if constexpr (std::is_same<T, Value>::value)
            return Value::Type::NULL_VALUE;
        else if constexpr (std::is_same<T, bool>::value)
            return Value::Type::BOOLEAN;
        else if constexpr (std::is_integral<T>::value && std::is_signed<T>::value)
            return Value::Type::SIGNED_INTEGER;
        else if constexpr (std::is_integral<T>::value)
            return Value::Type::UNSIGNED_INTEGER;
        else if constexpr (std::is_floating_point<T>::value)
            return Value::Type::FLOATING;
        else
            return Value::Type::STRING;
    }

    void add(std::string path, Value::Type type, std::optional<Value> defaultValue, bool required,
             FieldConstraints constraints, bool secret, Validator validator);
    std::vector<Field> fields_;
    std::vector<std::string> allowedPrefixes_;
};

}  // namespace config
NOVAINFRA_NAMESPACE_END
