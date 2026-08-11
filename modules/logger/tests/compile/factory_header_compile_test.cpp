#include <logger/factories/synchronous_factory.h>
#include <logger/sinks/console_sink.h>

int main()
{
    auto logger = nova::log::factories::SynchronousFactory::create<nova::log::sinks::ConsoleOutSinkMt>(
        "factory-header");
    return logger ? 0 : 1;
}
