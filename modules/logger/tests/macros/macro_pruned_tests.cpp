#include <logger/logger.h>
#include <logger/macros.h>
#include <logger/log.h>
#include <gtest/gtest.h>

#include <memory>

TEST(LoggerMacros, PrunedMacroDoesNotEvaluateArguments)
{
    auto logger = std::make_shared<novainfra::log::Logger>("pruned");
    int evaluationCount = 0;

    NOVAINFRA_LOGGER_INFO(logger, (++evaluationCount, "message"));

    EXPECT_EQ(evaluationCount, 0);
}

TEST(LoggerMacros, PrunedVariadicMacroDoesNotEvaluateFormatArguments)
{
    auto logger = std::make_shared<novainfra::log::Logger>("pruned");
    int evaluationCount = 0;

    NOVAINFRA_LOGGER_INFO(logger, "value {}", ++evaluationCount);

    EXPECT_EQ(evaluationCount, 0);
}

TEST(LoggerMacros, PrunedGlobalMacroDoesNotEvaluateArguments)
{
    int evaluationCount = 0;

    NOVAINFRA_LOG_INFO("value {}", ++evaluationCount);

    EXPECT_EQ(evaluationCount, 0);
}
