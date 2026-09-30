#include <logger/logger.h>
#include <logger/sinks/basic_file_sink.h>
#include <logger/sinks/sink.h>

#include <core/os_utils.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

struct RecordedMessage {
    novainfra::log::Level level;
    std::string text;
};

class RecordingSink NOVAINFRA_FINAL : public novainfra::log::sinks::ISink
{
public:
    void log(const novainfra::log::details::LogMsg &msg) NOVAINFRA_OVERRIDE
    {
        messages.push_back({msg.level, std::string(msg.message)});
    }
    void setPattern(const std::string &) NOVAINFRA_OVERRIDE {}
    void setFormatter(std::unique_ptr<novainfra::log::formatters::IFormatter>) NOVAINFRA_OVERRIDE {}
    void flush() NOVAINFRA_OVERRIDE { ++flushCount; }

    std::vector<RecordedMessage> messages;
    int flushCount{0};
};

TEST(LogMsg, ConstructorPreservesLevel)
{
    const novainfra::log::details::LogMsg message{"test", novainfra::log::Level::ERROR, "failure"};
    EXPECT_EQ(message.level, novainfra::log::Level::ERROR);
}

TEST(LoggerBacktrace, BuffersFilteredMessagesAndDrainsLatestEntries)
{
    auto sink = std::make_shared<RecordingSink>();
    novainfra::log::Logger logger("backtrace", sink);
    logger.setLevel(novainfra::log::Level::ERROR);
    logger.enableBacktrace(2);

    logger.debug("discarded by capacity");
    logger.info("buffered context");
    logger.error("trigger");

    ASSERT_EQ(sink->messages.size(), 1U);
    EXPECT_EQ(sink->messages[0].text, "trigger");
    sink->messages.clear();

    logger.dumpBacktrace();

    ASSERT_EQ(sink->messages.size(), 4U);
    EXPECT_EQ(sink->messages[0].text, "****************** Backtrace Start ******************");
    EXPECT_EQ(sink->messages[1].text, "buffered context");
    EXPECT_EQ(sink->messages[1].level, novainfra::log::Level::INFO);
    EXPECT_EQ(sink->messages[2].text, "trigger");
    EXPECT_EQ(sink->messages[2].level, novainfra::log::Level::ERROR);
    EXPECT_EQ(sink->messages[3].text, "****************** Backtrace End ********************");
    EXPECT_EQ(sink->flushCount, 1);
    EXPECT_EQ(logger.flushLevel(), novainfra::log::Level::OFF);

    sink->messages.clear();
    logger.dumpBacktrace();
    EXPECT_TRUE(sink->messages.empty());
}

TEST(LoggerBacktrace, DumpFlushesFileSinkBeforeReturning)
{
    const auto path = std::filesystem::temp_directory_path() /
                      ("novainfra_backtrace_" + std::to_string(novainfra::core::Pid()) + "_" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".log");
    auto sink = std::make_shared<novainfra::log::sinks::BasicFileSink<std::mutex>>(path.string(), true);
    sink->setPattern("%v");
    novainfra::log::Logger logger("backtrace-file", sink);
    logger.setLevel(novainfra::log::Level::ERROR);
    logger.enableBacktrace(4);

    logger.info("buffered context");
    logger.dumpBacktrace();

    std::ifstream input(path, std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    std::error_code error;
    std::filesystem::remove(path, error);

    EXPECT_NE(contents.find("Backtrace Start"), std::string::npos);
    EXPECT_NE(contents.find("buffered context"), std::string::npos);
    EXPECT_NE(contents.find("Backtrace End"), std::string::npos);
}

TEST(LoggerBacktrace, DisableStopsDumpingAndEnableResetsTheQueue)
{
    auto sink = std::make_shared<RecordingSink>();
    novainfra::log::Logger logger("backtrace", sink);
    logger.setLevel(novainfra::log::Level::ERROR);
    logger.enableBacktrace(4);
    logger.info("old context");
    logger.disableBacktrace();

    logger.dumpBacktrace();
    EXPECT_TRUE(sink->messages.empty());

    logger.enableBacktrace(4);
    logger.info("new context");
    logger.dumpBacktrace();

    ASSERT_EQ(sink->messages.size(), 3U);
    EXPECT_EQ(sink->messages[1].text, "new context");
}

TEST(LoggerBacktrace, ZeroCapacityDisablesAndClearsTheQueue)
{
    auto sink = std::make_shared<RecordingSink>();
    novainfra::log::Logger logger("backtrace", sink);
    logger.setLevel(novainfra::log::Level::ERROR);
    logger.enableBacktrace(2);
    logger.info("old context");

    logger.enableBacktrace(0);
    EXPECT_FALSE(logger.shouldBacktrace());
    logger.dumpBacktrace();
    EXPECT_TRUE(sink->messages.empty());
}

TEST(LoggerBacktrace, CloneKeepsBufferedContext)
{
    auto sink = std::make_shared<RecordingSink>();
    novainfra::log::Logger logger("original", sink);
    logger.setLevel(novainfra::log::Level::ERROR);
    logger.enableBacktrace(4);
    logger.info("context copied into clone");

    auto clone = logger.clone("clone");
    ASSERT_TRUE(clone->shouldBacktrace());
    clone->dumpBacktrace();

    ASSERT_EQ(sink->messages.size(), 3U);
    EXPECT_EQ(sink->messages[1].text, "context copied into clone");
}

}  // namespace
