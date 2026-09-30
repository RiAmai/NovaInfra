#pragma once

#include <logger/formatters/pattern_formatter.h>
#include <logger/details/mutex.h>
#include <logger/sinks/sink.h>
#include <mutex>
#include <stdexcept>

NOVAINFRA_NAMESPACE_BEGIN

namespace log::sinks {
// base sink
template <typename Mutex>
class BaseSink : public ISink
{
public:
    BaseSink() : formatter_(std::make_unique<formatters::PatternFormatter>()) {}
    explicit BaseSink(std::unique_ptr<formatters::IFormatter> formatter) : formatter_(std::move(formatter))
    {
        if (!formatter_) {
            NOVAINFRA_THROW(std::invalid_argument("Sink formatter must not be null"));
        }
    }
    virtual ~BaseSink() = default;
    NOVAINFRA_DISABLE_COPY_MOVE(BaseSink)

    void log(const details::LogMsg &msg) NOVAINFRA_FINAL NOVAINFRA_OVERRIDE
    {
        std::lock_guard<Mutex> lock(mutex_);
        _log(msg);
    }

    void setPattern(const std::string &pattern) NOVAINFRA_FINAL NOVAINFRA_OVERRIDE
    {
        std::lock_guard<Mutex> lock(mutex_);
        _setPattern(pattern);
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
        return !std::is_same<Mutex, details::NullMutex>::value;
    }

protected:
    virtual void _log(const details::LogMsg &msg) = 0;
    virtual void _flush() = 0;

    virtual void _setPattern(const std::string &pattern)
    {
        _setFormatter(std::make_unique<formatters::PatternFormatter>(pattern));
    }

    virtual void _setFormatter(std::unique_ptr<formatters::IFormatter> formatter)
    {
        formatter_ = std::move(formatter);
    }

    const std::string &formatMessage(const details::LogMsg &msg)
    {
        formattedBuffer_.clear();
        formatter_->format(msg, formattedBuffer_);
        return formattedBuffer_;
    }

    mutable Mutex mutex_;
    std::unique_ptr<formatters::IFormatter> formatter_;
    std::string formattedBuffer_;
};

}  // namespace log::sinks
NOVAINFRA_NAMESPACE_END
