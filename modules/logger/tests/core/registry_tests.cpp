#include <logger/factories/async_factory.h>
#include <logger/async/async_logger.h>
#include <logger/sinks/base_sink.h>
#include <logger/factories/synchronous_factory.h>
#include <logger/log.h>
#include <logger/formatters/pattern_formatter.h>

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

class ThreadSafeRecordingSink NOVAINFRA_FINAL : public novainfra::log::sinks::BaseSink<std::mutex>
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
    void _log(const novainfra::log::LogMsg &msg) NOVAINFRA_OVERRIDE
    {
        std::string formatted;
        formatter_->format(msg, formatted);
        std::lock_guard<std::mutex> lock(messagesMutex_);
        messages_.push_back({std::string(msg.message), !msg.source.empty()});
        formattedMessages_.push_back(std::move(formatted));
    }

    void _flush() NOVAINFRA_OVERRIDE { ++flushCount_; }

    mutable std::mutex messagesMutex_;
    std::vector<RecordedMessage> messages_;
    std::vector<std::string> formattedMessages_;
    std::atomic<int> flushCount_{0};
};

class ThrowingLogSink NOVAINFRA_FINAL : public novainfra::log::sinks::ISink
{
public:
    void log(const novainfra::log::LogMsg &) NOVAINFRA_OVERRIDE
    {
        throw std::runtime_error("registry sink failure");
    }
    void setPattern(const std::string &) NOVAINFRA_OVERRIDE {}
    void setFormatter(std::unique_ptr<novainfra::log::formatters::IFormatter>) NOVAINFRA_OVERRIDE {}
    void flush() NOVAINFRA_OVERRIDE {}
};

class ConstructionCountingSink NOVAINFRA_FINAL : public novainfra::log::sinks::ISink
{
public:
    explicit ConstructionCountingSink(std::atomic<int> &constructionCount) { ++constructionCount; }

    void log(const novainfra::log::LogMsg &) NOVAINFRA_OVERRIDE {}
    void setPattern(const std::string &) NOVAINFRA_OVERRIDE {}
    void setFormatter(std::unique_ptr<novainfra::log::formatters::IFormatter>) NOVAINFRA_OVERRIDE {}
    void flush() NOVAINFRA_OVERRIDE {}
};

class ReentrantFlushSink NOVAINFRA_FINAL : public novainfra::log::sinks::ISink
{
public:
    void log(const novainfra::log::LogMsg &) NOVAINFRA_OVERRIDE {}
    void setPattern(const std::string &) NOVAINFRA_OVERRIDE {}
    void setFormatter(std::unique_ptr<novainfra::log::formatters::IFormatter>) NOVAINFRA_OVERRIDE {}
    void flush() NOVAINFRA_OVERRIDE
    {
        observedDefault = static_cast<bool>(novainfra::log::defaultLogger());
        ++flushCount;
    }

    std::atomic<bool> observedDefault{false};
    std::atomic<int> flushCount{0};
};

class WorkerLookupFlushSink NOVAINFRA_FINAL : public novainfra::log::sinks::ISink
{
public:
    void log(const novainfra::log::LogMsg &) NOVAINFRA_OVERRIDE { ++logCount; }
    void setPattern(const std::string &) NOVAINFRA_OVERRIDE {}
    void setFormatter(std::unique_ptr<novainfra::log::formatters::IFormatter>) NOVAINFRA_OVERRIDE {}
    void flush() NOVAINFRA_OVERRIDE
    {
        observedClearedWorker = !novainfra::log::worker();
        ++flushCount;
    }

    std::atomic<bool> observedClearedWorker{false};
    std::atomic<int> logCount{0};
    std::atomic<int> flushCount{0};
};

class ReentrantDestructorLogger NOVAINFRA_FINAL : public novainfra::log::Logger
{
public:
    ReentrantDestructorLogger(std::string name, std::atomic<bool> &destroyed)
        : Logger(std::move(name)), destroyed_(destroyed)
    {
    }

    ~ReentrantDestructorLogger() NOVAINFRA_OVERRIDE
    {
        NOVAINFRA_UNUSED(novainfra::log::defaultLogger());
        novainfra::log::setAutoRegistration(true);
        destroyed_ = true;
    }

private:
    std::atomic<bool> &destroyed_;
};

