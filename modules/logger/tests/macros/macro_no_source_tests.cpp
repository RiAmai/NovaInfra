#include <logger/logger.h>
#include <logger/macros.h>
#include <logger/log.h>

#include <gtest/gtest.h>

#include <memory>

namespace {

class SourceSink NOVAINFRA_FINAL : public novainfra::log::sinks::ISink
{
public:
    void log(const novainfra::log::LogMsg &msg) NOVAINFRA_OVERRIDE { source = msg.source; }
    void setPattern(const std::string &) NOVAINFRA_OVERRIDE {}
    void setFormatter(std::unique_ptr<novainfra::log::formatters::IFormatter>) NOVAINFRA_OVERRIDE {}
    void flush() NOVAINFRA_OVERRIDE {}

    novainfra::log::SourceLocation source{};
};

TEST(LoggerMacros, NoSourceLocationPassesEmptySource)
{
    auto sink = std::make_shared<SourceSink>();
    auto logger = std::make_shared<novainfra::log::Logger>("no-source", sink);

    NOVAINFRA_LOGGER_INFO(logger, "message");

    EXPECT_TRUE(sink->source.file.empty());
    EXPECT_EQ(sink->source.line, 0);
    EXPECT_TRUE(sink->source.function.empty());
}

TEST(LoggerMacros, GlobalMacroHonorsNoSourceLocation)
{
    auto sink = std::make_shared<SourceSink>();
    auto logger = std::make_shared<novainfra::log::Logger>("global-no-source", sink);
    novainfra::log::setDefaultLogger(logger);

    NOVAINFRA_LOG_INFO("message");

    EXPECT_TRUE(sink->source.file.empty());
    EXPECT_EQ(sink->source.line, 0);
    EXPECT_TRUE(sink->source.function.empty());
    novainfra::log::shutdown();
}

}  // namespace
