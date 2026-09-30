#include <config/error.h>

#include <sstream>

NOVAINFRA_NAMESPACE_BEGIN
namespace config {

std::string Error::message() const
{
    std::ostringstream out;
    for (std::size_t i = 0; i < diagnostics.size(); ++i) {
        if (i)
            out << "\n";
        const auto &d = diagnostics[i];
        if (!d.source.empty())
            out << d.source << ':' << d.line << ':' << d.column << ": ";
        if (!d.key.empty())
            out << d.key << ": ";
        out << d.message;
    }
    return out.str();
}

}  // namespace config
NOVAINFRA_NAMESPACE_END
