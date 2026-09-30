#include <config/schema.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <sstream>

NOVAINFRA_NAMESPACE_BEGIN
namespace config {

void Schema::add(std::string path, Value::Type type, std::optional<Value> defaultValue, bool required,
                 FieldConstraints constraints, bool secret, Validator validator)
{
    fields_.push_back(Field{std::move(path), type, std::move(defaultValue), required, std::move(constraints),
                            secret, std::move(validator)});
}

Schema &Schema::allowUnknown(const std::string &prefix)
{
    allowedPrefixes_.push_back(prefix);
    return *this;
}

Result<Value> Schema::parse(const std::string &path, const std::string &raw) const
{
    const auto it =
        std::find_if(fields_.begin(), fields_.end(), [&](const Field &field) { return field.path == path; });
    if (it == fields_.end())
        return Value(raw);
    const auto fail = [&] {
        return Result<Value>(Error(Diagnostic{Severity::ERROR, ErrorCode::TYPE_MISMATCH, path, "source", 0, 0,
                                              "source value cannot be converted to the declared type"}));
    };
    if (it->type == Value::Type::STRING)
        return Value(raw);
    if (it->type == Value::Type::BOOLEAN) {
        if (raw == "true" || raw == "TRUE" || raw == "1" || raw == "yes" || raw == "on")
            return Value(true);
        if (raw == "false" || raw == "FALSE" || raw == "0" || raw == "no" || raw == "off")
            return Value(false);
        return fail();
    }
    char *end = nullptr;
    if (it->type == Value::Type::SIGNED_INTEGER) {
        auto first = raw.data();
        if (first != raw.data() + raw.size() && *first == '+')
            ++first;
        auto base = 10;
        if (raw.data() + raw.size() - first >= 2 && first[0] == '0' && (first[1] == 'x' || first[1] == 'X')) {
            first += 2;
            base = 16;
        } else if (raw.data() + raw.size() - first >= 3 && first[0] == '-' && first[1] == '0' &&
                   (first[2] == 'x' || first[2] == 'X')) {
            std::uint64_t magnitude = 0;
            const auto digits = first + 3;
            auto parsed = std::from_chars(digits, raw.data() + raw.size(), magnitude, 16);
            const auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1;
            if (parsed.ec == std::errc{} && parsed.ptr == raw.data() + raw.size() && magnitude <= limit)
                return Value(magnitude == limit ? std::numeric_limits<std::int64_t>::min()
                                                : -static_cast<std::int64_t>(magnitude));
            return fail();
        }
        std::int64_t value = 0;
        auto parsed = std::from_chars(first, raw.data() + raw.size(), value, base);
        return parsed.ec == std::errc{} && parsed.ptr == raw.data() + raw.size() ? Result<Value>(Value(value))
                                                                                 : fail();
    }
    if (it->type == Value::Type::UNSIGNED_INTEGER) {
        auto first = raw.data();
        if (first != raw.data() + raw.size() && *first == '-')
            return fail();
        if (first != raw.data() + raw.size() && *first == '+')
            ++first;
        auto base = 10;
        if (raw.data() + raw.size() - first >= 2 && first[0] == '0' && (first[1] == 'x' || first[1] == 'X')) {
            first += 2;
            base = 16;
        }
        std::uint64_t value = 0;
        auto parsed = std::from_chars(first, raw.data() + raw.size(), value, base);
        return parsed.ec == std::errc{} && parsed.ptr == raw.data() + raw.size() ? Result<Value>(Value(value))
                                                                                 : fail();
    }
    if (it->type == Value::Type::FLOATING) {
        if (raw.empty() || std::isspace(static_cast<unsigned char>(raw.front())))
            return fail();
        errno = 0;
        const auto value = std::strtod(raw.c_str(), &end);
        return end != raw.c_str() && *end == '\0' && errno == 0 && std::isfinite(value)
                   ? Result<Value>(Value(value))
                   : fail();
    }
    return Value(raw);
}

bool Schema::knows(const std::string &path) const
{
    return std::any_of(fields_.begin(), fields_.end(),
                       [&](const Field &field) {
                           return field.path == path ||
                                  (field.type == Value::Type::NULL_VALUE && path.size() > field.path.size() &&
                                   path.compare(0, field.path.size(), field.path) == 0 &&
                                   path[field.path.size()] == '.');
                       }) ||
           std::any_of(allowedPrefixes_.begin(), allowedPrefixes_.end(), [&](const std::string &prefix) {
               if (prefix.empty())
                   return true;
               return path == prefix ||
                      (path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0 &&
                       path[prefix.size()] == '.');
           });
}

bool Schema::isSecret(const std::string &path) const
{
    const auto it =
        std::find_if(fields_.begin(), fields_.end(), [&](const Field &field) { return field.path == path; });
    return it != fields_.end() && it->secret;
}

namespace {
void collectLeaves(const Value &value, const std::string &prefix,
                   std::vector<std::pair<std::string, const Value *>> &out)
{
    if (const auto object = std::get_if<Value::Object>(&value.storage())) {
        for (const auto &entry : *object)
            collectLeaves(entry.second, prefix.empty() ? entry.first : prefix + "." + entry.first, out);
    } else
        out.emplace_back(prefix, &value);
}

bool matches(Value::Type expected, Value::Type actual)
{
    if (expected == Value::Type::NULL_VALUE)
        return true;
    if (expected == actual)
        return true;
    return expected == Value::Type::FLOATING &&
           (actual == Value::Type::SIGNED_INTEGER || actual == Value::Type::UNSIGNED_INTEGER);
}
}  // namespace

Result<void> Schema::validate(Value &root) const
{
    Error error;
    for (const auto &field : fields_) {
        auto *value = root.find(field.path);
        if (!value && field.defaultValue) {
            root[field.path] = *field.defaultValue;
            value = root.find(field.path);
        }
        if (!value) {
            if (field.required)
                error.diagnostics.push_back({Severity::ERROR, ErrorCode::MISSING_REQUIRED, field.path,
                                             "schema", 0, 0, "required configuration key is missing"});
            continue;
        }
        if (!matches(field.type, value->type())) {
            error.diagnostics.push_back({Severity::ERROR, ErrorCode::TYPE_MISMATCH, field.path, "schema", 0,
                                         0, "configuration value has an incompatible type"});
            continue;
        }
        if (const auto p = std::get_if<double>(&value->storage()); p && !std::isfinite(*p)) {
            error.diagnostics.push_back({Severity::ERROR, ErrorCode::INVALID_VALUE, field.path, "schema", 0,
                                         0, "value must be finite"});
            continue;
        }
        const auto &constraints = field.constraints;
        if (const auto p = std::get_if<std::int64_t>(&value->storage())) {
            if (constraints.min && static_cast<double>(*p) < *constraints.min)
                error.diagnostics.push_back({Severity::ERROR, ErrorCode::INVALID_VALUE, field.path, "schema",
                                             0, 0, "value is below the minimum"});
            if (constraints.max && static_cast<double>(*p) > *constraints.max)
                error.diagnostics.push_back({Severity::ERROR, ErrorCode::INVALID_VALUE, field.path, "schema",
                                             0, 0, "value is above the maximum"});
        }
        if (const auto p = std::get_if<std::uint64_t>(&value->storage())) {
            if (constraints.min && static_cast<double>(*p) < *constraints.min)
                error.diagnostics.push_back({Severity::ERROR, ErrorCode::INVALID_VALUE, field.path, "schema",
                                             0, 0, "value is below the minimum"});
            if (constraints.max && static_cast<double>(*p) > *constraints.max)
                error.diagnostics.push_back({Severity::ERROR, ErrorCode::INVALID_VALUE, field.path, "schema",
                                             0, 0, "value is above the maximum"});
        }
        if (const auto p = std::get_if<double>(&value->storage())) {
            if (constraints.min && *p < *constraints.min)
                error.diagnostics.push_back({Severity::ERROR, ErrorCode::INVALID_VALUE, field.path, "schema",
                                             0, 0, "value is below the minimum"});
            if (constraints.max && *p > *constraints.max)
                error.diagnostics.push_back({Severity::ERROR, ErrorCode::INVALID_VALUE, field.path, "schema",
                                             0, 0, "value is above the maximum"});
        }
        if (const auto p = std::get_if<std::string>(&value->storage())) {
            if (constraints.minLength && p->size() < *constraints.minLength)
                error.diagnostics.push_back({Severity::ERROR, ErrorCode::INVALID_VALUE, field.path, "schema",
                                             0, 0, "string is shorter than the minimum"});
            if (constraints.maxLength && p->size() > *constraints.maxLength)
                error.diagnostics.push_back({Severity::ERROR, ErrorCode::INVALID_VALUE, field.path, "schema",
                                             0, 0, "string is longer than the maximum"});
            if (!constraints.allowed.empty() &&
                std::find(constraints.allowed.begin(), constraints.allowed.end(), *p) ==
                    constraints.allowed.end())
                error.diagnostics.push_back({Severity::ERROR, ErrorCode::INVALID_VALUE, field.path, "schema",
                                             0, 0, "value is not in the allowed set"});
        }
        if (field.validator) {
            if (auto message = field.validator(*value))
                error.diagnostics.push_back(
                    {Severity::ERROR, ErrorCode::INVALID_VALUE, field.path, "schema", 0, 0, *message});
        }
    }
    std::vector<std::pair<std::string, const Value *>> leaves;
    collectLeaves(root, {}, leaves);
    for (const auto &leaf : leaves)
        if (!knows(leaf.first))
            error.diagnostics.push_back({Severity::ERROR, ErrorCode::UNKNOWN_KEY, leaf.first, "schema", 0, 0,
                                         "unknown configuration key"});
    return error.empty() ? Result<void>() : Result<void>(std::move(error));
}

}  // namespace config
NOVAINFRA_NAMESPACE_END
