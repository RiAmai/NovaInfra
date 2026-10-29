#pragma once

#include <logger/detail/mutex.h>
#include <logger/formatters/pattern_formatter.h>
#include <logger/sinks/sink.h>

#include <core/os_utils.h>
#include <stdexcept>


NOVAINFRA_NAMESPACE_BEGIN
namespace log::sinks {

// console out sink, supports automatic switching between stdout and stderr.
template <typename MutexOrPolicy>
class ConsoleOutSink : public ISink
{
public:
    using MutexTraits = detail::ConsoleMutexTraits<MutexOrPolicy>;
    using Mutex = typename MutexTraits::Mutex;
    explicit ConsoleOutSink()
        : mutex_(MutexTraits::mutex()), formatter_(std::make_unique<formatters::PatternFormatter>())
    {
    }
    ~ConsoleOutSink() NOVAINFRA_OVERRIDE = default;
    NOVAINFRA_DISABLE_COPY_MOVE(ConsoleOutSink<MutexOrPolicy>)

    void log(const detail::LogMsg &msg) NOVAINFRA_FINAL NOVAINFRA_OVERRIDE
    {
        std::lock_guard<Mutex> lock(mutex_);
        _log(msg);
    }

    void setPattern(const std::string &pattern) NOVAINFRA_FINAL NOVAINFRA_OVERRIDE
    {
        std::lock_guard<Mutex> lock(mutex_);
        _setFormatter(std::make_unique<formatters::PatternFormatter>(pattern));
    }

    void setFormatter(std::unique_ptr<formatters::IFormatter> formatter) NOVAINFRA_FINAL NOVAINFRA_OVERRIDE
    {
        if (!formatter) {
            NOVAINFRA_THROW(std::invalid_argument("Sink formatter must not be null"));
        }
        std::lock_guard<Mutex> lock(mutex_);
        _setFormatter(std::move(formatter));
    }

    void flush() NOVAINFRA_FINAL NOVAINFRA_OVERRIDE
    {
        std::lock_guard<Mutex> lock(mutex_);
        _flush();
    }

    bool isThreadSafe() const NOVAINFRA_NOEXCEPT NOVAINFRA_FINAL NOVAINFRA_OVERRIDE
    {
        return !std::is_same<Mutex, detail::NullMutex>::value;
    }

protected:
    void _log(const detail::LogMsg &msg)
    {
        const auto &dstStr = this->formatMessage(msg);
        FILE *file;
        if (msg.level < Level::WARN) {
            file = stdout;
        } else {
            file = stderr;
        }
        if (!core::writeBytes(dstStr.data(), dstStr.size(), file)) {
            NOVAINFRA_THROW(std::runtime_error("Failed writing log message to console"));
        }
    }

    void _flush()
    {
        const auto stdoutResult = ::fflush(stdout);
        const auto stderrResult = ::fflush(stderr);
        if (stdoutResult != 0) {
            NOVAINFRA_THROW(std::runtime_error("Failed flushing stdout"));
        }
        if (stderrResult != 0) {
            NOVAINFRA_THROW(std::runtime_error("Failed flushing stderr"));
        }
    }

    const std::string &formatMessage(const detail::LogMsg &msg)
    {
        formattedBuffer_.clear();
        formatter_->format(msg, formattedBuffer_);
        return formattedBuffer_;
    }

    void _setFormatter(std::unique_ptr<formatters::IFormatter> formatter)
    {
        formatter_ = std::move(formatter);
    }

    Mutex &mutex_;
    std::unique_ptr<formatters::IFormatter> formatter_;
    std::string formattedBuffer_;
};

using ConsoleOutSinkMt = ConsoleOutSink<detail::ConsoleMutex>;
using ConsoleOutSinkSt = ConsoleOutSink<detail::ConsoleNullMutex>;

}  // namespace log::sinks
NOVAINFRA_NAMESPACE_END
