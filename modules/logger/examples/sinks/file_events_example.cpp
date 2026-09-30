#include <logger/sinks/basic_file_sink.h>
#include <logger/logger.h>

#include <core/os_utils.h>

#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace novainfra_log = novainfra::log;

int main()
{
    constexpr auto kLogFile = "/tmp/novainfra_file_events_example.log";
    std::vector<std::string> events;
    bool openedFileWasValid = false;

    novainfra::log::sinks::FileEventHandlers handlers;
    handlers.beforeOpen = [&](const novainfra::Filename &filename) {
        events.push_back("beforeOpen");
        std::cout << "beforeOpen " << filename << '\n';
    };
    handlers.afterOpen = [&](const novainfra::Filename &filename, std::FILE *file) {
        events.push_back("afterOpen");
        openedFileWasValid = file != nullptr;
        std::cout << "afterOpen " << filename << '\n';
    };
    handlers.beforeClose = [&](const novainfra::Filename &filename, std::FILE *file) {
        events.push_back("beforeClose");
        openedFileWasValid = openedFileWasValid && file != nullptr;
        std::cout << "beforeClose " << filename << '\n';
    };
    handlers.afterClose = [&](const novainfra::Filename &filename) {
        events.push_back("afterClose");
        std::cout << "afterClose " << filename << '\n';
    };

    {
        auto sink =
            std::make_shared<novainfra::log::sinks::BasicFileSink<std::mutex>>(kLogFile, true, handlers);
        sink->setPattern("%v");
        novainfra_log::Logger logger("FileEvents", sink);
        logger.info("file event callbacks");
        logger.flush();
    }

    const std::vector<std::string> expectedEvents{"beforeOpen", "afterOpen", "beforeClose", "afterClose"};
    const bool callbacksValid =
        openedFileWasValid && events == expectedEvents && novainfra::core::PathExists(kLogFile);
    std::cout.flush();

    bool errorReported = true;
#ifndef NOVAINFRA_NO_EXCEPTIONS
    if (novainfra::core::PathExists("/dev/full")) {
        errorReported = false;
        auto sink = std::make_shared<novainfra::log::sinks::BasicFileSink<std::mutex>>("/dev/full");
        sink->setPattern("%v");
        novainfra_log::Logger logger("FileFailure", sink);
        logger.setErrHandler([&](const std::string &message) {
            errorReported = true;
            std::cerr << "error handler: " << message << '\n';
        });
        logger.info(std::string(8192, 'x'));
        logger.flush();
    } else {
        std::cout << "/dev/full is unavailable; IO failure injection skipped.\n";
    }
#else
    std::cout << "IO failure injection is disabled in the no-exceptions build.\n";
#endif

    std::cout << "callbacks valid=" << std::boolalpha << callbacksValid << " error reported=" << errorReported
              << '\n';
    return callbacksValid && errorReported ? 0 : 1;
}
