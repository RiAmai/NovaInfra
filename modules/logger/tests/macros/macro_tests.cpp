#include <logger/logger.h>
#include <logger/macros.h>
#include <logger/log.h>
#include <gtest/gtest.h>

#include <memory>

namespace {

class MacroSourceSink NOVAINFRA_FINAL : public novainfra::log::sinks::ISink
{
public:
    void log(const novainfra::log::LogMsg &msg) NOVAINFRA_OVERRIDE
    {
        source = msg.source;
        message = msg.message;
    }
    void setPattern(const std::string &) NOVAINFRA_OVERRIDE {}
    void setFormatter(std::unique_ptr<novainfra::log::formatters::IFormatter>) NOVAINFRA_OVERRIDE {}
    void flush() NOVAINFRA_OVERRIDE {}

    novainfra::log::SourceLocation source{};
    std::string message;
};

TEST(LoggerMacros, CompileWithoutImportingLoggerNamespace)
{
    auto logger = std::make_shared<novainfra::log::Logger>("macro");

    NOVAINFRA_LOGGER_INFO(logger, "message");
}

TEST(LoggerMacros, AcceptPointerLikeLoggerTypes)
{
    novainfra::log::Logger stackLogger("stack");
    auto uniqueLogger = std::make_unique<novainfra::log::Logger>("unique");
    auto sharedLogger = std::make_shared<novainfra::log::Logger>("shared");

    NOVAINFRA_LOGGER_INFO(&stackLogger, "raw");
    NOVAINFRA_LOGGER_INFO(uniqueLogger, "unique");
    NOVAINFRA_LOGGER_INFO(sharedLogger, "shared");
}

TEST(LoggerMacros, CapturesSourceLocation)
{
    auto sink = std::make_shared<MacroSourceSink>();
    auto logger = std::make_shared<novainfra::log::Logger>("source", sink);
    const int expectedLine = __LINE__ + 1;
    NOVAINFRA_LOGGER_INFO(logger, "message");

    EXPECT_NE(sink->source.file.find("macro_tests.cpp"), std::string_view::npos);
    EXPECT_EQ(sink->source.line, expectedLine);
    EXPECT_FALSE(sink->source.function.empty());
    EXPECT_TRUE(sink->source.hasStaticStorage());
}

TEST(LoggerMacros, ForwardsFormatArguments)
{
    auto sink = std::make_shared<MacroSourceSink>();
    auto logger = std::make_shared<novainfra::log::Logger>("format", sink);

    NOVAINFRA_LOGGER_INFO(logger, "value {:04d}", 7);

    EXPECT_EQ(sink->message, "value 0007");
}

}  // namespace
