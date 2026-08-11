#include <logger/factories/async_factory.h>
#include <logger/sinks/basic_file_sink.h>
#include <logger/novalog.h>

#include <cstddef>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace nova_log = nova::log;

int main()
{
    constexpr auto kLogFile = "/tmp/novacore_async_st_logger_example.log";
    constexpr std::size_t kProducerCount = 4;
    constexpr std::size_t kMessagesPerProducer = 100;
    constexpr std::size_t kExpectedMessages = kProducerCount * kMessagesPerProducer;

    // Producer threads only enqueue messages. The single async worker serializes all access to the
    // St sink, so the sink itself does not need a mutex.
    nova_log::setWorker(std::make_shared<nova::log::async::AsyncLogThreadPool>(128, 1));
    auto logger = nova::log::factories::createBasicFileLoggerSt<nova::log::factories::AsyncFactory>(
        "AsyncStFileExample", kLogFile, true);
    logger->sinks().front()->setPattern("[%t] [%l] [%n] %v");

    std::vector<std::thread> producers;
    producers.reserve(kProducerCount);
    for (std::size_t producer = 0; producer < kProducerCount; ++producer) {
        producers.emplace_back([logger, producer] {
            for (std::size_t message = 0; message < kMessagesPerProducer; ++message) {
                NOVA_LOGGER_INFO(logger, "producer={} message={}", producer, message);
            }
        });
    }

    for (auto &producer : producers) {
        producer.join();
    }

    logger->flush();       // Reliably enqueued, but completion is asynchronous.
    nova_log::shutdown();  // Drains the queue, flushes the St sink, and joins the worker.
    logger.reset();

    std::ifstream logFile(kLogFile);
    std::size_t writtenMessages = 0;
    for (std::string line; std::getline(logFile, line);) {
        ++writtenMessages;
    }

    std::cout << "Async St log file: " << kLogFile << '\n'
              << "Expected messages: " << kExpectedMessages << ", written messages: " << writtenMessages
              << '\n';
    return writtenMessages == kExpectedMessages ? 0 : 1;
}
