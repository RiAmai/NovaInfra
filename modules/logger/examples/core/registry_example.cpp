#include <logger/sinks/console_sink.h>
#include <logger/log.h>

#include <iostream>
#include <memory>
#include <utility>

namespace novainfra_log = novainfra::log;

int main()
{
    const auto globalLevel = novainfra_log::Level::INFO;
    novainfra_log::Registry::LogLevels levels{
        {"Diagnostics", novainfra_log::Level::TRACE}
    };
    novainfra_log::setLevels(std::move(levels), &globalLevel);
    novainfra_log::setPattern("[%l] [%n] %v");

    auto application = novainfra::log::factories::createConsoleLoggerMt<>("Application");
    auto diagnostics = novainfra::log::factories::createConsoleLoggerMt<>("Diagnostics");
    novainfra_log::setDefaultLogger(application);

    NOVAINFRA_LOG_DEBUG("This message is filtered by the global INFO level.");
    NOVAINFRA_LOG_INFO("Global macro uses the Application logger.");
    diagnostics->trace("Name-specific TRACE override is active.");

    const bool registered = novainfra_log::getNameLogger("Application") == application &&
                            novainfra_log::getNameLogger("Diagnostics") == diagnostics &&
                            novainfra_log::defaultLogger() == application;

    novainfra_log::dropLogger("Diagnostics");
    const bool dropped = novainfra_log::getNameLogger("Diagnostics") == nullptr;
    std::cout << "registry registered=" << std::boolalpha << registered << " dropped=" << dropped << '\n';

    novainfra_log::shutdown();
    const auto resetDefault = novainfra_log::defaultLogger();
    const bool reset = resetDefault && resetDefault->name().empty();
    std::cout << "registry reset=" << reset << '\n';
    return registered && dropped && reset ? 0 : 1;
}
