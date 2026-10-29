#pragma once

#include <logger/detail/level_storage.h>
#include <logger/formatters/formatter.h>
#include <logger/detail/log_msg.h>
#include <logger/types.h>

#include <core/export.h>
#include <core/namespace.h>

#include <initializer_list>
#include <memory>

NOVAINFRA_NAMESPACE_BEGIN
namespace log::sinks {

class NOVAINFRA_LIB_PORT ISink
{
public:
    virtual ~ISink() = default;

    // LogMsg string views are valid only for the duration of this call.
    virtual void log(const detail::LogMsg &msg) = 0;
    virtual void setPattern(const std::string &pattern) = 0;
    virtual void setFormatter(std::unique_ptr<formatters::IFormatter> formatter) = 0;
    virtual void flush() = 0;

    // Return true only when log(), flush(), and concurrent configuration access
    // are safe. Async pools with multiple workers require this capability;
    // custom sinks must opt in explicitly.
    virtual bool isThreadSafe() const NOVAINFRA_NOEXCEPT { return false; }

    void setLevel(Level level) { level_.store(level); }
    Level level() const { return level_.load(std::memory_order_relaxed); }
    bool shouldLog(Level level) const { return level >= level_.load(std::memory_order_relaxed); }

protected:
    detail::LevelType level_{Level::TRACE};
};

using SinkPtr = std::shared_ptr<ISink>;
using SinkInitList = std::initializer_list<SinkPtr>;

}  // namespace log::sinks
NOVAINFRA_NAMESPACE_END
