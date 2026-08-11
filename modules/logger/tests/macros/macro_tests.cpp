#include <logger/logger.h>
#include <logger/macros.h>
#include <logger/novalog.h>
#include <gtest/gtest.h>

#include <memory>

namespace {

class MacroSourceSink final : public nova::log::sinks::ISink
{
public:
    void log(const nova::log::LogMsg &msg) override
    {
        source = msg.source;
        message = msg.message;
    }
    void setPattern(const std::string &) override {}
    void setFormatter(std::unique_ptr<nova::log::formatters::IFormatter>) override {}
    void flush() override {}

    nova::log::SourceLocation source{};
    std::string message;
};

TEST(LoggerMacros, CompileWithoutImportingLoggerNamespace)
{
    auto logger = std::make_shared<nova::log::Logger>("macro");

    NOVA_LOGGER_INFO(logger, "message");
}

TEST(LoggerMacros, AcceptPointerLikeLoggerTypes)
{
    nova::log::Logger stackLogger("stack");
    auto uniqueLogger = std::make_unique<nova::log::Logger>("unique");
    auto sharedLogger = std::make_shared<nova::log::Logger>("shared");

    NOVA_LOGGER_INFO(&stackLogger, "raw");
    NOVA_LOGGER_INFO(uniqueLogger, "unique");
    NOVA_LOGGER_INFO(sharedLogger, "shared");
}

TEST(LoggerMacros, CapturesSourceLocation)
{
    auto sink = std::make_shared<MacroSourceSink>();
    auto logger = std::make_shared<nova::log::Logger>("source", sink);
    const int expectedLine = __LINE__ + 1;
    NOVA_LOGGER_INFO(logger, "message");

    EXPECT_NE(sink->source.file.find("macro_tests.cpp"), std::string_view::npos);
    EXPECT_EQ(sink->source.line, expectedLine);
    EXPECT_FALSE(sink->source.function.empty());
    EXPECT_TRUE(sink->source.hasStaticStorage());
}

TEST(LoggerMacros, ForwardsFormatArguments)
{
    auto sink = std::make_shared<MacroSourceSink>();
    auto logger = std::make_shared<nova::log::Logger>("format", sink);

    NOVA_LOGGER_INFO(logger, "value {:04d}", 7);

    EXPECT_EQ(sink->message, "value 0007");
}

}  // namespace
