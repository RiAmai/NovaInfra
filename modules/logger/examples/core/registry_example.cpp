#include <logger/sinks/console_sink.h>
#include <logger/novalog.h>

#include <iostream>
#include <memory>
#include <utility>

namespace nova_log = nova::log;

int main()
{
    const auto globalLevel = nova_log::Level::INFO;
    nova_log::Registry::LogLevels levels{
        {"Diagnostics", nova_log::Level::TRACE}
    };
    nova_log::setLevels(std::move(levels), &globalLevel);
    nova_log::setPattern("[%l] [%n] %v");

    auto application = nova::log::factories::createConsoleLoggerMt<>("Application");
    auto diagnostics = nova::log::factories::createConsoleLoggerMt<>("Diagnostics");
    nova_log::setDefaultLogger(application);

    NOVA_LOG_DEBUG("This message is filtered by the global INFO level.");
    NOVA_LOG_INFO("Global macro uses the Application logger.");
    diagnostics->trace("Name-specific TRACE override is active.");

    const bool registered = nova_log::getNameLogger("Application") == application &&
                            nova_log::getNameLogger("Diagnostics") == diagnostics &&
                            nova_log::defaultLogger() == application;

    nova_log::dropLogger("Diagnostics");
    const bool dropped = nova_log::getNameLogger("Diagnostics") == nullptr;
    std::cout << "registry registered=" << std::boolalpha << registered << " dropped=" << dropped << '\n';

    nova_log::shutdown();
    const auto resetDefault = nova_log::defaultLogger();
    const bool reset = resetDefault && resetDefault->name().empty();
    std::cout << "registry reset=" << reset << '\n';
    return registered && dropped && reset ? 0 : 1;
}
