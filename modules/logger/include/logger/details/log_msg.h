#pragma once

#include <logger/types.h>

#include <core/os_utils.h>
#include <core/namespace.h>


NOVA_NAMESPACE_BEGIN
namespace log::details {

struct NOVA_LIB_PORT LogMsg {
    LogMsg() = default;
    LogMsg(SystemTimePoint logTime, SourceLocation loc, std::string_view loggerNameIn, Level levelIn,
           std::string_view msg)
        : timestamp(logTime), level(levelIn)
#ifndef NOVA_NO_THREAD_ID
          ,
          threadId(core::ThreadId())
#endif
          ,
          source(loc), loggerName(loggerNameIn), message(msg)
    {
    }
    LogMsg(SystemTimePoint logTime, Level levelIn, std::size_t threadIdIn, SourceLocation loc,
           std::string_view loggerNameIn, std::string_view msg)
        : timestamp(logTime), level(levelIn), threadId(threadIdIn), source(loc), loggerName(loggerNameIn),
          message(msg)
    {
    }
    LogMsg(SourceLocation loc, std::string_view loggerNameIn, Level levelIn, std::string_view msg)
        : LogMsg(core::Now(), loc, loggerNameIn, levelIn, msg)
    {
    }
    LogMsg(std::string_view loggerNameIn, Level levelIn, std::string_view msg)
        : LogMsg(core::Now(), SourceLocation{}, loggerNameIn, levelIn, msg)
    {
    }
    LogMsg(const LogMsg &other) = default;
    LogMsg &operator=(const LogMsg &other) = default;

    SystemTimePoint timestamp;
    Level level{Level::OFF};
    std::size_t threadId{0};
    SourceLocation source;
    std::string_view loggerName;
    std::string_view message;
};

}  // namespace log::details

namespace log {
using LogMsg = details::LogMsg;
}
NOVA_NAMESPACE_END
