#include <logger/factories/async_factory.h>
#include <logger/sinks/basic_file_sink.h>
#include <logger/sinks/console_sink.h>
#include <logger/novalog.h>

#include <memory>

namespace nova_log = nova::log;

int main()
{
    constexpr auto kLogFile = "/tmp/novacore_async_logger_example.log";

    nova_log::setWorker(std::make_shared<nova::log::async::AsyncLogThreadPool>(1024, 4));
    auto consoleLogger = nova::log::factories::createConsoleLoggerMt<nova::log::factories::AsyncFactory>(
        "AsyncConsoleExample");
    auto fileLogger = nova::log::factories::createBasicFileLoggerMt<nova::log::factories::AsyncFactory>(
        "AsyncFileExample", kLogFile, true);
    fileLogger->sinks().front()->setPattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v");

    consoleLogger->info("Asynchronous console logger created by AsyncFactory.");
    NOVA_LOGGER_WARN(fileLogger, "Asynchronous file logger writes to {}.", kLogFile);
    fileLogger->flush();  // Enqueues a reliable flush request; it does not wait for the worker.

    nova_log::shutdown();  // Drains pending messages, flushes registered loggers, and joins the worker.
    return 0;
}
