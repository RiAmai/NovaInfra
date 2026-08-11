#include <logger/sinks/base_sink.h>
#include <logger/sinks/console_sink.h>
#include <logger/logger.h>
#include <logger/sinks/sink.h>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

struct CountingFormattedValue {
    int value;
    std::atomic<int> *formatCount;
};

template <>
struct fmt::formatter<CountingFormattedValue> : fmt::formatter<int> {
    template <typename FormatContext>
    auto format(const CountingFormattedValue &value, FormatContext &ctx) const
    {
        ++(*value.formatCount);
        return fmt::formatter<int>::format(value.value, ctx);
    }
};

namespace {

static_assert(!std::is_copy_assignable<nova::log::Logger>::value, "Logger assignment must stay disabled");
static_assert(!std::is_move_assignable<nova::log::Logger>::value, "Logger assignment must stay disabled");
static_assert(std::is_same<decltype(std::declval<nova::log::Logger &>().sinks()),
                           const std::vector<nova::log::sinks::SinkPtr> &>::value,
              "Logger sinks must be immutable after construction");

class RecordingSink final : public nova::log::sinks::ISink
{
public:
    void log(const nova::log::LogMsg &msg) override
    {
        const auto assignView = [](std::string &destination, std::string_view source) {
            destination.clear();
            if (!source.empty()) {
                destination.assign(source.data(), source.size());
            }
        };
        ++logCount;
        assignView(lastLoggerName, msg.loggerName);
        assignView(lastMessageText, msg.message);
        assignView(lastSourceFile, msg.source.file);
        assignView(lastSourceFunction, msg.source.function);
        lastMessage = msg;
        lastMessage.loggerName = lastLoggerName;
        lastMessage.message = lastMessageText;
        lastMessage.source = nova::log::SourceLocation{lastSourceFile, msg.source.line, lastSourceFunction};
        messages.emplace_back(msg.message);
        levels.push_back(msg.level);
    }

    void setPattern(const std::string &) override {}
    void setFormatter(std::unique_ptr<nova::log::formatters::IFormatter>) override {}
    void flush() override { ++flushCount; }

    int logCount = 0;
    int flushCount = 0;
    nova::log::LogMsg lastMessage{};
    std::string lastLoggerName;
    std::string lastMessageText;
    std::string lastSourceFile;
    std::string lastSourceFunction;
    std::vector<std::string> messages;
    std::vector<nova::log::Level> levels;
};

class ConcreteBaseSink final : public nova::log::sinks::BaseSink<std::mutex>
{
public:
    using BaseSink::BaseSink;

private:
    void _log(const nova::log::LogMsg &) override {}
    void _flush() override {}
};

class ThrowingSink final : public nova::log::sinks::ISink
{
public:
    enum class Failure {
        STANDARD,
        UNKNOWN,
    };

    explicit ThrowingSink(Failure failure) : failure_(failure) {}

    void log(const nova::log::LogMsg &) override
    {
        if (failure_ == Failure::STANDARD) {
            throw std::runtime_error("sink failure");
        }
        throw 42;
    }

    void setPattern(const std::string &) override {}
    void setFormatter(std::unique_ptr<nova::log::formatters::IFormatter>) override {}
    void flush() override {}

private:
    Failure failure_;
};

class ThreadSafeCountingSink final : public nova::log::sinks::BaseSink<std::mutex>
{
public:
    int count() const { return count_.load(); }

private:
    void _log(const nova::log::LogMsg &) override { ++count_; }
    void _flush() override {}

