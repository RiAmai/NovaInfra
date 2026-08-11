#include <logger/logger.h>
#include <logger/macros.h>
#include <logger/novalog.h>
#include <gtest/gtest.h>

#include <memory>

TEST(LoggerMacros, PrunedMacroDoesNotEvaluateArguments)
{
    auto logger = std::make_shared<nova::log::Logger>("pruned");
    int evaluationCount = 0;

    NOVA_LOGGER_INFO(logger, (++evaluationCount, "message"));

    EXPECT_EQ(evaluationCount, 0);
}

TEST(LoggerMacros, PrunedVariadicMacroDoesNotEvaluateFormatArguments)
{
    auto logger = std::make_shared<nova::log::Logger>("pruned");
    int evaluationCount = 0;

    NOVA_LOGGER_INFO(logger, "value {}", ++evaluationCount);

    EXPECT_EQ(evaluationCount, 0);
}

TEST(LoggerMacros, PrunedGlobalMacroDoesNotEvaluateArguments)
{
    int evaluationCount = 0;

    NOVA_LOG_INFO("value {}", ++evaluationCount);

    EXPECT_EQ(evaluationCount, 0);
}
