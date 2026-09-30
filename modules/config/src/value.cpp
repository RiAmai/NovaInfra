#include <config/value.h>

#include <sstream>

NOVAINFRA_NAMESPACE_BEGIN
namespace config {

namespace {
std::vector<std::string> parts(const std::string &path)
{
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto end = path.find('.', start);
        const auto part = path.substr(start, end == std::string::npos ? end : end - start);
        if (!part.empty())
            result.push_back(part);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return result;
}

void dumpValue(std::ostringstream &out, const Value &value, bool redact)
{
    if (std::holds_alternative<std::monostate>(value.storage()))
        out << "null";
    else if (const auto p = std::get_if<bool>(&value.storage()))
        out << (*p ? "true" : "false");
    else if (const auto p = std::get_if<std::int64_t>(&value.storage()))
        out << *p;
    else if (const auto p = std::get_if<std::uint64_t>(&value.storage()))
        out << *p;
    else if (const auto p = std::get_if<double>(&value.storage()))
        out << *p;
    else if (const auto p = std::get_if<std::string>(&value.storage())) {
        if (redact)
            out << "\"<redacted>\"";
        else
            out << '"' << *p << '"';
    } else if (const auto p = std::get_if<Value::Array>(&value.storage())) {
        out << '[';
        for (std::size_t i = 0; i < p->size(); ++i) {
            if (i)
                out << ',';
            dumpValue(out, (*p)[i], redact);
        }
        out << ']';
    } else {
        const auto &object = std::get<Value::Object>(value.storage());
        out << '{';
        bool first = true;
        for (const auto &item : object) {
            if (!first)
                out << ',';
            first = false;
            out << '"' << item.first << "\":";
            dumpValue(out, item.second, redact);
        }
        out << '}';
    }
}
}  // namespace

Value::Type Value::type() const noexcept
{
    switch (storage_.index()) {
        case 1:
            return Type::BOOLEAN;
        case 2:
            return Type::SIGNED_INTEGER;
        case 3:
            return Type::UNSIGNED_INTEGER;
        case 4:
            return Type::FLOATING;
        case 5:
            return Type::STRING;
        case 6:
            return Type::ARRAY;
        case 7:
            return Type::OBJECT;
        default:
            return Type::NULL_VALUE;
    }
}

bool Value::isScalar() const noexcept
{
    return type() != Type::ARRAY && type() != Type::OBJECT;
}

const Value *Value::find(const std::string &path) const noexcept
{
    const Value *current = this;
    for (const auto &part : parts(path)) {
        const auto object = std::get_if<Object>(&current->storage_);
        if (!object)
            return nullptr;
        const auto found = object->find(part);
        if (found == object->end())
            return nullptr;
        current = &found->second;
    }
    return current;
}

Value *Value::find(const std::string &path) noexcept
{
    return const_cast<Value *>(static_cast<const Value &>(*this).find(path));
}

const Value &Value::at(const std::string &path) const
{
    const auto *value = find(path);
    static const Value empty;
    return value ? *value : empty;
}

Value &Value::operator[](const std::string &path)
{
    auto names = parts(path);
    if (!std::holds_alternative<Object>(storage_))
        storage_ = Object{};
    Value *current = this;
    for (const auto &name : names) {
        auto &object = std::get<Object>(current->storage_);
        current = &object[name];
        if (&name != &names.back() && !std::holds_alternative<Object>(current->storage_))
            current->storage_ = Object{};
    }
    return *current;
}

void Value::merge(const Value &overlay)
{
    if (auto base = std::get_if<Object>(&storage_)) {
        if (const auto incoming = std::get_if<Object>(&overlay.storage_)) {
            for (const auto &entry : *incoming) {
                auto it = base->find(entry.first);
                if (it == base->end())
                    base->emplace(entry.first, entry.second);
                else
                    it->second.merge(entry.second);
            }
            return;
        }
    }
    storage_ = overlay.storage_;
}

std::string Value::toString(bool redact) const
{
    std::ostringstream out;
    dumpValue(out, *this, redact);
    return out.str();
}

}  // namespace config
NOVAINFRA_NAMESPACE_END
