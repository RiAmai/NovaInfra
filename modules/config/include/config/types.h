#pragma once

#include <core/export.h>
#include <core/namespace.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

NOVAINFRA_NAMESPACE_BEGIN
namespace config {

enum class Severity { WARNING, ERROR };

enum class ErrorCode {
    FILE_NOT_FOUND,
    FILE_READ,
    PARSE,
    INVALID_COMMAND_LINE,
    TYPE_MISMATCH,
    MISSING_REQUIRED,
    INVALID_VALUE,
    UNKNOWN_KEY,
    DUPLICATE_KEY,
    RELOAD_FAILED,
};

struct NOVAINFRA_LIB_PORT Diagnostic {
    Severity severity = Severity::ERROR;
    ErrorCode code = ErrorCode::INVALID_VALUE;
    std::string key;
    std::string source;
    std::size_t line = 0;
    std::size_t column = 0;
    std::string message;
};

struct FieldConstraints {
    std::optional<double> min;
    std::optional<double> max;
    std::optional<std::size_t> minLength;
    std::optional<std::size_t> maxLength;
    std::vector<std::string> allowed;
};

struct FileSpec {
    std::filesystem::path path;
    bool required = true;
};

struct WatchOptions {
    std::chrono::milliseconds interval{1000};
    std::chrono::milliseconds debounce{200};
};

}  // namespace config
NOVAINFRA_NAMESPACE_END
