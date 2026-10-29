#include <logger/async/async_log_thread_pool.h>
#include <logger/async/async_logger.h>
#include <logger/logger.h>
#include <logger/sinks/callback_sink.h>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct ObservedMessage {
    novainfra::SystemTimePoint timestamp;
    novainfra::log::Level level{novainfra::log::Level::OFF};
    std::string loggerName;
    std::string message;
    std::string file;
    std::string function;
    int line{0};
    bool staticSource{false};
};

ObservedMessage copyMessage(const novainfra::log::LogMsg &msg)
{
    return {msg.timestamp,
            msg.level,
            std::string(msg.loggerName),
            std::string(msg.message.data(), msg.message.size()),
            std::string(msg.source.file),
            std::string(msg.source.function),
            msg.source.line,
            msg.source.hasStaticStorage()};
}

TEST(CallbackSink, ReceivesRawMessageFieldsAndEmbeddedNulls)
{
    ObservedMessage observed;
    auto sink = std::make_shared<novainfra::log::sinks::CallbackSinkSt>(
        [&observed](const novainfra::log::LogMsg &msg) { observed = copyMessage(msg); });
    novainfra::log::Logger logger("callback", sink);
    const auto timestamp = novainfra::core::now();
    constexpr auto source = novainfra::log::SourceLocation::fromStatic("callback.cpp", 27, "callbackTest");
    const std::string message("raw\0message", 11);

    logger.log(timestamp, source, novainfra::log::Level::WARN,
               std::string_view(message.data(), message.size()));

    EXPECT_EQ(observed.timestamp, timestamp);
    EXPECT_EQ(observed.level, novainfra::log::Level::WARN);
    EXPECT_EQ(observed.loggerName, "callback");
    EXPECT_EQ(observed.message, message);
    EXPECT_EQ(observed.file, "callback.cpp");
    EXPECT_EQ(observed.function, "callbackTest");
    EXPECT_EQ(observed.line, 27);
    EXPECT_TRUE(observed.staticSource);
}

TEST(CallbackSink, RejectsEmptyCallback)
{
    EXPECT_THROW(novainfra::log::sinks::CallbackSinkMt(novainfra::log::sinks::CallbackSinkMt::Callback{}),
                 std::invalid_argument);
    EXPECT_THROW(novainfra::log::sinks::CallbackSinkSt(novainfra::log::sinks::CallbackSinkSt::Callback{}),
                 std::invalid_argument);
}

TEST(CallbackSink, ReportsThreadSafetyFromMutexType)
{
    novainfra::log::sinks::CallbackSinkMt mt([](const novainfra::log::LogMsg &) {});
    novainfra::log::sinks::CallbackSinkSt st([](const novainfra::log::LogMsg &) {});

    EXPECT_TRUE(mt.isThreadSafe());
    EXPECT_FALSE(st.isThreadSafe());
}

TEST(CallbackSink, HonorsSinkLevelAndFlushDoesNotInvokeCallback)
{
    int calls = 0;
    auto sink = std::make_shared<novainfra::log::sinks::CallbackSinkSt>(
        [&calls](const novainfra::log::LogMsg &) { ++calls; });
    sink->setLevel(novainfra::log::Level::ERROR);
    novainfra::log::Logger logger("filtered-callback", sink);

    logger.info("filtered");
    logger.error("accepted");
    logger.flush();

    EXPECT_EQ(calls, 1);
}

TEST(CallbackSink, MultiThreadedVariantSerializesCallbackCalls)
{
    constexpr int kThreadCount = 8;
    constexpr int kMessagesPerThread = 1000;
    std::atomic<int> active{0};
    std::atomic<int> calls{0};
    std::atomic<bool> overlap{false};
    auto sink = std::make_shared<novainfra::log::sinks::CallbackSinkMt>([&](const novainfra::log::LogMsg &) {
        if (active.fetch_add(1, std::memory_order_acq_rel) != 0) {
            overlap.store(true, std::memory_order_relaxed);
        }
        std::this_thread::yield();
        calls.fetch_add(1, std::memory_order_relaxed);
        active.fetch_sub(1, std::memory_order_release);
    });
    novainfra::log::Logger logger("concurrent-callback", sink);
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for (int thread = 0; thread < kThreadCount; ++thread) {
        threads.emplace_back([&logger] {
            for (int message = 0; message < kMessagesPerThread; ++message) {
                logger.info("message");
            }
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }

    EXPECT_EQ(calls.load(std::memory_order_relaxed), kThreadCount * kMessagesPerThread);
    EXPECT_FALSE(overlap.load(std::memory_order_relaxed));
}

TEST(CallbackSink, StandardCallbackExceptionUsesLoggerErrorHandler)
{
    auto sink = std::make_shared<novainfra::log::sinks::CallbackSinkSt>(
        [](const novainfra::log::LogMsg &) { throw std::runtime_error("callback failure"); });
    novainfra::log::Logger logger("throwing-callback", sink);
    int errors = 0;
    std::string errorMessage;
    logger.setErrHandler([&](const std::string &message) {
        ++errors;
        errorMessage = message;
    });

    EXPECT_NO_THROW(logger.info("message"));
    EXPECT_EQ(errors, 1);
    EXPECT_NE(errorMessage.find("callback failure"), std::string::npos);
}

TEST(CallbackSink, UnknownCallbackExceptionKeepsLoggerSemantics)
{
    auto sink = std::make_shared<novainfra::log::sinks::CallbackSinkSt>(
        [](const novainfra::log::LogMsg &) { throw 42; });
    novainfra::log::Logger logger("throwing-callback", sink);
    int errors = 0;
    logger.setErrHandler([&errors](const std::string &) { ++errors; });

    EXPECT_THROW(logger.info("message"), int);
    EXPECT_GE(errors, 1);
}

TEST(CallbackSink, SingleThreadedVariantRequiresOneAsyncWorker)
{
    auto sink =
        std::make_shared<novainfra::log::sinks::CallbackSinkSt>([](const novainfra::log::LogMsg &) {});
    auto multiWorker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(8, 2);

    EXPECT_THROW((void)std::make_shared<novainfra::log::async::AsyncLogger>("callback", sink, multiWorker),
                 std::invalid_argument);

    multiWorker.reset();
    std::atomic<int> calls{0};
    auto oneWorker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(8, 1);
    auto serialSink = std::make_shared<novainfra::log::sinks::CallbackSinkSt>(
        [&calls](const novainfra::log::LogMsg &) { calls.fetch_add(1, std::memory_order_relaxed); });
    auto logger = std::make_shared<novainfra::log::async::AsyncLogger>("callback", serialSink, oneWorker);
    logger->info("message");
    oneWorker.reset();

    EXPECT_EQ(calls.load(std::memory_order_relaxed), 1);
}

}  // namespace
