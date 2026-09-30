#include <logger/logger.h>
#include <logger/macros.h>
#include <logger/sinks/basic_file_sink.h>
#include <logger/sinks/console_sink.h>

#include <cstdio>
#include <memory>
#include <string_view>

namespace {

constexpr auto kDefaultLogFile = "/tmp/novainfra_backtrace_example.log";

void printUsage(const char *program)
{
    std::fprintf(stderr, "Usage: %s <error|critical> [log-file]\n", program);
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        printUsage(argv[0]);
        return 2;
    }

    const std::string_view mode = argv[1];
    if (mode != "error" && mode != "critical") {
        printUsage(argv[0]);
        return 2;
    }

    const char *logFile = argc == 3 ? argv[2] : kDefaultLogFile;
    auto consoleSink = std::make_shared<novainfra::log::sinks::ConsoleOutSinkMt>();
    auto fileSink = std::make_shared<novainfra::log::sinks::BasicFileSinkMt>(logFile, false);
    auto logger = std::make_shared<novainfra::log::Logger>(
        "BacktraceExample", novainfra::log::sinks::SinkInitList{consoleSink, fileSink});

    logger->enableBacktrace(16);
    logger->setLevel(novainfra::log::Level::WARN);
    logger->setFlushLevel(novainfra::log::Level::ERROR);

    logger->debug("Buffered context: request id {}", 42);
    logger->info("Buffered context: preparing failure demonstration");
    if (mode == "error") {
        NOVAINFRA_LOGGER_ERROR(logger, "Demonstration ERROR triggered");
    } else {
        NOVAINFRA_LOGGER_CRITICAL(logger, "Demonstration CRITICAL triggered");
    }

    logger->dumpBacktrace();
    return 0;
}
