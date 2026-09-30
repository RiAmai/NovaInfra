#include <logger/factories/async_factory.h>
#include <logger/sinks/base_sink.h>
#include <logger/log.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace novainfra_log = novainfra::log;

namespace {

class SlowSink NOVAINFRA_FINAL : public novainfra::log::sinks::BaseSink<std::mutex>
{
public:
    std::size_t processed() const { return processed_.load(); }

private:
    void _log(const novainfra_log::LogMsg &) NOVAINFRA_OVERRIDE
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ++processed_;
    }

    void _flush() NOVAINFRA_OVERRIDE {}

    std::atomic<std::size_t> processed_{0};
};

struct PolicyStats {
    std::size_t processed;
    std::size_t overrun;
    std::size_t discarded;
};

template <typename Factory>
PolicyStats runPolicy(const std::string &name, std::size_t messageCount)
{
    novainfra_log::shutdown();
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(4, 1);
    novainfra_log::setWorker(worker);
    auto logger = Factory::template create<SlowSink>(name);
    auto sink = std::static_pointer_cast<SlowSink>(logger->sinks().front());

    for (std::size_t index = 0; index < messageCount; ++index) {
        logger->info("message {}", index);
    }

    PolicyStats stats{0, worker->overrunCounter(), worker->discardCounter()};
    novainfra_log::shutdown();
    worker.reset();
    stats.processed = sink->processed();
    return stats;
}

void printStats(const char *name, const PolicyStats &stats)
{
    std::cout << std::left << std::setw(16) << name << " processed=" << std::setw(4) << stats.processed
              << " overrun=" << std::setw(4) << stats.overrun << " discarded=" << stats.discarded << '\n';
}

}  // namespace

int main()
{
    constexpr std::size_t kMessageCount = 256;
    const auto blocking = runPolicy<novainfra::log::factories::AsyncFactory>("Blocking", kMessageCount);
    const auto overrun =
        runPolicy<novainfra::log::factories::AsyncOverrunOldestFactory>("OverrunOldest", kMessageCount);
    const auto discard =
        runPolicy<novainfra::log::factories::AsyncDiscardNewFactory>("DiscardNew", kMessageCount);

    printStats("BLOCK", blocking);
    printStats("OVER_RUN_OLDEST", overrun);
    printStats("DISCARD_NEW", discard);

    const bool blockingValid =
        blocking.processed == kMessageCount && blocking.overrun == 0 && blocking.discarded == 0;
    const bool overrunValid =
        overrun.processed < kMessageCount && overrun.overrun > 0 && overrun.discarded == 0;
    const bool discardValid =
        discard.processed < kMessageCount && discard.overrun == 0 && discard.discarded > 0;
    return blockingValid && overrunValid && discardValid ? 0 : 1;
}
