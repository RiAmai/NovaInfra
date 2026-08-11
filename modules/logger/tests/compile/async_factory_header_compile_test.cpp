#include <logger/factories/async_factory.h>
#include <logger/sinks/console_sink.h>

#include <type_traits>

static_assert(
    std::is_same<
        nova::log::factories::AsyncFactory,
        nova::log::factories::AsynchronousFactory<nova::log::async::AsyncOverflowPolicy::BLOCK>>::value,
    "AsyncFactory must use the blocking overflow policy");
static_assert(std::is_same<nova::log::factories::AsyncOverrunOldestFactory,
                           nova::log::factories::AsynchronousFactory<
                               nova::log::async::AsyncOverflowPolicy::OVER_RUN_OLDEST>>::value,
              "AsyncOverrunOldestFactory must use the overrun-oldest policy");
static_assert(
    std::is_same<
        nova::log::factories::AsyncDiscardNewFactory,
        nova::log::factories::AsynchronousFactory<nova::log::async::AsyncOverflowPolicy::DISCARD_NEW>>::value,
    "AsyncDiscardNewFactory must use the discard-new policy");

int main()
{
    nova::log::Registry::instance().setWorker(std::make_shared<nova::log::async::AsyncLogThreadPool>(8, 2));
    auto logger = nova::log::factories::AsyncFactory::create<nova::log::sinks::ConsoleOutSinkMt>(
        "async-factory-header");
    nova::log::Registry::instance().shutdown();
    return logger ? 0 : 1;
}
