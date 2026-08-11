#pragma once

#include <logger/logger.h>
#include <logger/registry.h>

#include <memory>
#include <string>
#include <utility>

NOVA_NAMESPACE_BEGIN
namespace log::factories {

struct SynchronousFactory {
    template <typename Sink, typename... SinkArgs>
    static std::shared_ptr<Logger> create(std::string loggerName, SinkArgs &&...args)
    {
        auto sink = std::make_shared<Sink>(std::forward<SinkArgs>(args)...);
        auto newLogger = std::make_shared<Logger>(std::move(loggerName), std::move(sink));
        Registry::instance().initLogger(newLogger);
        return newLogger;
    }
};

}  // namespace log::factories
NOVA_NAMESPACE_END
