#include <logger/factories/async_factory.h>
#include <logger/factories/sink_factories.h>
#include <logger/sinks/basic_file_sink.h>
#include <logger/sinks/console_sink.h>
#include <logger/sinks/rotating_file_sink.h>

int main()
{
    auto *basicFactory = &nova::log::factories::createBasicFileLoggerMt<>;
    auto *rotatingFactory = &nova::log::factories::createRotatingFileLoggerMt<>;
    auto *asyncConsoleFactory =
        &nova::log::factories::createConsoleLoggerMt<nova::log::factories::AsyncFactory>;
    auto *asyncBasicFactory =
        &nova::log::factories::createBasicFileLoggerMt<nova::log::factories::AsyncOverrunOldestFactory>;
    auto *asyncRotatingFactory =
        &nova::log::factories::createRotatingFileLoggerMt<nova::log::factories::AsyncDiscardNewFactory>;
    NOVA_UNUSED(basicFactory);
    NOVA_UNUSED(rotatingFactory);
    NOVA_UNUSED(asyncConsoleFactory);
    NOVA_UNUSED(asyncBasicFactory);
    NOVA_UNUSED(asyncRotatingFactory);
    return 0;
}
