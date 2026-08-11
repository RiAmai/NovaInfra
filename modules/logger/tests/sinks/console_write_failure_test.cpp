#include <logger/sinks/console_sink.h>
#include <logger/logger.h>

#include <cstdio>
#include <memory>
#include <mutex>

int main()
{
    if (std::freopen("/dev/full", "w", stdout) == nullptr) {
        return 2;
    }

    auto sink = std::make_shared<nova::log::sinks::ConsoleOutSink<std::mutex>>();
    nova::log::Logger logger("console-failure", sink);
    logger.setFlushLevel(nova::log::Level::INFO);
    bool errorReported = false;
    logger.setErrHandler([&errorReported](const std::string &) { errorReported = true; });

    logger.info("write failure");

    return errorReported ? 0 : 1;
}
