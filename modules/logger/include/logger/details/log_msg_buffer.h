#pragma once

#include <logger/types.h>
#include <logger/details/log_msg.h>
#include <core/macros.h>

#include <core/namespace.h>


NOVAINFRA_NAMESPACE_BEGIN
namespace log::details {

struct NOVAINFRA_LIB_PORT LogMsgBuffer : LogMsg {
    LogMsgBuffer() = default;
    explicit LogMsgBuffer(const LogMsg &logMsg) : LogMsg(logMsg)
    {
        buffer.append(loggerName);
        buffer.append(message);
        appendSource();
        updateStringView();
    }
    LogMsgBuffer(const LogMsgBuffer &other) : LogMsg(other)
    {
        buffer.append(loggerName);
        buffer.append(message);
        appendSource();
        updateStringView();
    }
    LogMsgBuffer(LogMsgBuffer &&other) NOVAINFRA_NOEXCEPT : LogMsg(other), buffer(std::move(other.buffer))
    {
        updateStringView();
    }
    LogMsgBuffer &operator=(const LogMsgBuffer &other)
    {
        LogMsg::operator=(other);
        buffer.clear();
        buffer.append(other.buffer);
        updateStringView();
        return *this;
    }
    LogMsgBuffer &operator=(LogMsgBuffer &&other) NOVAINFRA_NOEXCEPT
    {
        LogMsg::operator=(other);
        buffer = std::move(other.buffer);
        updateStringView();
        return *this;
    }

    void appendSource()
    {
        if (!source.file.empty()) {
            buffer.append(source.file);
        }
        if (!source.function.empty()) {
            buffer.append(source.function);
        }
    }
    void updateStringView()
    {
        loggerName = std::string_view(buffer.data(), loggerName.size());
        message = std::string_view(buffer.data() + loggerName.size(), message.size());

        std::size_t offset = loggerName.size() + message.size();
        if (!source.file.empty()) {
            source.file = std::string_view(buffer.data() + offset, source.file.size());
            offset += source.file.size();
        }
        if (!source.function.empty()) {
            source.function = std::string_view(buffer.data() + offset, source.function.size());
        }
    }

    std::string buffer;
};

}
NOVAINFRA_NAMESPACE_END
