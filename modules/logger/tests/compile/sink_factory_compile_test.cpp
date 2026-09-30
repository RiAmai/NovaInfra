#include <logger/factories/async_factory.h>
#include <logger/factories/sink_factories.h>
#include <logger/sinks/basic_file_sink.h>
#include <logger/sinks/console_sink.h>
#include <logger/sinks/rotating_file_sink.h>

int main()
{
    auto *basicFactory = &novainfra::log::factories::createBasicFileLoggerMt<>;
    auto *rotatingFactory = &novainfra::log::factories::createRotatingFileLoggerMt<>;
    auto *asyncConsoleFactory =
        &novainfra::log::factories::createConsoleLoggerMt<novainfra::log::factories::AsyncFactory>;
    auto *asyncBasicFactory = &novainfra::log::factories::createBasicFileLoggerMt<
        novainfra::log::factories::AsyncOverrunOldestFactory>;
    auto *asyncRotatingFactory = &novainfra::log::factories::createRotatingFileLoggerMt<
        novainfra::log::factories::AsyncDiscardNewFactory>;
    NOVAINFRA_UNUSED(basicFactory);
    NOVAINFRA_UNUSED(rotatingFactory);
    NOVAINFRA_UNUSED(asyncConsoleFactory);
    NOVAINFRA_UNUSED(asyncBasicFactory);
    NOVAINFRA_UNUSED(asyncRotatingFactory);
    return 0;
}
