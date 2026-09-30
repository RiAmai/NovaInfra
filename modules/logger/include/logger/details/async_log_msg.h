#pragma once

#include <logger/details/log_buffer.h>
#include <logger/details/log_msg.h>
#include <logger/types.h>

#include <core/namespace.h>

#include <memory>
#include <string>
#include <utility>

NOVAINFRA_NAMESPACE_BEGIN
namespace log::async {
class AsyncLogger;
}

namespace log::details {

enum class AsyncMsgType { LOG, FLUSH, TERMINATE };

struct AsyncLogMsg : LogMsg {
    AsyncLogMsg() = default;
    ~AsyncLogMsg() = default;

    AsyncLogMsg(const AsyncLogMsg &) = delete;
    AsyncLogMsg &operator=(const AsyncLogMsg &) = delete;

    AsyncLogMsg(std::shared_ptr<async::AsyncLogger> &&logger, AsyncMsgType type, const LogMsg &msg)
        : LogMsg{msg}, msgType{type}, loggerPtr{std::move(logger)}
    {
        ownMessage();
        ownDynamicSource();
    }

    AsyncLogMsg(std::shared_ptr<async::AsyncLogger> &&logger, AsyncMsgType type)
        : LogMsg{}, msgType{type}, loggerPtr{std::move(logger)}
    {
    }

    explicit AsyncLogMsg(AsyncMsgType type) : AsyncLogMsg{nullptr, type} {}

    AsyncLogMsg(AsyncLogMsg &&other) NOVAINFRA_NOEXCEPT { *this = std::move(other); }

    AsyncLogMsg &operator=(AsyncLogMsg &&other) NOVAINFRA_NOEXCEPT
    {
        if (this == &other) {
            return *this;
        }

        LogMsg::operator=(std::move(other));
        msgType = other.msgType;
        loggerPtr = std::move(other.loggerPtr);
        messageBuffer_ = std::move(other.messageBuffer_);
        dynamicSource_ = std::move(other.dynamicSource_);
        rebindMessage();
        rebindDynamicSource();
        other.message = {};
        other.source = SourceLocation{};
        return *this;
    }

    AsyncMsgType msgType{AsyncMsgType::LOG};
    std::shared_ptr<async::AsyncLogger> loggerPtr;

private:
    void ownMessage()
    {
        if (!message.empty()) {
            messageBuffer_.append(message.data(), message.data() + message.size());
        }
        rebindMessage();
    }

    void ownDynamicSource()
    {
        if (source.hasStaticStorage()) {
            return;
        }
        if (source.file.empty() && source.function.empty()) {
            source.file = {};
            source.function = {};
            return;
        }

        dynamicSource_ = std::make_unique<OwnedSource>();
        if (!source.file.empty()) {
            dynamicSource_->file.assign(source.file.data(), source.file.size());
        }
        if (!source.function.empty()) {
            dynamicSource_->function.assign(source.function.data(), source.function.size());
        }
        rebindDynamicSource();
    }

    void rebindMessage() { message = std::string_view(messageBuffer_.data(), messageBuffer_.size()); }

    void rebindDynamicSource()
    {
        if (!source.hasStaticStorage() && dynamicSource_) {
            source.file = dynamicSource_->file;
            source.function = dynamicSource_->function;
        }
    }

    struct OwnedSource {
        std::string file;
        std::string function;
    };

    LogMemoryBuffer messageBuffer_;
    std::unique_ptr<OwnedSource> dynamicSource_;
};

}  // namespace log::details
NOVAINFRA_NAMESPACE_END
