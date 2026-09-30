#include <logger/factories/async_factory.h>
#include <logger/sinks/console_sink.h>
#include <logger/logger.h>
#include <logger/macros.h>
#include <logger/log.h>

#include <memory>
#include <mutex>
#include <string>

int main()
{
    auto sink = std::make_shared<novainfra::log::sinks::ConsoleOutSink<std::mutex>>();
    auto logger = std::make_shared<novainfra::log::Logger>("package-consumer", sink);
    novainfra::log::setDefaultLogger(logger);
    const std::string format = "package-consumer-{}";
    NOVAINFRA_LOGGER_INFO(logger, format, "ok");
    NOVAINFRA_LOG_INFO(format, "global-ok");

    novainfra::log::setWorker(std::make_shared<novainfra::log::async::AsyncLogThreadPool>(8, 2));
    auto asyncLogger =
        novainfra::log::factories::createConsoleLoggerMt<novainfra::log::factories::AsyncFactory>(
            "package-consumer-async");
    asyncLogger->info(format, "async-ok");
    novainfra::log::shutdown();
}
