#include <logger/factories/async_factory.h>
#include <logger/sinks/console_sink.h>

#include <type_traits>

static_assert(std::is_same<novainfra::log::factories::AsyncFactory,
                           novainfra::log::factories::AsynchronousFactory<
                               novainfra::log::async::AsyncOverflowPolicy::BLOCK>>::value,
              "AsyncFactory must use the blocking overflow policy");
static_assert(std::is_same<novainfra::log::factories::AsyncOverrunOldestFactory,
                           novainfra::log::factories::AsynchronousFactory<
                               novainfra::log::async::AsyncOverflowPolicy::OVER_RUN_OLDEST>>::value,
              "AsyncOverrunOldestFactory must use the overrun-oldest policy");
static_assert(std::is_same<novainfra::log::factories::AsyncDiscardNewFactory,
                           novainfra::log::factories::AsynchronousFactory<
                               novainfra::log::async::AsyncOverflowPolicy::DISCARD_NEW>>::value,
              "AsyncDiscardNewFactory must use the discard-new policy");

int main()
{
    novainfra::log::Registry::instance().setWorker(
        std::make_shared<novainfra::log::async::AsyncLogThreadPool>(8, 2));
    auto logger = novainfra::log::factories::AsyncFactory::create<novainfra::log::sinks::ConsoleOutSinkMt>(
        "async-factory-header");
    novainfra::log::Registry::instance().shutdown();
    return logger ? 0 : 1;
}
