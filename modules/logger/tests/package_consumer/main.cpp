#include <logger/factories/async_factory.h>
#include <logger/sinks/console_sink.h>
#include <logger/logger.h>
#include <logger/macros.h>
#include <logger/novalog.h>

#include <memory>
#include <mutex>
#include <string>

int main()
{
    auto sink = std::make_shared<nova::log::sinks::ConsoleOutSink<std::mutex>>();
    auto logger = std::make_shared<nova::log::Logger>("package-consumer", sink);
    nova::log::setDefaultLogger(logger);
    const std::string format = "package-consumer-{}";
    NOVA_LOGGER_INFO(logger, format, "ok");
    NOVA_LOG_INFO(format, "global-ok");

    nova::log::setWorker(std::make_shared<nova::log::async::AsyncLogThreadPool>(8, 2));
    auto asyncLogger = nova::log::factories::createConsoleLoggerMt<nova::log::factories::AsyncFactory>(
        "package-consumer-async");
    asyncLogger->info(format, "async-ok");
    nova::log::shutdown();
}
