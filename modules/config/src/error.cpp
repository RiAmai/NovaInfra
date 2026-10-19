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

        // Error code prefix for better categorization
        switch (d.code) {
            case ErrorCode::FILE_NOT_FOUND:
                out << "[Config Error - File Not Found] ";
                break;
            case ErrorCode::FILE_READ:
                out << "[Config Error - File Read] ";
                break;
            case ErrorCode::PARSE:
                out << "[Config Error - Parse Error] ";
                break;
            case ErrorCode::INVALID_COMMAND_LINE:
                out << "[Config Error - Invalid Command Line] ";
                break;
            case ErrorCode::TYPE_MISMATCH:
                out << "[Config Error - Type Mismatch] ";
                break;
            case ErrorCode::MISSING_REQUIRED:
                out << "[Config Error - Missing Required Key] ";
                break;
            case ErrorCode::INVALID_VALUE:
                out << "[Config Error - Invalid Value] ";
                break;
            case ErrorCode::UNKNOWN_KEY:
                out << "[Config Error - Unknown Key] ";
                break;
            case ErrorCode::DUPLICATE_KEY:
                out << "[Config Error - Duplicate Key] ";
                break;
            case ErrorCode::RELOAD_FAILED:
                out << "[Config Error - Reload Failed] ";
                break;
        }

        // Location information
        if (!d.source.empty())
            out << d.source << ':' << d.line << ':' << d.column << ": ";

        // Key information
        if (!d.key.empty())
            out << "'" << d.key << "': ";

        // Error message
        out << d.message;
    }
    return out.str();
}

}  // namespace config
NOVAINFRA_NAMESPACE_END