class RegistryTest : public ::testing::Test
{
protected:
    void SetUp() NOVAINFRA_OVERRIDE { novainfra::log::shutdown(); }
    void TearDown() NOVAINFRA_OVERRIDE { novainfra::log::shutdown(); }
};

TEST_F(RegistryTest, DefaultLoggerIsAlwaysAvailableAndCannotBeDropped)
{
    auto initial = novainfra::log::defaultLogger();
    ASSERT_NE(initial, nullptr);
    EXPECT_EQ(initial->name(), "");
    EXPECT_EQ(initial->level(), novainfra::log::Level::INFO);
    EXPECT_EQ(initial->flushLevel(), novainfra::log::Level::OFF);
    EXPECT_EQ(novainfra::log::getNameLogger(""), initial);

    EXPECT_THROW(novainfra::log::setDefaultLogger(nullptr), std::invalid_argument);
    EXPECT_THROW(novainfra::log::dropLogger(""), std::invalid_argument);
    EXPECT_THROW(novainfra::log::registerLogger(nullptr), std::invalid_argument);
    EXPECT_THROW(novainfra::log::registerOrReplaceLogger(nullptr), std::invalid_argument);
    EXPECT_THROW(novainfra::log::initLogger(nullptr), std::invalid_argument);
    EXPECT_THROW(novainfra::log::setFormatter(nullptr), std::invalid_argument);
    EXPECT_THROW(novainfra::log::applyAll({}), std::invalid_argument);

    novainfra::log::dropAll();
    EXPECT_EQ(novainfra::log::defaultLogger(), initial);
    EXPECT_EQ(novainfra::log::getNameLogger(""), initial);
}

TEST_F(RegistryTest, ReplacementKeepsSnapshotsAndOldNamedLoggerAlive)
{
    auto first = std::make_shared<novainfra::log::Logger>("first");
    auto second = std::make_shared<novainfra::log::Logger>("second");
    novainfra::log::setDefaultLogger(first);
    auto snapshot = novainfra::log::defaultLogger();

    novainfra::log::setDefaultLogger(second);

    EXPECT_EQ(snapshot, first);
    EXPECT_EQ(novainfra::log::defaultLogger(), second);
    EXPECT_EQ(novainfra::log::getNameLogger("first"), first);
    EXPECT_EQ(novainfra::log::getNameLogger("second"), second);
}

TEST_F(RegistryTest, ReplacingTheDefaultNameKeepsDefaultAndNamedLookupConsistent)
{
    auto first = std::make_shared<novainfra::log::Logger>("same-name");
    auto replacement = std::make_shared<novainfra::log::Logger>("same-name");
    novainfra::log::setDefaultLogger(first);

    novainfra::log::registerOrReplaceLogger(replacement);

    EXPECT_EQ(novainfra::log::defaultLogger(), replacement);
    EXPECT_EQ(novainfra::log::getNameLogger("same-name"), replacement);
    EXPECT_NE(first, replacement);
}

