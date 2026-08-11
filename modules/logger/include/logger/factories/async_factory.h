#pragma once

#include <logger/async/async_logger.h>
#include <logger/async/types.h>
#include <logger/registry.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

NOVA_NAMESPACE_BEGIN
namespace log::factories {

template <async::AsyncOverflowPolicy OverflowPolicy = async::AsyncOverflowPolicy::BLOCK>
struct AsynchronousFactory {
    static_assert(OverflowPolicy == async::AsyncOverflowPolicy::BLOCK ||
                      OverflowPolicy == async::AsyncOverflowPolicy::OVER_RUN_OLDEST ||
                      OverflowPolicy == async::AsyncOverflowPolicy::DISCARD_NEW,
                  "Invalid async overflow policy");

    template <typename Sink, typename... SinkArgs>
    static std::shared_ptr<Logger> create(std::string loggerName, SinkArgs &&...args)
    {
        auto worker = Registry::instance().worker();
        if (!worker) {
            NOVA_THROW(std::runtime_error("Async logger worker is not configured"));
        }

        auto sink = std::make_shared<Sink>(std::forward<SinkArgs>(args)...);
        auto newLogger = std::make_shared<async::AsyncLogger>(std::move(loggerName), std::move(sink), worker,
                                                              OverflowPolicy);
        Registry::instance().initLogger(newLogger);
        return newLogger;
    }
};

using AsyncFactory = AsynchronousFactory<async::AsyncOverflowPolicy::BLOCK>;
using AsyncOverrunOldestFactory = AsynchronousFactory<async::AsyncOverflowPolicy::OVER_RUN_OLDEST>;
using AsyncDiscardNewFactory = AsynchronousFactory<async::AsyncOverflowPolicy::DISCARD_NEW>;

template <typename Sink, typename... SinkArgs>
inline std::shared_ptr<Logger> createAsync(std::string loggerName, SinkArgs &&...args)
{
    return AsyncFactory::create<Sink>(loggerName, std::forward<SinkArgs>(args)...);
}

template <typename Sink, typename... SinkArgs>
inline std::shared_ptr<Logger> createAsyncORO(std::string loggerName, SinkArgs &&...args)
{
    return AsyncOverrunOldestFactory::create<Sink>(loggerName, std::forward<SinkArgs>(args)...);
}

template <typename Sink, typename... SinkArgs>
inline std::shared_ptr<Logger> createAsyncDN(std::string loggerName, SinkArgs &&...args)
{
    return AsyncDiscardNewFactory::create<Sink>(loggerName, std::forward<SinkArgs>(args)...);
}

}  // namespace log::factories
NOVA_NAMESPACE_END
