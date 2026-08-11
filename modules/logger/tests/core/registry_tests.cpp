#include <logger/factories/async_factory.h>
#include <logger/async/async_logger.h>
#include <logger/sinks/base_sink.h>
#include <logger/factories/synchronous_factory.h>
#include <logger/novalog.h>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct RecordedMessage {
    std::string message;
    bool hasSource = false;
};

class ThreadSafeRecordingSink final : public nova::log::sinks::BaseSink<std::mutex>
{
public:
    std::vector<RecordedMessage> messages() const
    {
        std::lock_guard<std::mutex> lock(messagesMutex_);
        return messages_;
    }

    std::vector<std::string> formattedMessages() const
    {
        std::lock_guard<std::mutex> lock(messagesMutex_);
        return formattedMessages_;
    }

    int flushCount() const { return flushCount_.load(); }

private:
    void _log(const nova::log::LogMsg &msg) override
    {
        std::string formatted;
        formatter_->format(msg, formatted);
        std::lock_guard<std::mutex> lock(messagesMutex_);
        messages_.push_back({std::string(msg.message), !msg.source.empty()});
        formattedMessages_.push_back(std::move(formatted));
    }

    void _flush() override { ++flushCount_; }

    mutable std::mutex messagesMutex_;
    std::vector<RecordedMessage> messages_;
    std::vector<std::string> formattedMessages_;
    std::atomic<int> flushCount_{0};
};

class ThrowingLogSink final : public nova::log::sinks::ISink
{
public:
    void log(const nova::log::LogMsg &) override { throw std::runtime_error("registry sink failure"); }
    void setPattern(const std::string &) override {}
    void setFormatter(std::unique_ptr<nova::log::formatters::IFormatter>) override {}
    void flush() override {}
};

class ConstructionCountingSink final : public nova::log::sinks::ISink
{
public:
    explicit ConstructionCountingSink(std::atomic<int> &constructionCount) { ++constructionCount; }

    void log(const nova::log::LogMsg &) override {}
    void setPattern(const std::string &) override {}
    void setFormatter(std::unique_ptr<nova::log::formatters::IFormatter>) override {}
    void flush() override {}
};

class ReentrantFlushSink final : public nova::log::sinks::ISink
{
public:
    void log(const nova::log::LogMsg &) override {}
    void setPattern(const std::string &) override {}
    void setFormatter(std::unique_ptr<nova::log::formatters::IFormatter>) override {}
    void flush() override
    {
        observedDefault = static_cast<bool>(nova::log::defaultLogger());
        ++flushCount;
    }

    std::atomic<bool> observedDefault{false};
    std::atomic<int> flushCount{0};
};

class WorkerLookupFlushSink final : public nova::log::sinks::ISink
{
public:
    void log(const nova::log::LogMsg &) override { ++logCount; }
    void setPattern(const std::string &) override {}
    void setFormatter(std::unique_ptr<nova::log::formatters::IFormatter>) override {}
    void flush() override
    {
        observedClearedWorker = !nova::log::worker();
        ++flushCount;
    }

    std::atomic<bool> observedClearedWorker{false};
    std::atomic<int> logCount{0};
    std::atomic<int> flushCount{0};
};

class ReentrantDestructorLogger final : public nova::log::Logger
{
public:
    ReentrantDestructorLogger(std::string name, std::atomic<bool> &destroyed)
        : Logger(std::move(name)), destroyed_(destroyed)
    {
    }

    ~ReentrantDestructorLogger() override
    {
        NOVA_UNUSED(nova::log::defaultLogger());
        destroyed_ = true;
    }

private:
    std::atomic<bool> &destroyed_;
};

class RegistryTest : public ::testing::Test
{
protected:
    void SetUp() override { nova::log::shutdown(); }
    void TearDown() override { nova::log::shutdown(); }
};

