#pragma once

#include <logger/detail/error_handling.h>
#include <logger/detail/format_traits.h>
#include <logger/detail/level_storage.h>
#include <logger/detail/log_buffer.h>
#include <logger/detail/backtracer.h>
#include <logger/sinks/sink.h>
#include <logger/detail/log_msg.h>
#include <core/os_utils.h>

#include <fmt/format.h>
#include <algorithm>
#include <iterator>
#include <memory>

NOVAINFRA_NAMESPACE_BEGIN
namespace log {
class NOVAINFRA_LIB_PORT Logger
{
public:
    explicit Logger(std::string name) : name_(std::move(name)), sinks_() {}
    template <typename It>
    Logger(std::string name, It begin, It end) : name_(std::move(name)), sinks_(begin, end)
    {
        validateSinks();
    }
    Logger(std::string name, sinks::SinkPtr singleSink) : Logger(std::move(name), {std::move(singleSink)}) {}
    Logger(std::string name, sinks::SinkInitList sinks) : Logger(std::move(name), sinks.begin(), sinks.end())
    {
    }
    virtual ~Logger() = default;

    Logger(const Logger &other);
    Logger(Logger &&other) NOVAINFRA_NOEXCEPT;
    Logger &operator=(const Logger &) = delete;
    Logger &operator=(Logger &&) = delete;

    const std::string &name() const { return name_; }

    void setLevel(Level level) { level_.store(level); }
    Level level() const { return level_.load(std::memory_order_relaxed); }
    bool shouldLog(Level level) const
    {
        return (level >= level_.load(std::memory_order_relaxed)) && (level != Level::OFF);
    }

    void setFlushLevel(Level level) { flushLevel_.store(level); }
    Level flushLevel() const { return flushLevel_.load(std::memory_order_relaxed); }
    bool shouldFlush(Level level) const { return (level >= flushLevel()) && (level != Level::OFF); }

    const std::vector<sinks::SinkPtr> &sinks() const { return sinks_; }

    bool shouldBacktrace() const { return tracer_.enabled(); }
    void enableBacktrace(std::size_t size) { tracer_.enable(size); }
    void disableBacktrace() { tracer_.disable(); }
    void dumpBacktrace()
    {
        using detail::LogMsg;
        if (!tracer_.enabled()) {
            return;
        }
        auto messages = tracer_.drain();
        if (!messages.empty()) {
            sinkIt(LogMsg{name(), Level::INFO, "****************** Backtrace Start ******************"});
            for (const auto &message : messages) {
                sinkIt(message);
            }
            sinkIt(LogMsg{name(), Level::INFO, "****************** Backtrace End ********************"});
            // Backtrace replay must be visible to synchronous sinks before this call returns.
            // AsyncLogger overrides _flush() with an enqueue-only operation; its completion
            // semantics are documented separately.
            flush();
        }
    }

    void setFormatter(std::unique_ptr<formatters::IFormatter> formatter)
    {
        if (!formatter) {
            NOVAINFRA_THROW(std::invalid_argument("Logger formatter must not be null"));
        }
        const auto lastSink = std::find_if(sinks_.rbegin(), sinks_.rend(),
                                           [](const auto &sink) { return static_cast<bool>(sink); });
        for (auto it = sinks_.begin(); it != sinks_.end(); ++it) {
            if (!*it) {
                errHandler("Logger sink is null");
                continue;
            }
            if (lastSink != sinks_.rend() && it == std::prev(lastSink.base())) {
                (*it)->setFormatter(std::move(formatter));
            } else {
                (*it)->setFormatter(formatter->clone());
            }
        }
    }

    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    void log(SourceLocation source, Level level, std::string_view format, Args &&...args)
    {
        _log(source, level, format, std::forward<Args>(args)...);
    }

    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    void log(SourceLocation source, Level level, FmtRuntimeString<char> format, Args &&...args)
    {
        _log(source, level, std::string_view(format.str.data(), format.str.size()),
             std::forward<Args>(args)...);
    }

    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    void log(Level level, std::string_view format, Args &&...args)
    {
        log(SourceLocation{}, level, format, std::forward<Args>(args)...);
    }

    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    void log(Level level, FmtRuntimeString<char> format, Args &&...args)
    {
        log(SourceLocation{}, level, format, std::forward<Args>(args)...);
    }

    template <typename T>
    void log(Level level, const T &msg)
    {
        log(SourceLocation{}, level, msg);
    }

    // T cannot be statically converted to format string (including string_view/wstring_view)
    template <class T, typename std::enable_if<!detail::IsConvertibleToAnyFormatString<const T &>::value,
                                               int>::type = 0>
    void log(SourceLocation source, Level level, const T &msg)
    {
        log(source, level, "{}", msg);
    }