TEST_F(RegistryTest, GlobalConfigurationAppliesToEveryEntryPath)
{
    auto &registry = novainfra::log::Registry::instance();
    const auto globalLevel = novainfra::log::Level::WARN;
    registry.setLevels(
        {
            {"special", novainfra::log::Level::DEBUG}
    },
        &globalLevel);
    registry.setFlushLevel(novainfra::log::Level::ERROR);
    registry.setFormatter(std::make_unique<novainfra::log::formatters::PatternFormatter>("%v"));
    registry.enableBacktrace(4);

    std::atomic<int> errorCount{0};
    registry.setErrHandler([&](const std::string &) { ++errorCount; });

    auto registeredSink = std::make_shared<ThreadSafeRecordingSink>();
    auto registered = std::make_shared<novainfra::log::Logger>("special", registeredSink);
    registry.registerLogger(registered);

    auto defaultSink = std::make_shared<ThrowingLogSink>();
    auto newDefault = std::make_shared<novainfra::log::Logger>("configured-default", defaultSink);
    registry.setDefaultLogger(newDefault);

    EXPECT_EQ(registered->level(), novainfra::log::Level::DEBUG);
    EXPECT_EQ(registered->flushLevel(), novainfra::log::Level::ERROR);
    EXPECT_TRUE(registered->shouldBacktrace());
    EXPECT_EQ(newDefault->level(), novainfra::log::Level::WARN);
    EXPECT_EQ(newDefault->flushLevel(), novainfra::log::Level::ERROR);
    EXPECT_TRUE(newDefault->shouldBacktrace());

    registered->info("configured");
    newDefault->error("failure");
    ASSERT_EQ(registeredSink->messages().size(), 1U);
    EXPECT_EQ(registeredSink->messages().front().message, "configured");
    ASSERT_EQ(registeredSink->formattedMessages().size(), 1U);
    EXPECT_EQ(registeredSink->formattedMessages().front(), "configured\n");
    EXPECT_EQ(errorCount.load(), 1);

    registry.setAutoRegistration(false);
    auto initialized = std::make_shared<novainfra::log::Logger>("initialized");
    registry.initLogger(initialized);
    EXPECT_EQ(initialized->level(), novainfra::log::Level::WARN);
    EXPECT_TRUE(initialized->shouldBacktrace());
    EXPECT_EQ(registry.getNameLogger("initialized"), nullptr);
}

TEST_F(RegistryTest, GlobalBacktraceControlsExistingAndFutureLoggers)
{
    auto existing = std::make_shared<novainfra::log::Logger>("existing");
    novainfra::log::registerLogger(existing);

    novainfra::log::enableBacktrace(3);
    EXPECT_TRUE(novainfra::log::defaultLogger()->shouldBacktrace());
    EXPECT_TRUE(existing->shouldBacktrace());

    auto enabledLater = std::make_shared<novainfra::log::Logger>("enabled-later");
    novainfra::log::registerLogger(enabledLater);
    EXPECT_TRUE(enabledLater->shouldBacktrace());

    novainfra::log::disableBacktrace();
    EXPECT_FALSE(novainfra::log::defaultLogger()->shouldBacktrace());
    EXPECT_FALSE(existing->shouldBacktrace());
    EXPECT_FALSE(enabledLater->shouldBacktrace());

    auto disabledLater = std::make_shared<novainfra::log::Logger>("disabled-later");
    disabledLater->enableBacktrace(2);
    novainfra::log::registerLogger(disabledLater);
    EXPECT_FALSE(disabledLater->shouldBacktrace());
}

TEST_F(RegistryTest, GlobalDumpBacktraceOnlyDumpsTheDefaultLogger)
{
    novainfra::log::setLevel(novainfra::log::Level::ERROR);
    novainfra::log::setPattern("%v");

    auto defaultSink = std::make_shared<ThreadSafeRecordingSink>();
    auto namedSink = std::make_shared<ThreadSafeRecordingSink>();
    auto defaultLogger = std::make_shared<novainfra::log::Logger>("default-backtrace", defaultSink);
    auto namedLogger = std::make_shared<novainfra::log::Logger>("named-backtrace", namedSink);
    novainfra::log::setDefaultLogger(defaultLogger);
    novainfra::log::registerLogger(namedLogger);
    novainfra::log::enableBacktrace(4);

    defaultLogger->info("default context");
    namedLogger->info("named context");
    novainfra::log::dumpBacktrace();

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
    std::vector<std::shared_ptr<novainfra::log::Logger>> loggers;
    loggers.reserve(kLoggerCount);

    std::thread configurationThread([] {
        for (int index = 0; index < kConfigurationUpdates; ++index) {
            if (index % 2 == 0) {
                novainfra::log::enableBacktrace(4);
            } else {
                novainfra::log::disableBacktrace();
            }
        }
        novainfra::log::enableBacktrace(7);
    });
    std::thread registrationThread([&] {
        for (int index = 0; index < kLoggerCount; ++index) {
            auto logger = std::make_shared<novainfra::log::Logger>("backtrace-" + std::to_string(index));
            novainfra::log::registerLogger(logger);
            loggers.push_back(std::move(logger));
        }
    });

    configurationThread.join();
    registrationThread.join();

    EXPECT_TRUE(novainfra::log::defaultLogger()->shouldBacktrace());
    for (const auto &logger : loggers) {
        EXPECT_TRUE(logger->shouldBacktrace());
    }

    auto registeredAfterUpdates = std::make_shared<novainfra::log::Logger>("backtrace-after-updates");
    novainfra::log::registerLogger(registeredAfterUpdates);
    EXPECT_TRUE(registeredAfterUpdates->shouldBacktrace());
}

