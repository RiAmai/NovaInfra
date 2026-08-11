#pragma once

#include <logger/factories/synchronous_factory.h>
#include <logger/sinks/basic_file_sink.h>
#include <logger/sinks/console_sink.h>
#include <logger/sinks/rotating_file_sink.h>

#include <core/macros.h>
#include <core/namespace.h>

#include <memory>
#include <string>

NOVA_NAMESPACE_BEGIN
namespace log::factories {

// console
template <typename Factory = SynchronousFactory>
NOVA_INLINE std::shared_ptr<Logger> createConsoleLoggerMt(const std::string &loggerName)
{
    return Factory::template create<sinks::ConsoleOutSinkMt>(loggerName);
}

template <typename Factory = SynchronousFactory>
NOVA_INLINE std::shared_ptr<Logger> createConsoleLoggerSt(const std::string &loggerName)
{
    return Factory::template create<sinks::ConsoleOutSinkSt>(loggerName);
}

// basic file
template <typename Factory = SynchronousFactory>
NOVA_INLINE std::shared_ptr<Logger>
    createBasicFileLoggerMt(const std::string &loggerName, const Filename &filename, bool truncate = false,
                            const sinks::FileEventHandlers &eventHandlers = {})
{
    return Factory::template create<sinks::BasicFileSinkMt>(loggerName, filename, truncate, eventHandlers);
}

template <typename Factory = SynchronousFactory>
NOVA_INLINE std::shared_ptr<Logger>
    createBasicFileLoggerSt(const std::string &loggerName, const Filename &filename, bool truncate = false,
                            const sinks::FileEventHandlers &eventHandlers = {})
{
    return Factory::template create<sinks::BasicFileSinkSt>(loggerName, filename, truncate, eventHandlers);
}

// rotating file
template <typename Factory = SynchronousFactory>
NOVA_INLINE std::shared_ptr<Logger>
    createRotatingFileLoggerMt(const std::string &loggerName, const Filename &filename, std::size_t maxSize,
                               std::size_t maxFileNum, bool rotateOnOpen = false,
                               const sinks::FileEventHandlers &eventHandlers = {})
{
    return Factory::template create<sinks::RotatingFileSinkMt>(loggerName, filename, maxSize, maxFileNum,
                                                               rotateOnOpen, eventHandlers);
}

template <typename Factory = SynchronousFactory>
NOVA_INLINE std::shared_ptr<Logger>
    createRotatingFileLoggerSt(const std::string &loggerName, const Filename &filename, std::size_t maxSize,
                               std::size_t maxFileNum, bool rotateOnOpen = false,
                               const sinks::FileEventHandlers &eventHandlers = {})
{
    return Factory::template create<sinks::RotatingFileSinkSt>(loggerName, filename, maxSize, maxFileNum,
                                                               rotateOnOpen, eventHandlers);
}

}  // namespace log::factories
NOVA_NAMESPACE_END
