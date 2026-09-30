#include <logger/sinks/console_sink.h>
#include <logger/logger.h>

#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <string>

TEST(ConsoleOutSink, RoutesLevelsToExpectedStreams)
{
    auto sink = std::make_shared<novainfra::log::sinks::ConsoleOutSink<std::mutex>>();
    novainfra::log::Logger logger("console", sink);

    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    logger.info("stdout-message");
    logger.error("stderr-message");
    const std::string stderrOutput = testing::internal::GetCapturedStderr();
    const std::string stdoutOutput = testing::internal::GetCapturedStdout();

    EXPECT_NE(stdoutOutput.find("stdout-message"), std::string::npos);
    EXPECT_EQ(stdoutOutput.find("stderr-message"), std::string::npos);
    EXPECT_NE(stderrOutput.find("stderr-message"), std::string::npos);
    EXPECT_EQ(stderrOutput.find("stdout-message"), std::string::npos);
}