TEST_F(RegistryTest, DropAndShutdownPreserveTheirDocumentedOwnershipRules)
{
    auto current = std::make_shared<novainfra::log::Logger>("current");
    auto other = std::make_shared<novainfra::log::Logger>("other");
    novainfra::log::setDefaultLogger(current);
    novainfra::log::registerLogger(other);

    novainfra::log::dropAll();
    EXPECT_EQ(novainfra::log::defaultLogger(), current);
    EXPECT_EQ(novainfra::log::getNameLogger("current"), current);
    EXPECT_EQ(novainfra::log::getNameLogger("other"), nullptr);
    EXPECT_EQ(novainfra::log::getNameLogger(""), nullptr);

    novainfra::log::setLevel(novainfra::log::Level::TRACE);
    novainfra::log::setFlushLevel(novainfra::log::Level::WARN);
    novainfra::log::enableBacktrace(8);
    novainfra::log::setAutoRegistration(false);
    novainfra::log::setWorker(std::make_shared<novainfra::log::async::AsyncLogThreadPool>(8, 1));
    novainfra::log::shutdown();

    auto resetDefault = novainfra::log::defaultLogger();
    ASSERT_NE(resetDefault, nullptr);
    EXPECT_NE(resetDefault, current);
    EXPECT_EQ(resetDefault->name(), "");
    EXPECT_EQ(resetDefault->level(), novainfra::log::Level::INFO);
    EXPECT_EQ(resetDefault->flushLevel(), novainfra::log::Level::OFF);
    EXPECT_FALSE(resetDefault->shouldBacktrace());
    EXPECT_EQ(novainfra::log::worker(), nullptr);

    auto afterShutdown = std::make_shared<novainfra::log::Logger>("after-shutdown");
    novainfra::log::initLogger(afterShutdown);
    EXPECT_FALSE(afterShutdown->shouldBacktrace());
    EXPECT_EQ(novainfra::log::getNameLogger("after-shutdown"), afterShutdown);
}

TEST_F(RegistryTest, AsyncFactoryRequiresAConfiguredWorkerBeforeConstructingTheSink)
{
    std::atomic<int> constructionCount{0};

    EXPECT_THROW(novainfra::log::factories::AsyncFactory::create<ConstructionCountingSink>(
                     "async-factory-without-worker", constructionCount),
                 std::runtime_error);
    EXPECT_EQ(constructionCount.load(), 0);
    EXPECT_EQ(novainfra::log::getNameLogger("async-factory-without-worker"), nullptr);
}

TEST_F(RegistryTest, AsyncFactoryUsesRegistryWorkerConfigurationAndRegistration)
{
    novainfra::log::setLevel(novainfra::log::Level::TRACE);
    novainfra::log::setPattern("%v");
    novainfra::log::setWorker(std::make_shared<novainfra::log::async::AsyncLogThreadPool>(8, 1));

    auto logger = novainfra::log::factories::AsyncFactory::create<ThreadSafeRecordingSink>("async-factory");
    auto asyncLogger = std::dynamic_pointer_cast<novainfra::log::async::AsyncLogger>(logger);
    auto sink = std::static_pointer_cast<ThreadSafeRecordingSink>(logger->sinks().front());

    ASSERT_NE(asyncLogger, nullptr);
    EXPECT_EQ(novainfra::log::getNameLogger("async-factory"), logger);
    EXPECT_EQ(logger->level(), novainfra::log::Level::TRACE);

    logger->debug("factory {}", "message");
    novainfra::log::shutdown();

    const auto messages = sink->messages();
    ASSERT_EQ(messages.size(), 1U);
    EXPECT_EQ(messages.front().message, "factory message");
    ASSERT_EQ(sink->formattedMessages().size(), 1U);
    EXPECT_EQ(sink->formattedMessages().front(), "factory message\n");
    EXPECT_EQ(novainfra::log::worker(), nullptr);
}