    void log(SystemTimePoint logTime, SourceLocation source, Level level, std::string_view msg)
    {
        if (!shouldLogOrBacktrace(level)) {
            return;
        }

        NOVAINFRA_TRY
        {
            detail::LogMsg logMsg{logTime, source, name_, level, msg};
            logIt(logMsg);
        }
        NOVAINFRA_LOG_DETAIL_CATCH(source)
    }

    void log(SourceLocation source, Level level, std::string_view msg)
    {
        if (!shouldLogOrBacktrace(level)) {
            return;
        }

        NOVAINFRA_TRY
        {
            detail::LogMsg logMsg{source, name_, level, msg};
            logIt(logMsg);
        }
        NOVAINFRA_LOG_DETAIL_CATCH(source)
    }

    void log(Level level, std::string_view msg) { log(SourceLocation{}, level, msg); }

    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    void trace(std::string_view format, Args &&...args)
    {
        log(Level::TRACE, format, std::forward<Args>(args)...);
    }

    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    void trace(FmtRuntimeString<char> format, Args &&...args)
    {
        log(Level::TRACE, format, std::forward<Args>(args)...);
    }

    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    void debug(std::string_view format, Args &&...args)
    {
        log(Level::DEBUG, format, std::forward<Args>(args)...);
    }

    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    void debug(FmtRuntimeString<char> format, Args &&...args)
    {
        log(Level::DEBUG, format, std::forward<Args>(args)...);
    }

    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    void info(std::string_view format, Args &&...args)
    {
        log(Level::INFO, format, std::forward<Args>(args)...);
    }

    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    void info(FmtRuntimeString<char> format, Args &&...args)
    {
        log(Level::INFO, format, std::forward<Args>(args)...);
    }

    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    void warn(std::string_view format, Args &&...args)
    {
        log(Level::WARN, format, std::forward<Args>(args)...);
    }

    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    void warn(FmtRuntimeString<char> format, Args &&...args)
    {
        log(Level::WARN, format, std::forward<Args>(args)...);
    }

    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    void error(std::string_view format, Args &&...args)
    {
        log(Level::ERROR, format, std::forward<Args>(args)...);
    }

    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    void error(FmtRuntimeString<char> format, Args &&...args)
    {
        log(Level::ERROR, format, std::forward<Args>(args)...);
    }

    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    void critical(std::string_view format, Args &&...args)
    {
        log(Level::CRITICAL, format, std::forward<Args>(args)...);
    }

    template <typename... Args, std::enable_if_t<(sizeof...(Args) > 0), int> = 0>
    void critical(FmtRuntimeString<char> format, Args &&...args)
    {
        log(Level::CRITICAL, format, std::forward<Args>(args)...);
    }

    void trace(std::string_view message) { this->log(Level::TRACE, message); }
    void debug(std::string_view message) { this->log(Level::DEBUG, message); }
    void info(std::string_view message) { this->log(Level::INFO, message); }
    void warn(std::string_view message) { this->log(Level::WARN, message); }
    void error(std::string_view message) { this->log(Level::ERROR, message); }
    void critical(std::string_view message) { this->log(Level::CRITICAL, message); }

    void flush() { _flush(); }

    void setErrHandler(ErrHandler handler);
    virtual std::shared_ptr<Logger> clone(std::string name);

protected:
    template <typename... Args>
    void _log(SourceLocation source, Level level, std::string_view format, Args &&...args)
    {
        if (!shouldLogOrBacktrace(level)) {
            return;
        }

        NOVAINFRA_TRY
        {
            detail::LogMemoryBuffer message;
            fmt::vformat_to(fmt::appender(message), fmt::string_view(format.data(), format.size()),
                            fmt::make_format_args(args...));
            detail::LogMsg msg{source, name_, level, std::string_view(message.data(), message.size())};
            logIt(msg);
        }
        NOVAINFRA_LOG_DETAIL_CATCH(source)
    }
    void errHandler(const std::string &msg) NOVAINFRA_NOEXCEPT;

    bool shouldLogOrBacktrace(Level level) const
    {
        return level != Level::OFF && (shouldLog(level) || tracer_.enabled());
    }
    void logIt(const detail::LogMsg &msg)
    {
        if (tracer_.enabled()) {
            tracer_.pushBack(msg);
        }
        if (shouldLog(msg.level)) {
            sinkIt(msg);
        }
    }

    virtual void sinkIt(const detail::LogMsg &msg);
    virtual void _flush();

    void validateSinks() const;

    static std::size_t currentThreadId() NOVAINFRA_NOEXCEPT
    {
#ifdef NOVAINFRA_NO_THREAD_ID
        return 0;
#else
        return core::threadId();
#endif
    }

    std::string name_;
    detail::LevelType level_{Level::TRACE};
    detail::LevelType flushLevel_{Level::OFF};
    std::vector<sinks::SinkPtr> sinks_;
    // Read and replace through the atomic shared_ptr free functions in logger.cpp.
    std::shared_ptr<const ErrHandler> cstErrHandler_;
    detail::BackTracer tracer_;
};
}  // namespace log
NOVAINFRA_NAMESPACE_END
