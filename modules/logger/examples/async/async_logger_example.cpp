#include <logger/factories/async_factory.h>
#include <logger/sinks/basic_file_sink.h>
#include <logger/sinks/console_sink.h>
#include <logger/log.h>

#include <memory>

namespace novainfra_log = novainfra::log;

int main()
{
    constexpr auto kLogFile = "/tmp/novainfra_async_logger_example.log";

    novainfra_log::setWorker(std::make_shared<novainfra::log::async::AsyncLogThreadPool>(1024, 4));
    auto consoleLogger =
        novainfra::log::factories::createConsoleLoggerMt<novainfra::log::factories::AsyncFactory>(
            "AsyncConsoleExample");
    auto fileLogger =
        novainfra::log::factories::createBasicFileLoggerMt<novainfra::log::factories::AsyncFactory>(
            "AsyncFileExample", kLogFile, true);
    fileLogger->sinks().front()->setPattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v");

    consoleLogger->info("Asynchronous console logger created by AsyncFactory.");
    NOVAINFRA_LOGGER_WARN(fileLogger, "Asynchronous file logger writes to {}.", kLogFile);
    fileLogger->flush();  // Enqueues a reliable flush request; it does not wait for the worker.

    novainfra_log::shutdown();  // Drains pending messages, flushes registered loggers, and joins the worker.
    return 0;
}
