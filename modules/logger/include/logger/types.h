#pragma once

#include <core/macros.h>
#include <core/types.h>
#include <core/export.h>

#include <fmt/format.h>

#include <functional>
#include <string>
#include <string_view>

#define NOVAINFRA_LOG_LEVEL_TRACE    0
#define NOVAINFRA_LOG_LEVEL_DEBUG    1
#define NOVAINFRA_LOG_LEVEL_INFO     2
#define NOVAINFRA_LOG_LEVEL_WARN     3
#define NOVAINFRA_LOG_LEVEL_ERROR    4
#define NOVAINFRA_LOG_LEVEL_CRITICAL 5
#define NOVAINFRA_LOG_LEVEL_OFF      6

NOVAINFRA_NAMESPACE_BEGIN
namespace log {

using ErrHandler = std::function<void(const std::string &errMsg)>;

enum class Level : int {
    TRACE = NOVAINFRA_LOG_LEVEL_TRACE,
    DEBUG = NOVAINFRA_LOG_LEVEL_DEBUG,
    INFO = NOVAINFRA_LOG_LEVEL_INFO,
    WARN = NOVAINFRA_LOG_LEVEL_WARN,
    ERROR = NOVAINFRA_LOG_LEVEL_ERROR,
    CRITICAL = NOVAINFRA_LOG_LEVEL_CRITICAL,
    OFF = NOVAINFRA_LOG_LEVEL_OFF,
};

struct SourceLocation {
    NOVAINFRA_CONSTEXPR SourceLocation() = default;
    NOVAINFRA_CONSTEXPR SourceLocation(std::string_view filenameIn, int lineIn, std::string_view funcnameIn)
        : file{filenameIn}, line{lineIn}, function{funcnameIn}
    {
    }

    static NOVAINFRA_CONSTEXPR SourceLocation fromStatic(std::string_view filenameIn, int lineIn,
                                                         std::string_view funcnameIn)
    {
        return SourceLocation(filenameIn, lineIn, funcnameIn, true);
    }

    NOVAINFRA_CONSTEXPR bool empty() const NOVAINFRA_NOEXCEPT { return line <= 0; }
    NOVAINFRA_CONSTEXPR bool hasStaticStorage() const NOVAINFRA_NOEXCEPT { return hasStaticStorage_; }

    std::string_view file;
    int line = 0;
    std::string_view function;

private:
    NOVAINFRA_CONSTEXPR SourceLocation(std::string_view filenameIn, int lineIn, std::string_view funcnameIn,
                                       bool hasStaticStorage)
        : file{filenameIn}, line{lineIn}, function{funcnameIn}, hasStaticStorage_{hasStaticStorage}
    {
    }

    bool hasStaticStorage_{false};
};

template <typename Char>
#if FMT_VERSION >= 90101
using FmtRuntimeString = fmt::runtime_format_string<Char>;
#else
using FmtRuntimeString = fmt::basic_runtime<Char>;
#endif

}  // namespace log
NOVAINFRA_NAMESPACE_END