TEST_F(RegistryTest, DefaultLoggerIsAlwaysAvailableAndCannotBeDropped)
{
    auto initial = nova::log::defaultLogger();
    ASSERT_NE(initial, nullptr);
    EXPECT_EQ(initial->name(), "");
    EXPECT_EQ(initial->level(), nova::log::Level::INFO);
    EXPECT_EQ(initial->flushLevel(), nova::log::Level::OFF);
    EXPECT_EQ(nova::log::getNameLogger(""), initial);

    EXPECT_THROW(nova::log::setDefaultLogger(nullptr), std::invalid_argument);
    EXPECT_THROW(nova::log::dropLogger(""), std::invalid_argument);
    EXPECT_THROW(nova::log::registerLogger(nullptr), std::invalid_argument);
    EXPECT_THROW(nova::log::registerOrReplaceLogger(nullptr), std::invalid_argument);
    EXPECT_THROW(nova::log::initLogger(nullptr), std::invalid_argument);
    EXPECT_THROW(nova::log::setFormatter(nullptr), std::invalid_argument);
    EXPECT_THROW(nova::log::applyAll({}), std::invalid_argument);

    nova::log::dropAll();
    EXPECT_EQ(nova::log::defaultLogger(), initial);
    EXPECT_EQ(nova::log::getNameLogger(""), initial);
}

TEST_F(RegistryTest, ReplacementKeepsSnapshotsAndOldNamedLoggerAlive)
{
    auto first = std::make_shared<nova::log::Logger>("first");
    auto second = std::make_shared<nova::log::Logger>("second");
    nova::log::setDefaultLogger(first);
    auto snapshot = nova::log::defaultLogger();

    nova::log::setDefaultLogger(second);

    EXPECT_EQ(snapshot, first);
    EXPECT_EQ(nova::log::defaultLogger(), second);
    EXPECT_EQ(nova::log::getNameLogger("first"), first);
    EXPECT_EQ(nova::log::getNameLogger("second"), second);
}

TEST_F(RegistryTest, ReplacingTheDefaultNameKeepsDefaultAndNamedLookupConsistent)
{
    auto first = std::make_shared<nova::log::Logger>("same-name");
    auto replacement = std::make_shared<nova::log::Logger>("same-name");
    nova::log::setDefaultLogger(first);

    nova::log::registerOrReplaceLogger(replacement);

    EXPECT_EQ(nova::log::defaultLogger(), replacement);
    EXPECT_EQ(nova::log::getNameLogger("same-name"), replacement);
    EXPECT_NE(first, replacement);
}

TEST_F(RegistryTest, GlobalConfigurationAppliesToEveryEntryPath)
{
    auto &registry = nova::log::Registry::instance();
    const auto globalLevel = nova::log::Level::WARN;
    registry.setLevels(
        {
            {"special", nova::log::Level::DEBUG}
    },
        &globalLevel);
    registry.setFlushLevel(nova::log::Level::ERROR);
    registry.setFormatter(std::make_unique<nova::log::formatters::PatternFormatter>("%v"));
    registry.enableBacktrace(4);

    std::atomic<int> errorCount{0};
    registry.setErrHandler([&](const std::string &) { ++errorCount; });

    auto registeredSink = std::make_shared<ThreadSafeRecordingSink>();
    auto registered = std::make_shared<nova::log::Logger>("special", registeredSink);
    registry.registerLogger(registered);

    auto defaultSink = std::make_shared<ThrowingLogSink>();
    auto newDefault = std::make_shared<nova::log::Logger>("configured-default", defaultSink);
    registry.setDefaultLogger(newDefault);

    EXPECT_EQ(registered->level(), nova::log::Level::DEBUG);
    EXPECT_EQ(registered->flushLevel(), nova::log::Level::ERROR);
    EXPECT_TRUE(registered->shouldBacktrace());
    EXPECT_EQ(newDefault->level(), nova::log::Level::WARN);
    EXPECT_EQ(newDefault->flushLevel(), nova::log::Level::ERROR);
    EXPECT_TRUE(newDefault->shouldBacktrace());

    registered->info("configured");
    newDefault->error("failure");
    ASSERT_EQ(registeredSink->messages().size(), 1U);
    EXPECT_EQ(registeredSink->messages().front().message, "configured");
    ASSERT_EQ(registeredSink->formattedMessages().size(), 1U);
    EXPECT_EQ(registeredSink->formattedMessages().front(), "configured\n");
    EXPECT_EQ(errorCount.load(), 1);

    registry.setAutoRegistration(false);
    auto initialized = std::make_shared<nova::log::Logger>("initialized");
    registry.initLogger(initialized);
    EXPECT_EQ(initialized->level(), nova::log::Level::WARN);
    EXPECT_TRUE(initialized->shouldBacktrace());
    EXPECT_EQ(registry.getNameLogger("initialized"), nullptr);
}

