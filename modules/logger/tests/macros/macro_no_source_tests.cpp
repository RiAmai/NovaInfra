#include <logger/logger.h>
#include <logger/macros.h>
#include <logger/novalog.h>

#include <gtest/gtest.h>

#include <memory>

namespace {

class SourceSink final : public nova::log::sinks::ISink
{
public:
    void log(const nova::log::LogMsg &msg) override { source = msg.source; }
    void setPattern(const std::string &) override {}
    void setFormatter(std::unique_ptr<nova::log::formatters::IFormatter>) override {}
    void flush() override {}

    nova::log::SourceLocation source{};
};

TEST(LoggerMacros, NoSourceLocationPassesEmptySource)
{
    auto sink = std::make_shared<SourceSink>();
    auto logger = std::make_shared<nova::log::Logger>("no-source", sink);

    NOVA_LOGGER_INFO(logger, "message");

    EXPECT_TRUE(sink->source.file.empty());
    EXPECT_EQ(sink->source.line, 0);
    EXPECT_TRUE(sink->source.function.empty());
}

TEST(LoggerMacros, GlobalMacroHonorsNoSourceLocation)
{
    auto sink = std::make_shared<SourceSink>();
    auto logger = std::make_shared<nova::log::Logger>("global-no-source", sink);
    nova::log::setDefaultLogger(logger);

    NOVA_LOG_INFO("message");

    EXPECT_TRUE(sink->source.file.empty());
    EXPECT_EQ(sink->source.line, 0);
    EXPECT_TRUE(sink->source.function.empty());
    nova::log::shutdown();
}

}  // namespace