    std::atomic<int> count_{0};
};

TEST(ExceptionMacros, CatchBodiesHandleStandardAndUnknownExceptions)
{
    bool standardCaught = false;
    bool unknownCaught = false;

    NOVA_TRY
    {
        throw std::runtime_error("standard failure");
    }
    NOVA_CATCH_STD_BODY({
        standardCaught = std::string(ex.what()) == "standard failure";
    })
    NOVA_CATCH_ALL_BODY({
        unknownCaught = true;
    })

    EXPECT_TRUE(standardCaught);
    EXPECT_FALSE(unknownCaught);

    standardCaught = false;
    unknownCaught = false;
    NOVA_TRY
    {
        throw 7;
    }
    NOVA_CATCH_STD_BODY({
        standardCaught = true;
    })
    NOVA_CATCH_ALL_BODY({
        unknownCaught = true;
    })

    EXPECT_FALSE(standardCaught);
    EXPECT_TRUE(unknownCaught);
}

TEST(LoggerBaseline, SinkDefaultsToTrace)
{
    RecordingSink sink;

    EXPECT_EQ(sink.level(), nova::log::Level::TRACE);
    EXPECT_TRUE(sink.shouldLog(nova::log::Level::TRACE));
}

TEST(LoggerBaseline, SinkCanCopyTransientMessageViews)
{
    auto sink = std::make_shared<RecordingSink>();
    nova::log::Logger logger("baseline", sink);

    logger.info("message");

    ASSERT_EQ(sink->logCount, 1);
    EXPECT_EQ(sink->lastMessage.loggerName, "baseline");
    EXPECT_EQ(sink->lastMessage.message, "message");
}

TEST(SourceLocation, DistinguishesStaticAndBorrowedStorage)
{
    constexpr auto staticSource = nova::log::SourceLocation::fromStatic("static.cpp", 7, "function");
    const nova::log::SourceLocation borrowedSource{"borrowed.cpp", 8, "function"};

    EXPECT_TRUE(staticSource.hasStaticStorage());
    EXPECT_FALSE(borrowedSource.hasStaticStorage());
}

TEST(LoggerBaseline, CapturesConfiguredNumericThreadId)
{
    auto sink = std::make_shared<RecordingSink>();
    nova::log::Logger logger("thread-id", sink);

    logger.info("message");

#ifdef NOVA_NO_THREAD_ID
    EXPECT_EQ(sink->lastMessage.threadId, 0U);
#else
    EXPECT_EQ(sink->lastMessage.threadId, nova::core::ThreadId());
#endif
}

TEST(LoggerFormatting, FormatsArgumentsForAllLevelMethods)
{
    auto sink = std::make_shared<RecordingSink>();
    nova::log::Logger logger("format", sink);

    logger.trace("trace {}", 1);
    logger.debug("debug {:04d}", 2);
    logger.info("info {:.2f}", 3.5);
    logger.warn("warn {}", std::string("four"));
    logger.error("error {}", 5);
    logger.critical("critical {}", 6);

    EXPECT_EQ(sink->messages, (std::vector<std::string>{"trace 1", "debug 0002", "info 3.50", "warn four",
                                                        "error 5", "critical 6"}));
    EXPECT_EQ(sink->levels,
              (std::vector<nova::log::Level>{nova::log::Level::TRACE, nova::log::Level::DEBUG,
                                             nova::log::Level::INFO, nova::log::Level::WARN,
                                             nova::log::Level::ERROR, nova::log::Level::CRITICAL}));
}

TEST(LoggerFormatting, GenericLogAndDynamicFormatsAreSupported)
{
    auto sink = std::make_shared<RecordingSink>();
    nova::log::Logger logger("format", sink);
    std::string dynamicFormat = "dynamic {}";

    logger.log(nova::log::SourceLocation{"file.cpp", 12, "function"}, nova::log::Level::INFO, "generic {}",
               1);
    logger.info(dynamicFormat, 2);
    logger.info(fmt::runtime(dynamicFormat), 3);

    EXPECT_EQ(sink->messages, (std::vector<std::string>{"generic 1", "dynamic 2", "dynamic 3"}));
    EXPECT_EQ(sink->lastMessage.source.line, 0);
}

TEST(LoggerFormatting, SingleMessageKeepsBracesLiteral)
{
    auto sink = std::make_shared<RecordingSink>();
    nova::log::Logger logger("format", sink);
    std::string message = "literal {} braces";

    logger.info(message);

    EXPECT_EQ(sink->lastMessage.message, message);
}

TEST(LoggerFormatting, PreservesLongAndEmbeddedNullArguments)
{
    auto sink = std::make_shared<RecordingSink>();
    nova::log::Logger logger("format", sink);
    const std::string longMessage(1024, 'x');
    const std::string embeddedNull("left\0right", 10);

    logger.info("{}", longMessage);
    EXPECT_EQ(sink->lastMessage.message, longMessage);

    logger.info("{}", embeddedNull);
    EXPECT_EQ(sink->lastMessage.message, embeddedNull);
}

TEST(LoggerFormatting, InvalidRuntimeFormatIsReportedWithoutCallingSink)
{
    auto sink = std::make_shared<RecordingSink>();
    nova::log::Logger logger("format", sink);
    int errorCount = 0;
    std::string errorMessage;
    logger.setErrHandler([&](const std::string &message) {
        ++errorCount;
        errorMessage = message;
    });

    logger.info("{:d}", "text");

    EXPECT_EQ(sink->logCount, 0);
    EXPECT_EQ(errorCount, 1);
    EXPECT_NE(errorMessage.find("invalid format specifier"), std::string::npos);
}

TEST(LoggerFormatting, LoggerLevelFilteringSkipsUserFormatter)
{
    auto sink = std::make_shared<RecordingSink>();
    nova::log::Logger logger("format", sink);
    logger.setLevel(nova::log::Level::ERROR);
    std::atomic<int> formatCount{0};

    logger.info("{}", CountingFormattedValue{7, &formatCount});

    EXPECT_EQ(formatCount.load(), 0);
    EXPECT_EQ(sink->logCount, 0);
}

TEST(LoggerFlush, DoesNotFlushAutomaticallyByDefault)
{
    auto sink = std::make_shared<RecordingSink>();
    nova::log::Logger logger("flush", sink);

    logger.info("message");

    EXPECT_EQ(sink->logCount, 1);
    EXPECT_EQ(sink->flushCount, 0);
}

TEST(PatternFormatter, AcceptsSupportedPatternsAndRejectsUnknownFlags)
{
    EXPECT_NO_THROW(nova::log::formatters::PatternFormatter("%v"));
    EXPECT_THROW(nova::log::formatters::PatternFormatter("%q"), std::invalid_argument);

    nova::log::formatters::PatternFormatter formatter;
    EXPECT_NO_THROW(formatter.setPattern("%n"));
    EXPECT_THROW(formatter.setPattern("%q"), std::invalid_argument);
}

TEST(LoggerSinks, EmptySinkListIsSafe)
{
    nova::log::Logger logger("empty");

    EXPECT_NO_THROW(logger.info("message"));
    EXPECT_NO_THROW(logger.flush());
}

TEST(LoggerSinks, RejectsNullSinkAtConstruction)
{
    EXPECT_THROW(nova::log::Logger("null", nova::log::sinks::SinkPtr{}), std::invalid_argument);
}

TEST(BaseSink, RejectsNullFormatter)
{
    nova::log::sinks::ConsoleOutSink<std::mutex> sink;

    EXPECT_THROW(sink.setFormatter(nullptr), std::invalid_argument);
}

TEST(LoggerSinks, RejectsNullFormatterEvenWithoutSinks)
{
    nova::log::Logger logger("empty");

    EXPECT_THROW(logger.setFormatter(nullptr), std::invalid_argument);
}

TEST(BaseSink, RejectsNullFormatterAtConstruction)
{
    EXPECT_THROW(ConcreteBaseSink(nullptr), std::invalid_argument);
}

TEST(LoggerFiltering, AppliesLoggerAndSinkThresholds)
{
    auto sink = std::make_shared<RecordingSink>();
    sink->setLevel(nova::log::Level::ERROR);
    nova::log::Logger logger("filter", sink);
    logger.setLevel(nova::log::Level::WARN);

    logger.info("logger filtered");
    logger.warn("sink filtered");
    logger.error("accepted");
    logger.log({}, nova::log::Level::OFF, "off");

    EXPECT_EQ(sink->logCount, 1);
    EXPECT_EQ(sink->lastMessage.message, "accepted");
}

TEST(LoggerFlush, FlushesAtConfiguredLevelAndOnExplicitRequest)
{
    auto sink = std::make_shared<RecordingSink>();
    nova::log::Logger logger("flush", sink);
    logger.setFlushLevel(nova::log::Level::ERROR);

    logger.info("no automatic flush");
    EXPECT_EQ(sink->flushCount, 0);

    logger.error("automatic flush");
    EXPECT_EQ(sink->flushCount, 1);

    logger.flush();
    EXPECT_EQ(sink->flushCount, 2);
}

TEST(LoggerClone, PreservesConfigurationAndSharesSinks)
{
    auto sink = std::make_shared<RecordingSink>();
    nova::log::Logger logger("original", sink);
    logger.setLevel(nova::log::Level::WARN);
    logger.setFlushLevel(nova::log::Level::ERROR);

    auto clone = logger.clone("clone");

    EXPECT_EQ(clone->name(), "clone");
    EXPECT_EQ(clone->level(), nova::log::Level::WARN);
    EXPECT_EQ(clone->flushLevel(), nova::log::Level::ERROR);
    ASSERT_EQ(clone->sinks().size(), 1U);
    EXPECT_EQ(clone->sinks().front(), sink);
}

TEST(LoggerClone, CopiesCustomErrorHandler)
{
    auto sink = std::make_shared<ThrowingSink>(ThrowingSink::Failure::STANDARD);
    nova::log::Logger logger("original", sink);
    int errorCount = 0;
    logger.setErrHandler([&errorCount](const std::string &) { ++errorCount; });
    auto clone = logger.clone("clone");

    clone->info("message");

    EXPECT_EQ(errorCount, 1);
}

TEST(LoggerErrors, StandardSinkExceptionIsReportedAndSwallowed)
{
    auto sink = std::make_shared<ThrowingSink>(ThrowingSink::Failure::STANDARD);
    nova::log::Logger logger("errors", sink);
    int errorCount = 0;
    std::string errorMessage;
    logger.setErrHandler([&](const std::string &message) {
        ++errorCount;
        errorMessage = message;
    });

    EXPECT_NO_THROW(logger.info("message"));
    EXPECT_EQ(errorCount, 1);
    EXPECT_NE(errorMessage.find("sink failure"), std::string::npos);
}

TEST(LoggerErrors, ThrowingErrorHandlerIsContained)
{
    auto sink = std::make_shared<ThrowingSink>(ThrowingSink::Failure::STANDARD);
    nova::log::Logger logger("errors", sink);
    logger.setErrHandler([](const std::string &) { throw std::runtime_error("handler failure"); });

    EXPECT_NO_THROW(logger.info("message"));
}

TEST(LoggerErrors, UnknownSinkExceptionIsReportedAndRethrown)
{
    auto sink = std::make_shared<ThrowingSink>(ThrowingSink::Failure::UNKNOWN);
    nova::log::Logger logger("errors", sink);
    int errorCount = 0;
    logger.setErrHandler([&](const std::string &) { ++errorCount; });

    EXPECT_THROW(logger.info("message"), int);
    EXPECT_GE(errorCount, 1);
}

TEST(LoggerSource, MemberConvenienceFunctionsUseEmptySourceLocation)
{
    auto sink = std::make_shared<RecordingSink>();
    nova::log::Logger logger("source", sink);

    logger.info("message");

    EXPECT_TRUE(sink->lastMessage.source.file.empty());
    EXPECT_EQ(sink->lastMessage.source.line, 0);
    EXPECT_TRUE(sink->lastMessage.source.function.empty());
}

TEST(LoggerConcurrency, MultipleThreadsCanLogToTheSameSynchronizedSink)
{
    constexpr int kThreadCount = 8;
    constexpr int kMessagesPerThread = 2000;
    auto sink = std::make_shared<ThreadSafeCountingSink>();
    nova::log::Logger logger("concurrent", sink);
    std::vector<std::thread> threads;

    for (int threadIndex = 0; threadIndex < kThreadCount; ++threadIndex) {
        threads.emplace_back([&logger]() {
            for (int messageIndex = 0; messageIndex < kMessagesPerThread; ++messageIndex) {
                logger.info("message");
            }
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }

    EXPECT_EQ(sink->count(), kThreadCount * kMessagesPerThread);
}

TEST(LoggerConcurrency, LevelCanChangeWhileOtherThreadsLog)
{
    constexpr int kIterations = 10000;
    auto sink = std::make_shared<ThreadSafeCountingSink>();
    nova::log::Logger logger("level", sink);

    std::thread writer([&logger]() {
        for (int iteration = 0; iteration < kIterations; ++iteration) {
            logger.info("message");
        }
    });
    std::thread configurator([&logger]() {
        for (int iteration = 0; iteration < kIterations; ++iteration) {
            logger.setLevel((iteration % 2) == 0 ? nova::log::Level::TRACE : nova::log::Level::ERROR);
        }
    });

    writer.join();
    configurator.join();

    SUCCEED();
}

}  // namespace