TEST_F(RegistryTest, GlobalBacktraceControlsExistingAndFutureLoggers)
{
    auto existing = std::make_shared<nova::log::Logger>("existing");
    nova::log::registerLogger(existing);

    nova::log::enableBacktrace(3);
    EXPECT_TRUE(nova::log::defaultLogger()->shouldBacktrace());
    EXPECT_TRUE(existing->shouldBacktrace());

    auto enabledLater = std::make_shared<nova::log::Logger>("enabled-later");
    nova::log::registerLogger(enabledLater);
    EXPECT_TRUE(enabledLater->shouldBacktrace());

    nova::log::disableBacktrace();
    EXPECT_FALSE(nova::log::defaultLogger()->shouldBacktrace());
    EXPECT_FALSE(existing->shouldBacktrace());
    EXPECT_FALSE(enabledLater->shouldBacktrace());

    auto disabledLater = std::make_shared<nova::log::Logger>("disabled-later");
    disabledLater->enableBacktrace(2);
    nova::log::registerLogger(disabledLater);
    EXPECT_FALSE(disabledLater->shouldBacktrace());
}

TEST_F(RegistryTest, GlobalDumpBacktraceOnlyDumpsTheDefaultLogger)
{
    nova::log::setLevel(nova::log::Level::ERROR);
    nova::log::setPattern("%v");

    auto defaultSink = std::make_shared<ThreadSafeRecordingSink>();
    auto namedSink = std::make_shared<ThreadSafeRecordingSink>();
    auto defaultLogger = std::make_shared<nova::log::Logger>("default-backtrace", defaultSink);
    auto namedLogger = std::make_shared<nova::log::Logger>("named-backtrace", namedSink);
    nova::log::setDefaultLogger(defaultLogger);
    nova::log::registerLogger(namedLogger);
    nova::log::enableBacktrace(4);

    defaultLogger->info("default context");
    namedLogger->info("named context");
    nova::log::dumpBacktrace();

    const auto defaultMessages = defaultSink->messages();
    ASSERT_EQ(defaultMessages.size(), 3U);
    EXPECT_EQ(defaultMessages[1].message, "default context");
    EXPECT_TRUE(namedSink->messages().empty());

    namedLogger->dumpBacktrace();
    const auto namedMessages = namedSink->messages();
    ASSERT_EQ(namedMessages.size(), 3U);
    EXPECT_EQ(namedMessages[1].message, "named context");
}

TEST_F(RegistryTest, ConcurrentBacktraceUpdatesAndRegistrationConvergeToLatestConfig)
{
    constexpr int kLoggerCount = 64;
    constexpr int kConfigurationUpdates = 100;
    std::vector<std::shared_ptr<nova::log::Logger>> loggers;
    loggers.reserve(kLoggerCount);

    std::thread configurationThread([] {
        for (int index = 0; index < kConfigurationUpdates; ++index) {
            if (index % 2 == 0) {
                nova::log::enableBacktrace(4);
            } else {
                nova::log::disableBacktrace();
            }
        }
        nova::log::enableBacktrace(7);
    });
    std::thread registrationThread([&] {
        for (int index = 0; index < kLoggerCount; ++index) {
            auto logger = std::make_shared<nova::log::Logger>("backtrace-" + std::to_string(index));
            nova::log::registerLogger(logger);
            loggers.push_back(std::move(logger));
        }
    });

    configurationThread.join();
    registrationThread.join();

    EXPECT_TRUE(nova::log::defaultLogger()->shouldBacktrace());
    for (const auto &logger : loggers) {
        EXPECT_TRUE(logger->shouldBacktrace());
    }

    auto registeredAfterUpdates = std::make_shared<nova::log::Logger>("backtrace-after-updates");
    nova::log::registerLogger(registeredAfterUpdates);
    EXPECT_TRUE(registeredAfterUpdates->shouldBacktrace());
}

