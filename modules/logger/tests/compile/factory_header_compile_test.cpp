#include <logger/factories/synchronous_factory.h>
#include <logger/sinks/console_sink.h>

int main()
{
    auto logger =
        novainfra::log::factories::SynchronousFactory::create<novainfra::log::sinks::ConsoleOutSinkMt>(
            "factory-header");
    return logger ? 0 : 1;
}