TEST_F(RegistryTest, GlobalFunctionsAndMacrosUseTheCurrentDefaultSnapshot)
{
    novainfra::log::setLevel(novainfra::log::Level::TRACE);
    novainfra::log::setPattern("%v");
    auto sink = std::make_shared<ThreadSafeRecordingSink>();
    auto logger = std::make_shared<novainfra::log::Logger>("global", sink);
    novainfra::log::setDefaultLogger(logger);

    NOVAINFRA_LOG_TRACE("trace {}", 1);
    NOVAINFRA_LOG_DEBUG("debug {}", 2);
    NOVAINFRA_LOG_INFO("info {}", 3);
    NOVAINFRA_LOG_WARN("warn {}", 4);
    NOVAINFRA_LOG_ERROR("error {}", 5);
    NOVAINFRA_LOG_CRITICAL("critical {}", 6);
    novainfra::log::info("plain braces {} stay literal");

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
    novainfra::log::applyAll([&](std::shared_ptr<novainfra::log::Logger>) {
        EXPECT_NE(novainfra::log::defaultLogger(), nullptr);
        ++callbacks;
    });
    EXPECT_GT(callbacks.load(), 0);

    auto flushSink = std::make_shared<ReentrantFlushSink>();
    novainfra::log::registerLogger(std::make_shared<novainfra::log::Logger>("reentrant-flush", flushSink));
    novainfra::log::flushAll();
    EXPECT_TRUE(flushSink->observedDefault.load());
    EXPECT_EQ(flushSink->flushCount.load(), 1);

    std::atomic<bool> destroyed{false};
    auto reentrant = std::make_shared<ReentrantDestructorLogger>("reentrant-destructor", destroyed);
    novainfra::log::registerLogger(reentrant);
    reentrant.reset();
    novainfra::log::dropLogger("reentrant-destructor");
    EXPECT_TRUE(destroyed.load());
}

TEST_F(RegistryTest, ShutdownJoinsWorkerWithoutHoldingRegistryMutex)
{
    auto sink = std::make_shared<WorkerLookupFlushSink>();
    auto worker = std::make_shared<novainfra::log::async::AsyncLogThreadPool>(8, 1);
    novainfra::log::setWorker(worker);
    auto logger = std::make_shared<novainfra::log::async::AsyncLogger>("registry-async", sink, worker);
    novainfra::log::registerLogger(logger);
    logger->info("queued");

    logger.reset();
    worker.reset();
    novainfra::log::shutdown();

    EXPECT_EQ(sink->logCount.load(), 1);
    EXPECT_GE(sink->flushCount.load(), 1);
    EXPECT_TRUE(sink->observedClearedWorker.load());
}

