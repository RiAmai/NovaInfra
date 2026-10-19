#include <core/circular_queue.h>
#ifdef CONSUMER_LOGGER
#  include <logger/logger.h>
#endif
#ifdef CONSUMER_CONFIG
#  include <config/config.h>
#endif
#ifdef CONSUMER_THREADPOOL
#  include <thread_pool/thread_pool.h>
#endif
#ifdef CONSUMER_SQLITEPOOL
#  include <sqlite_pool/connection_pool.h>
#  include <filesystem>
#  include <unistd.h>
#endif
#ifdef CONSUMER_EVENTBUS
#  include <event_bus/event_bus.h>
#endif
#ifdef CONSUMER_STATEMACHINE
#  include <state_machine/runtime.h>
#  include <state_machine/state_machine.h>
#endif

int main()
{
    novainfra::CircularQueue<int> queue(1);
    queue.pushBack(7);
    if (queue.front() != 7)
        return 1;
#ifdef CONSUMER_LOGGER
    novainfra::log::Logger logger("installed-consumer");
    logger.info("value {}", queue.front());
#endif
#ifdef CONSUMER_CONFIG
    novainfra::config::Schema schema;
    schema.optional<int>("value", 7);
    auto loaded = novainfra::config::ConfigManager::load(std::move(schema));
    if (!loaded || loaded.value().snapshot().get<int>("value").value() != 7)
        return 2;
#endif
#ifdef CONSUMER_THREADPOOL
    novainfra::threadpool::ThreadPoolOptions options;
    options.workerCount = 1;
    options.queueCapacity = 8;
    novainfra::threadpool::ThreadPool pool(options);
    if (pool.submit([] { return 7; }).get() != 7)
        return 3;
#endif
#ifdef CONSUMER_EVENTBUS
    novainfra::eventbus::EventBus bus;
    int received = 0;
    auto subscription = bus.subscribe<int>([&](int value) { received = value; });
    bus.publish(7);
    if (received != 7)
        return 4;
#endif
#ifdef CONSUMER_SQLITEPOOL
    const auto path =
        std::filesystem::temp_directory_path() / ("novainfra-installed-" + std::to_string(getpid()) + ".db");
    {
        novainfra::sqlitepool::PoolConfig config;
        config.readConnections = 1;
        novainfra::sqlitepool::ConnectionPool db(path.string(), config);
        if (db.read().query("SELECT 7 AS value")[0].at("value") != "7")
            return 5;
    }
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + "-wal");
    std::filesystem::remove(path.string() + "-shm");
#endif
    return 0;
}
