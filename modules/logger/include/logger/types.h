#pragma once

#include <core/macros.h>
#include <core/types.h>
#include <core/export.h>

#include <fmt/format.h>

#include <functional>
#include <string>
#include <string_view>

#define NOVA_LOG_LEVEL_TRACE    0
#define NOVA_LOG_LEVEL_DEBUG    1
#define NOVA_LOG_LEVEL_INFO     2
#define NOVA_LOG_LEVEL_WARN     3
#define NOVA_LOG_LEVEL_ERROR    4
#define NOVA_LOG_LEVEL_CRITICAL 5
#define NOVA_LOG_LEVEL_OFF      6

NOVA_NAMESPACE_BEGIN
namespace log {

using ErrHandler = std::function<void(const std::string &errMsg)>;

enum Level : int {
    TRACE = NOVA_LOG_LEVEL_TRACE,
    DEBUG = NOVA_LOG_LEVEL_DEBUG,
    INFO = NOVA_LOG_LEVEL_INFO,
    WARN = NOVA_LOG_LEVEL_WARN,
    ERROR = NOVA_LOG_LEVEL_ERROR,
    CRITICAL = NOVA_LOG_LEVEL_CRITICAL,
    OFF = NOVA_LOG_LEVEL_OFF,
};

struct SourceLocation {
    NOVA_CONSTEXPR SourceLocation() = default;
    NOVA_CONSTEXPR SourceLocation(std::string_view filenameIn, int lineIn, std::string_view funcnameIn)
        : file{filenameIn}, line{lineIn}, function{funcnameIn}
    {
    }

    static NOVA_CONSTEXPR SourceLocation fromStatic(std::string_view filenameIn, int lineIn,
                                                    std::string_view funcnameIn)
    {
        return SourceLocation(filenameIn, lineIn, funcnameIn, true);
    }

    NOVA_CONSTEXPR bool empty() const NOVA_NOEXCEPT { return line <= 0; }
    NOVA_CONSTEXPR bool hasStaticStorage() const NOVA_NOEXCEPT { return hasStaticStorage_; }

    std::string_view file;
    int line = 0;
    std::string_view function;

private:
    NOVA_CONSTEXPR SourceLocation(std::string_view filenameIn, int lineIn, std::string_view funcnameIn,
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
NOVA_NAMESPACE_END