TEST_F(RegistryTest, ConcurrentDefaultReplacementAndGlobalLoggingKeepEveryCallAlive)
{
    novainfra::log::setLevel(novainfra::log::Level::INFO);
    auto firstSink = std::make_shared<ThreadSafeRecordingSink>();
    auto secondSink = std::make_shared<ThreadSafeRecordingSink>();
    auto first = std::make_shared<novainfra::log::Logger>("concurrent-default", firstSink);
    auto second = std::make_shared<novainfra::log::Logger>("concurrent-default", secondSink);
    novainfra::log::setDefaultLogger(first);

    constexpr int kLogThreads = 4;
    constexpr int kLogsPerThread = 1000;
    constexpr int kReplacements = 2000;
    std::vector<std::thread> threads;
    threads.emplace_back([&] {
        for (int i = 0; i < kReplacements; ++i) {
            novainfra::log::setDefaultLogger((i % 2 == 0) ? first : second);
        }
    });
    for (int threadIndex = 0; threadIndex < kLogThreads; ++threadIndex) {
        threads.emplace_back([] {
            for (int i = 0; i < kLogsPerThread; ++i) {
                NOVAINFRA_LOG_INFO("concurrent {}", i);
            }
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }

    const auto totalMessages = firstSink->messages().size() + secondSink->messages().size();
    EXPECT_EQ(totalMessages, static_cast<std::size_t>(kLogThreads * kLogsPerThread));
    EXPECT_NE(novainfra::log::defaultLogger(), nullptr);
}

TEST_F(RegistryTest, ErrorHandlerCanBeReplacedWhileOtherThreadsReportFailures)
{
    auto logger =
        std::make_shared<novainfra::log::Logger>("concurrent-errors", std::make_shared<ThrowingLogSink>());
    novainfra::log::setDefaultLogger(logger);
    std::atomic<int> firstHandlerCount{0};
    std::atomic<int> secondHandlerCount{0};
    novainfra::log::setErrHandler([&](const std::string &) { ++firstHandlerCount; });

    constexpr int kFailures = 2000;
    std::thread loggerThread([&] {
        for (int i = 0; i < kFailures; ++i) {
            logger->error("failure");
        }
    });
    std::thread configurationThread([&] {
        for (int i = 0; i < kFailures; ++i) {
            if (i % 2 == 0) {
                novainfra::log::setErrHandler([&](const std::string &) { ++firstHandlerCount; });
            } else {
                novainfra::log::setErrHandler([&](const std::string &) { ++secondHandlerCount; });
            }
        }
    });

    loggerThread.join();
    configurationThread.join();
    EXPECT_EQ(firstHandlerCount.load() + secondHandlerCount.load(), kFailures);
}


TEST_F(RegistryTest, RetiredLoggerDestructorsMayReenterWriteOperations)
{
    for (int operation = 0; operation < 5; ++operation) {
        SCOPED_TRACE(operation);
        novainfra::log::shutdown();
        std::atomic<bool> destroyed{false};
        auto logger = std::make_shared<ReentrantDestructorLogger>("retired", destroyed);
        if (operation == 2)
            novainfra::log::setDefaultLogger(logger);
        else
            novainfra::log::registerLogger(logger);
        logger.reset();
        switch (operation) {
            case 0:
                novainfra::log::dropLogger("retired");
                break;
            case 1:
                novainfra::log::registerOrReplaceLogger(std::make_shared<novainfra::log::Logger>("retired"));
                break;
            case 2:
                novainfra::log::setDefaultLogger(std::make_shared<novainfra::log::Logger>("retired"));
                break;
            case 3:
                novainfra::log::dropAll();
                break;
            case 4:
                novainfra::log::shutdown();
                break;
        }
        EXPECT_TRUE(destroyed.load());
    }
}

TEST_F(RegistryTest, RetiredErrorHandlerCaptureMayReenterWriteOperations)
{
    struct ReentrantCapture {
        explicit ReentrantCapture(bool &destroyed) : destroyed(destroyed) {}
        ~ReentrantCapture()
        {
            novainfra::log::setAutoRegistration(true);
            destroyed = true;
        }
        bool &destroyed;
    };
    bool destroyed = false;
    auto capture = std::make_shared<ReentrantCapture>(destroyed);
    novainfra::log::setErrHandler([capture](const std::string &) {});
    capture.reset();
    novainfra::log::setErrHandler({});
    EXPECT_TRUE(destroyed);
}


TEST_F(RegistryTest, RetiredRegistryFormatterMayReenterWriteOperations)
{
    class ReentrantFormatter final : public novainfra::log::formatters::IFormatter
    {
    public:
        explicit ReentrantFormatter(bool &destroyed) : destroyed_(destroyed) {}
        ~ReentrantFormatter() override
        {
            novainfra::log::setAutoRegistration(true);
            destroyed_ = true;
        }
        void format(const novainfra::log::details::LogMsg &, std::string &) override {}
        std::unique_ptr<IFormatter> clone() const override
        {
            return std::make_unique<novainfra::log::formatters::PatternFormatter>();
        }

    private:
        bool &destroyed_;
    };
    bool destroyed = false;
    novainfra::log::setFormatter(std::make_unique<ReentrantFormatter>(destroyed));
    novainfra::log::setFormatter(std::make_unique<novainfra::log::formatters::PatternFormatter>());
    EXPECT_TRUE(destroyed);
}

}  // namespace
