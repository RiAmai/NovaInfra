#pragma once

#include <logger/async/async_log_thread_pool.h>
#include <logger/details/format_traits.h>
#include <logger/formatters/pattern_formatter.h>
#include <logger/types.h>
#include <logger/logger.h>
#include <logger/registry.h>

#include <core/namespace.h>
#include <core/macros.h>

NOVAINFRA_NAMESPACE_BEGIN
namespace log {

NOVAINFRA_INLINE void setDefaultLogger(std::shared_ptr<Logger> logger)
{
    Registry::instance().setDefaultLogger(std::move(logger));
}

NOVAINFRA_INLINE std::shared_ptr<Logger> defaultLogger()
{
    return Registry::instance().defaultLogger();
}

NOVAINFRA_INLINE void registerLogger(std::shared_ptr<Logger> newLogger)
{
    Registry::instance().registerLogger(std::move(newLogger));
}

NOVAINFRA_INLINE void registerOrReplaceLogger(std::shared_ptr<Logger> newLogger)
{
    Registry::instance().registerOrReplaceLogger(std::move(newLogger));
}

NOVAINFRA_INLINE void initLogger(std::shared_ptr<Logger> newLogger)
{
    Registry::instance().initLogger(std::move(newLogger));
}

NOVAINFRA_INLINE std::shared_ptr<Logger> getNameLogger(std::string name)
{
    return Registry::instance().getNameLogger(name);
}

NOVAINFRA_INLINE void setLevel(Level level)
{
    Registry::instance().setLevel(level);
}

NOVAINFRA_INLINE void setLevels(Registry::LogLevels levels, const Level *globalLevel = nullptr)
{
    Registry::instance().setLevels(std::move(levels), globalLevel);
}

NOVAINFRA_INLINE Level getLevel()
{
    return defaultLogger()->level();
}

NOVAINFRA_INLINE void setFlushLevel(Level level)
{
    Registry::instance().setFlushLevel(level);
}

NOVAINFRA_INLINE void setFormatter(std::unique_ptr<formatters::IFormatter> formatter)
{
    Registry::instance().setFormatter(std::move(formatter));
}

NOVAINFRA_INLINE void setPattern(std::string pattern,
                                 formatters::PatternTimeType timeType = formatters::PatternTimeType::LOCAL)
{
    setFormatter(std::make_unique<formatters::PatternFormatter>(std::move(pattern), timeType));
}

NOVAINFRA_INLINE void enableBacktrace(std::size_t messageCount)
{
    Registry::instance().enableBacktrace(messageCount);
}

NOVAINFRA_INLINE void disableBacktrace()
{
    Registry::instance().disableBacktrace();
}

NOVAINFRA_INLINE void dumpBacktrace()
{
    auto logger = defaultLogger();
    if (logger) {
        logger->dumpBacktrace();
    }
}

NOVAINFRA_INLINE void setWorker(std::shared_ptr<async::AsyncLogThreadPool> worker)
{
    Registry::instance().setWorker(std::move(worker));
}

NOVAINFRA_INLINE std::shared_ptr<async::AsyncLogThreadPool> worker() NOVAINFRA_NOEXCEPT
{
    return Registry::instance().worker();
}

NOVAINFRA_INLINE void setAutoRegistration(bool on)
{
    Registry::instance().setAutoRegistration(on);
}

NOVAINFRA_INLINE void setErrHandler(ErrHandler handler)
{
    Registry::instance().setErrHandler(std::move(handler));
}

NOVAINFRA_INLINE void applyAll(const std::function<void(std::shared_ptr<Logger>)> &func)
{
    Registry::instance().applyAll(func);
}

NOVAINFRA_INLINE void flushAll()
{
    Registry::instance().flushAll();
}

NOVAINFRA_INLINE void dropLogger(const std::string &name)
{
    Registry::instance().dropLogger(name);
}

NOVAINFRA_INLINE void dropAll()
{
    Registry::instance().dropAll();
}

// clean all resources and thread started by the registry
NOVAINFRA_INLINE void shutdown()
{
    Registry::instance().shutdown();
}

// log
template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVAINFRA_INLINE void log(SourceLocation source, Level level, std::string_view format, Args &&...args)
{
    defaultLogger()->log(source, level, format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVAINFRA_INLINE void log(SourceLocation source, Level level, FmtRuntimeString<char> format, Args &&...args)
{
    defaultLogger()->log(source, level, format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVAINFRA_INLINE void log(Level level, std::string_view format, Args &&...args)
{
    defaultLogger()->log(level, format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVAINFRA_INLINE void log(Level level, FmtRuntimeString<char> format, Args &&...args)
{
    defaultLogger()->log(level, format, std::forward<Args>(args)...);
}

template <typename T>
NOVAINFRA_INLINE void log(Level level, const T &msg)
{
    defaultLogger()->log(level, msg);
}

// T cannot be statically converted to format string (including string_view/wstring_view)
template <class T,
          typename std::enable_if<!details::IsConvertibleToAnyFormatString<const T &>::value, int>::type = 0>
NOVAINFRA_INLINE void log(SourceLocation source, Level level, const T &msg)
{
    defaultLogger()->log(source, level, msg);
}

NOVAINFRA_INLINE void log(SystemTimePoint logTime, SourceLocation source, Level level, std::string_view msg)
{
    defaultLogger()->log(logTime, source, level, msg);
}

NOVAINFRA_INLINE void log(SourceLocation source, Level level, std::string_view msg)
{
    defaultLogger()->log(source, level, msg);
}

NOVAINFRA_INLINE void log(Level level, std::string_view msg)
{
    defaultLogger()->log(level, msg);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVAINFRA_INLINE void trace(std::string_view format, Args &&...args)
{
    defaultLogger()->trace(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVAINFRA_INLINE void trace(FmtRuntimeString<char> format, Args &&...args)
{
    defaultLogger()->trace(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVAINFRA_INLINE void debug(std::string_view format, Args &&...args)
{
    defaultLogger()->debug(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVAINFRA_INLINE void debug(FmtRuntimeString<char> format, Args &&...args)
{
    defaultLogger()->debug(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVAINFRA_INLINE void info(std::string_view format, Args &&...args)
{
    defaultLogger()->info(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVAINFRA_INLINE void info(FmtRuntimeString<char> format, Args &&...args)
{
    defaultLogger()->info(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVAINFRA_INLINE void warn(std::string_view format, Args &&...args)
{
    defaultLogger()->warn(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVAINFRA_INLINE void warn(FmtRuntimeString<char> format, Args &&...args)
{
    defaultLogger()->warn(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVAINFRA_INLINE void error(std::string_view format, Args &&...args)
{
    defaultLogger()->error(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVAINFRA_INLINE void error(FmtRuntimeString<char> format, Args &&...args)
{
    defaultLogger()->error(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVAINFRA_INLINE void critical(std::string_view format, Args &&...args)
{
    defaultLogger()->critical(format, std::forward<Args>(args)...);
}

template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
NOVAINFRA_INLINE void critical(FmtRuntimeString<char> format, Args &&...args)
{
    defaultLogger()->critical(format, std::forward<Args>(args)...);
}

NOVAINFRA_INLINE void trace(std::string_view message)
{
    defaultLogger()->trace(message);
}
NOVAINFRA_INLINE void debug(std::string_view message)
{
    defaultLogger()->debug(message);
}
NOVAINFRA_INLINE void info(std::string_view message)
{
    defaultLogger()->info(message);
}
NOVAINFRA_INLINE void warn(std::string_view message)
{
    defaultLogger()->warn(message);
}
NOVAINFRA_INLINE void error(std::string_view message)
{
    defaultLogger()->error(message);
}
NOVAINFRA_INLINE void critical(std::string_view message)
{
    defaultLogger()->critical(message);
}

}  // namespace log
NOVAINFRA_NAMESPACE_END