TEST_F(RegistryTest, DropAndShutdownPreserveTheirDocumentedOwnershipRules)
{
    auto current = std::make_shared<nova::log::Logger>("current");
    auto other = std::make_shared<nova::log::Logger>("other");
    nova::log::setDefaultLogger(current);
    nova::log::registerLogger(other);

    nova::log::dropAll();
    EXPECT_EQ(nova::log::defaultLogger(), current);
    EXPECT_EQ(nova::log::getNameLogger("current"), current);
    EXPECT_EQ(nova::log::getNameLogger("other"), nullptr);
    EXPECT_EQ(nova::log::getNameLogger(""), nullptr);

    nova::log::setLevel(nova::log::Level::TRACE);
    nova::log::setFlushLevel(nova::log::Level::WARN);
    nova::log::enableBacktrace(8);
    nova::log::setAutoRegistration(false);
    nova::log::setWorker(std::make_shared<nova::log::async::AsyncLogThreadPool>(8, 1));
    nova::log::shutdown();

    auto resetDefault = nova::log::defaultLogger();
    ASSERT_NE(resetDefault, nullptr);
    EXPECT_NE(resetDefault, current);
    EXPECT_EQ(resetDefault->name(), "");
    EXPECT_EQ(resetDefault->level(), nova::log::Level::INFO);
    EXPECT_EQ(resetDefault->flushLevel(), nova::log::Level::OFF);
    EXPECT_FALSE(resetDefault->shouldBacktrace());
    EXPECT_EQ(nova::log::worker(), nullptr);

    auto afterShutdown = std::make_shared<nova::log::Logger>("after-shutdown");
    nova::log::initLogger(afterShutdown);
    EXPECT_FALSE(afterShutdown->shouldBacktrace());
    EXPECT_EQ(nova::log::getNameLogger("after-shutdown"), afterShutdown);
}

TEST_F(RegistryTest, AsyncFactoryRequiresAConfiguredWorkerBeforeConstructingTheSink)
{
    std::atomic<int> constructionCount{0};

    EXPECT_THROW(nova::log::factories::AsyncFactory::create<ConstructionCountingSink>(
                     "async-factory-without-worker", constructionCount),
                 std::runtime_error);
    EXPECT_EQ(constructionCount.load(), 0);
    EXPECT_EQ(nova::log::getNameLogger("async-factory-without-worker"), nullptr);
}

TEST_F(RegistryTest, AsyncFactoryUsesRegistryWorkerConfigurationAndRegistration)
{
    nova::log::setLevel(nova::log::Level::TRACE);
    nova::log::setPattern("%v");
    nova::log::setWorker(std::make_shared<nova::log::async::AsyncLogThreadPool>(8, 1));

    auto logger = nova::log::factories::AsyncFactory::create<ThreadSafeRecordingSink>("async-factory");
    auto asyncLogger = std::dynamic_pointer_cast<nova::log::async::AsyncLogger>(logger);
    auto sink = std::static_pointer_cast<ThreadSafeRecordingSink>(logger->sinks().front());

    ASSERT_NE(asyncLogger, nullptr);
    EXPECT_EQ(nova::log::getNameLogger("async-factory"), logger);
    EXPECT_EQ(logger->level(), nova::log::Level::TRACE);

    logger->debug("factory {}", "message");
    nova::log::shutdown();

    const auto messages = sink->messages();
    ASSERT_EQ(messages.size(), 1U);
    EXPECT_EQ(messages.front().message, "factory message");
    ASSERT_EQ(sink->formattedMessages().size(), 1U);
    EXPECT_EQ(sink->formattedMessages().front(), "factory message\n");
    EXPECT_EQ(nova::log::worker(), nullptr);
}

TEST_F(RegistryTest, GlobalFunctionsAndMacrosUseTheCurrentDefaultSnapshot)
{
    nova::log::setLevel(nova::log::Level::TRACE);
    nova::log::setPattern("%v");
    auto sink = std::make_shared<ThreadSafeRecordingSink>();
    auto logger = std::make_shared<nova::log::Logger>("global", sink);
    nova::log::setDefaultLogger(logger);

    NOVA_LOG_TRACE("trace {}", 1);
    NOVA_LOG_DEBUG("debug {}", 2);
    NOVA_LOG_INFO("info {}", 3);
    NOVA_LOG_WARN("warn {}", 4);
    NOVA_LOG_ERROR("error {}", 5);
    NOVA_LOG_CRITICAL("critical {}", 6);
    nova::log::info("plain braces {} stay literal");

    const auto messages = sink->messages();
    ASSERT_EQ(messages.size(), 7U);
    EXPECT_EQ(messages[0].message, "trace 1");
    EXPECT_EQ(messages[5].message, "critical 6");
    EXPECT_EQ(messages[6].message, "plain braces {} stay literal");
    EXPECT_TRUE(messages[0].hasSource);
    EXPECT_FALSE(messages[6].hasSource);
}

