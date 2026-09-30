#include <logger/sinks/basic_file_sink.h>
#include <logger/sinks/console_sink.h>
#include <logger/logger.h>
#include <logger/log.h>

#include <memory>
#include <mutex>

namespace novainfra_log = novainfra::log;

int main()
{
    constexpr auto kLogFile = "/tmp/novainfra_logger_example.log";
    using ConsoleSink = novainfra::log::sinks::ConsoleOutSinkMt;
    using FileSink = novainfra::log::sinks::BasicFileSinkMt;

    // ConsoleOutSink routes TRACE/DEBUG/INFO to stdout and WARN or higher to stderr.
    auto consoleSink = std::make_shared<ConsoleSink>();
    // Truncate the example-owned file so every run contains only the current output.
    auto fileSink = std::make_shared<FileSink>(kLogFile, true);
    // Each sink owns its formatter; keep the console default and make the file output compact.
    fileSink->setPattern("<%Y-%m-%d %H:%M:%S.%e> [%l] [%n] %v");
    auto logger = std::make_shared<novainfra_log::Logger>(
        "LoggerExample", novainfra::log::sinks::SinkInitList{consoleSink, fileSink});
    logger->setLevel(novainfra_log::Level::TRACE);

    logger->info("Log file - {}", kLogFile);
    logger->trace("Member API - trace.");
    logger->debug("Member API - debug.");
    logger->info("Member API - info.");
    logger->warn("Member API - warn.");
    logger->error("Member API - error.");
    logger->critical("Member API - critical.");

    logger->info("fmt arguments - int {}, float {:.4f}, string {}.", 10, 10.1123, "test");
    NOVAINFRA_LOGGER_INFO(logger, "Macro API captures source location - value {}.", 42);

    logger->flush();
    return 0;
}
