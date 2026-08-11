#pragma once

#include <logger/async/async_log_thread_pool.h>
#include <logger/details/format_traits.h>
#include <logger/formatters/pattern_formatter.h>
#include <logger/types.h>
#include <logger/logger.h>
#include <logger/registry.h>

#include <core/namespace.h>
#include <core/macros.h>

NOVA_NAMESPACE_BEGIN
namespace log {

NOVA_INLINE void setDefaultLogger(std::shared_ptr<Logger> logger)
{
    Registry::instance().setDefaultLogger(std::move(logger));
}

NOVA_INLINE std::shared_ptr<Logger> defaultLogger()
{
    return Registry::instance().defaultLogger();
}

NOVA_INLINE void registerLogger(std::shared_ptr<Logger> newLogger)
{
    Registry::instance().registerLogger(std::move(newLogger));
}

NOVA_INLINE void registerOrReplaceLogger(std::shared_ptr<Logger> newLogger)
{
    Registry::instance().registerOrReplaceLogger(std::move(newLogger));
}

NOVA_INLINE void initLogger(std::shared_ptr<Logger> newLogger)
{
    Registry::instance().initLogger(std::move(newLogger));
}

NOVA_INLINE std::shared_ptr<Logger> getNameLogger(std::string name)
{
    return Registry::instance().getNameLogger(name);
}

NOVA_INLINE void setLevel(Level level)
{
    Registry::instance().setLevel(level);
}

NOVA_INLINE void setLevels(Registry::LogLevels levels, const Level *globalLevel = nullptr)
{
    Registry::instance().setLevels(std::move(levels), globalLevel);
}

NOVA_INLINE Level getLevel()
{
    return defaultLogger()->level();
}

NOVA_INLINE void setFlushLevel(Level level)
{
    Registry::instance().setFlushLevel(level);
}

NOVA_INLINE void setFormatter(std::unique_ptr<formatters::IFormatter> formatter)
{
    Registry::instance().setFormatter(std::move(formatter));
}

NOVA_INLINE void setPattern(std::string pattern,
                            formatters::PatternTimeType timeType = formatters::PatternTimeType::LOCAL)
{
    setFormatter(std::make_unique<formatters::PatternFormatter>(std::move(pattern), timeType));
}

NOVA_INLINE void enableBacktrace(std::size_t messageCount)
{
    Registry::instance().enableBacktrace(messageCount);
}

NOVA_INLINE void disableBacktrace()
{
    Registry::instance().disableBacktrace();
}

NOVA_INLINE void dumpBacktrace()
{
    auto logger = defaultLogger();
    if (logger) {
        logger->dumpBacktrace();
    }
}

NOVA_INLINE void setWorker(std::shared_ptr<async::AsyncLogThreadPool> worker)
{
    Registry::instance().setWorker(std::move(worker));
}

NOVA_INLINE std::shared_ptr<async::AsyncLogThreadPool> worker() noexcept
{
    return Registry::instance().worker();
}

NOVA_INLINE void setAutoRegistration(bool on)
{
    Registry::instance().setAutoRegistration(on);
}

NOVA_INLINE void setErrHandler(ErrHandler handler)
{
    Registry::instance().setErrHandler(std::move(handler));
}

NOVA_INLINE void applyAll(const std::function<void(std::shared_ptr<Logger>)> &func)
{
    Registry::instance().applyAll(func);
}

NOVA_INLINE void flushAll()
{
    Registry::instance().flushAll();
}

NOVA_INLINE void dropLogger(const std::string &name)
{
    Registry::instance().dropLogger(name);
}

NOVA_INLINE void dropAll()
{
    Registry::instance().dropAll();
}

// clean all resources and thread started by the registry
NOVA_INLINE void shutdown()
{
    Registry::instance().shutdown();
}

// log
template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVA_INLINE void log(SourceLocation source, Level level, std::string_view format, Args &&...args)
{
    defaultLogger()->log(source, level, format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVA_INLINE void log(SourceLocation source, Level level, FmtRuntimeString<char> format, Args &&...args)
{
    defaultLogger()->log(source, level, format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVA_INLINE void log(Level level, std::string_view format, Args &&...args)
{
    defaultLogger()->log(level, format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVA_INLINE void log(Level level, FmtRuntimeString<char> format, Args &&...args)
{
    defaultLogger()->log(level, format, std::forward<Args>(args)...);
}

template <typename T>
NOVA_INLINE void log(Level level, const T &msg)
{
    defaultLogger()->log(level, msg);
}

// T cannot be statically converted to format string (including string_view/wstring_view)
template <class T,
          typename std::enable_if<!details::IsConvertibleToAnyFormatString<const T &>::value, int>::type = 0>
NOVA_INLINE void log(SourceLocation source, Level level, const T &msg)
{
    defaultLogger()->log(source, level, msg);
}

NOVA_INLINE void log(SystemTimePoint logTime, SourceLocation source, Level level, std::string_view msg)
{
    defaultLogger()->log(logTime, source, level, msg);
}

NOVA_INLINE void log(SourceLocation source, Level level, std::string_view msg)
{
    defaultLogger()->log(source, level, msg);
}

NOVA_INLINE void log(Level level, std::string_view msg)
{
    defaultLogger()->log(level, msg);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVA_INLINE void trace(std::string_view format, Args &&...args)
{
    defaultLogger()->trace(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVA_INLINE void trace(FmtRuntimeString<char> format, Args &&...args)
{
    defaultLogger()->trace(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVA_INLINE void debug(std::string_view format, Args &&...args)
{
    defaultLogger()->debug(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVA_INLINE void debug(FmtRuntimeString<char> format, Args &&...args)
{
    defaultLogger()->debug(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVA_INLINE void info(std::string_view format, Args &&...args)
{
    defaultLogger()->info(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVA_INLINE void info(FmtRuntimeString<char> format, Args &&...args)
{
    defaultLogger()->info(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVA_INLINE void warn(std::string_view format, Args &&...args)
{
    defaultLogger()->warn(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVA_INLINE void warn(FmtRuntimeString<char> format, Args &&...args)
{
    defaultLogger()->warn(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVA_INLINE void error(std::string_view format, Args &&...args)
{
    defaultLogger()->error(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVA_INLINE void error(FmtRuntimeString<char> format, Args &&...args)
{
    defaultLogger()->error(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVA_INLINE void critical(std::string_view format, Args &&...args)
{
    defaultLogger()->critical(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVA_INLINE void critical(FmtRuntimeString<char> format, Args &&...args)
{
    defaultLogger()->critical(format, std::forward<Args>(args)...);
}

NOVA_INLINE void trace(std::string_view message)
{
    defaultLogger()->trace(message);
}
NOVA_INLINE void debug(std::string_view message)
{
    defaultLogger()->debug(message);
}
NOVA_INLINE void info(std::string_view message)
{
    defaultLogger()->info(message);
}
NOVA_INLINE void warn(std::string_view message)
{
    defaultLogger()->warn(message);
}
NOVA_INLINE void error(std::string_view message)
{
    defaultLogger()->error(message);
}
NOVA_INLINE void critical(std::string_view message)
{
    defaultLogger()->critical(message);
}

}  // namespace log
NOVA_NAMESPACE_END