TEST_F(RegistryTest, RegistryDoesNotHoldItsMutexAcrossCallbacksFlushOrDestruction)
{
    std::atomic<int> callbacks{0};
    nova::log::applyAll([&](std::shared_ptr<nova::log::Logger>) {
        EXPECT_NE(nova::log::defaultLogger(), nullptr);
        ++callbacks;
    });
    EXPECT_GT(callbacks.load(), 0);

    auto flushSink = std::make_shared<ReentrantFlushSink>();
    nova::log::registerLogger(std::make_shared<nova::log::Logger>("reentrant-flush", flushSink));
    nova::log::flushAll();
    EXPECT_TRUE(flushSink->observedDefault.load());
    EXPECT_EQ(flushSink->flushCount.load(), 1);

    std::atomic<bool> destroyed{false};
    auto reentrant = std::make_shared<ReentrantDestructorLogger>("reentrant-destructor", destroyed);
    nova::log::registerLogger(reentrant);
    reentrant.reset();
    nova::log::dropLogger("reentrant-destructor");
    EXPECT_TRUE(destroyed.load());
}

TEST_F(RegistryTest, ShutdownJoinsWorkerWithoutHoldingRegistryMutex)
{
    auto sink = std::make_shared<WorkerLookupFlushSink>();
    auto worker = std::make_shared<nova::log::async::AsyncLogThreadPool>(8, 1);
    nova::log::setWorker(worker);
    auto logger = std::make_shared<nova::log::async::AsyncLogger>("registry-async", sink, worker);
    nova::log::registerLogger(logger);
    logger->info("queued");

    logger.reset();
    worker.reset();
    nova::log::shutdown();

    EXPECT_EQ(sink->logCount.load(), 1);
    EXPECT_GE(sink->flushCount.load(), 1);
    EXPECT_TRUE(sink->observedClearedWorker.load());
}

TEST_F(RegistryTest, ConcurrentDefaultReplacementAndGlobalLoggingKeepEveryCallAlive)
{
    nova::log::setLevel(nova::log::Level::INFO);
    auto firstSink = std::make_shared<ThreadSafeRecordingSink>();
    auto secondSink = std::make_shared<ThreadSafeRecordingSink>();
    auto first = std::make_shared<nova::log::Logger>("concurrent-default", firstSink);
    auto second = std::make_shared<nova::log::Logger>("concurrent-default", secondSink);
    nova::log::setDefaultLogger(first);

    constexpr int kLogThreads = 4;
    constexpr int kLogsPerThread = 1000;
    constexpr int kReplacements = 2000;
    std::vector<std::thread> threads;
    threads.emplace_back([&] {
        for (int i = 0; i < kReplacements; ++i) {
            nova::log::setDefaultLogger((i % 2 == 0) ? first : second);
        }
    });
    for (int threadIndex = 0; threadIndex < kLogThreads; ++threadIndex) {
        threads.emplace_back([] {
            for (int i = 0; i < kLogsPerThread; ++i) {
                NOVA_LOG_INFO("concurrent {}", i);
            }
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }

    const auto totalMessages = firstSink->messages().size() + secondSink->messages().size();
    EXPECT_EQ(totalMessages, static_cast<std::size_t>(kLogThreads * kLogsPerThread));
    EXPECT_NE(nova::log::defaultLogger(), nullptr);
}

TEST_F(RegistryTest, ErrorHandlerCanBeReplacedWhileOtherThreadsReportFailures)
{
    auto logger =
        std::make_shared<nova::log::Logger>("concurrent-errors", std::make_shared<ThrowingLogSink>());
    nova::log::setDefaultLogger(logger);
    std::atomic<int> firstHandlerCount{0};
    std::atomic<int> secondHandlerCount{0};
    nova::log::setErrHandler([&](const std::string &) { ++firstHandlerCount; });

    constexpr int kFailures = 2000;
    std::thread loggerThread([&] {
        for (int i = 0; i < kFailures; ++i) {
            logger->error("failure");
        }
    });
    std::thread configurationThread([&] {
        for (int i = 0; i < kFailures; ++i) {
            if (i % 2 == 0) {
                nova::log::setErrHandler([&](const std::string &) { ++firstHandlerCount; });
            } else {
                nova::log::setErrHandler([&](const std::string &) { ++secondHandlerCount; });
            }
        }
    });

    loggerThread.join();
    configurationThread.join();
    EXPECT_EQ(firstHandlerCount.load() + secondHandlerCount.load(), kFailures);
}

}  // namespace
