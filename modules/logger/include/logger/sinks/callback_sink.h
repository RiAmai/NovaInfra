#pragma once

#include <logger/details/mutex.h>
#include <logger/sinks/base_sink.h>

#include <functional>
#include <mutex>
#include <stdexcept>
#include <utility>

NOVAINFRA_NAMESPACE_BEGIN
namespace log::sinks {

template <typename Mutex>
class CallbackSink NOVAINFRA_FINAL : public BaseSink<Mutex>
{
public:
    using Callback = std::function<void(const details::LogMsg &)>;

    explicit CallbackSink(Callback callback) : callback_(std::move(callback))
    {
        if (!callback_) {
            NOVAINFRA_THROW(std::invalid_argument("Callback sink callback must not be empty"));
        }
    }

private:
    void _log(const details::LogMsg &msg) NOVAINFRA_OVERRIDE { callback_(msg); }
    void _flush() NOVAINFRA_OVERRIDE {}

    Callback callback_;
};

using CallbackSinkMt = CallbackSink<std::mutex>;
using CallbackSinkSt = CallbackSink<details::NullMutex>;

}  // namespace log::sinks
NOVAINFRA_NAMESPACE_END
