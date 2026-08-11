#include <logger/formatters/pattern_formatter.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {

nova::log::LogMsg MakeMessage(nova::SystemTimePoint timestamp, std::string_view file = "path/to/file.cpp")
{
    return nova::log::LogMsg{
        timestamp,
        nova::log::Level::INFO,
        nova::core::ThreadId(),
        nova::log::SourceLocation{file, 42, "function"},
        "formatter",
        "message"
    };
}

TEST(PatternFormatterOutput, DefaultPatternMatchesDocumentedLayout)
{
    const auto message = MakeMessage(nova::SystemTimePoint{std::chrono::milliseconds{5}});
    nova::log::formatters::PatternFormatter formatter(nova::log::formatters::PatternTimeType::UTC);
    std::string output;

    formatter.format(message, output);

    EXPECT_EQ(output, "[1970-01-01 00:00:00.005] [INFO] [formatter] [tid=" +
                          std::to_string(message.threadId) + "] [path/to/file.cpp:42] message\n");
}

TEST(PatternFormatterOutput, DefaultPatternAppendsAndPreservesEmbeddedNullFields)
{
    const std::string loggerName("logger\0name", 11);
    const std::string message("message\0body", 12);
    const std::string file("path/fi\0le.cpp", 14);
    const nova::log::LogMsg logMessage{
        nova::SystemTimePoint{std::chrono::milliseconds{5}},
        nova::log::Level::INFO,
        7,
        nova::log::SourceLocation{file, 42, "function"},
        loggerName,
        message
    };
    nova::log::formatters::PatternFormatter formatter(nova::log::formatters::PatternTimeType::UTC);
    std::string output = "prefix|";

    formatter.format(logMessage, output);

    std::string expected = "prefix|[1970-01-01 00:00:00.005] [INFO] [";
    expected.append(loggerName);
    expected.append("] [tid=7] [");
    expected.append(file);
    expected.append(":42] ");
    expected.append(message);
    expected.push_back('\n');
    EXPECT_EQ(output, expected);
}

TEST(PatternFormatterOutput, CustomPatternFormatsCoreFieldsAndEscapedPercent)
{
    const auto timestamp =
        nova::SystemTimePoint{std::chrono::seconds{978307200} + std::chrono::milliseconds{5}};
    const auto message = MakeMessage(timestamp);
    nova::log::formatters::PatternFormatter formatter(
        "%Y-%m-%d %H:%M:%S.%e|%C|%D|%n|%l|%t|%s|%g|%@|%#|%!|%%|%v",
        nova::log::formatters::PatternTimeType::UTC);
    std::string output;

    formatter.format(message, output);

    EXPECT_EQ(output, "2001-01-01 00:00:00.005|01|01/01/01|formatter|INFO|" +
                          std::to_string(message.threadId) +
                          "|file.cpp|path/to/file.cpp|path/to/file.cpp:42|42|function|%|message\n");
}

TEST(PatternFormatterOutput, TimeAliasesAndNamesProduceExpectedValues)
{
    const auto message = MakeMessage(nova::SystemTimePoint{std::chrono::seconds{978307200}});
    nova::log::formatters::PatternFormatter formatter("%a|%A|%b|%h|%B|%c|%D|%x|%R|%T|%X|%E|%u|%i|%o|%O",
                                                      nova::log::formatters::PatternTimeType::UTC);
    std::string output;

    formatter.format(message, output);

    EXPECT_EQ(output, "Mon|Monday|Jan|Jan|January|Mon Jan 01 00:00:00 2001|01/01/01|01/01/01|"
                      "00:00|00:00:00|00:00:00|978307200|0|0|0|0\n");
}

TEST(PatternFormatterOutput, SubsecondsAndMidnightUseFixedWidth)
{
    const auto message = MakeMessage(nova::SystemTimePoint{std::chrono::milliseconds{5}});
    nova::log::formatters::PatternFormatter formatter("%e|%f|%F|%I|%r|%p",
                                                      nova::log::formatters::PatternTimeType::UTC);
    std::string output;

    formatter.format(message, output);

    EXPECT_EQ(output, "005|005000|005000000|12|12:00:00 AM|AM\n");
}

TEST(PatternFormatterOutput, FixedWidthFastPathsHandleUpperBoundaries)
{
    const auto timestamp =
        nova::SystemTimePoint{std::chrono::seconds{978307200} + std::chrono::nanoseconds{999999999}};
    const auto message = MakeMessage(timestamp);
    nova::log::formatters::PatternFormatter formatter("%Y-%m-%d %H:%M:%S.%e|%f|%F",
                                                      nova::log::formatters::PatternTimeType::UTC);
    std::string output;

    formatter.format(message, output);

    EXPECT_EQ(output, "2001-01-01 00:00:00.999|999999|999999999\n");
}

TEST(PatternFormatterOutput, PreEpochSubsecondsAreNormalized)
{
    const auto message = MakeMessage(nova::SystemTimePoint{std::chrono::milliseconds{-1}});
    nova::log::formatters::PatternFormatter formatter("%Y-%m-%d %H:%M:%S.%e|%f|%F",
                                                      nova::log::formatters::PatternTimeType::UTC);
    std::string output;

    formatter.format(message, output);

    EXPECT_EQ(output, "1969-12-31 23:59:59.999|999000|999000000\n");
}

TEST(PatternFormatterOutput, PreEpochNanosecondsAreNormalizedAtEveryPrecision)
{
    const auto message = MakeMessage(nova::SystemTimePoint{std::chrono::nanoseconds{-1}});
    nova::log::formatters::PatternFormatter formatter("%Y-%m-%d %H:%M:%S.%e|%f|%F",
                                                      nova::log::formatters::PatternTimeType::UTC);
    std::string output;

    formatter.format(message, output);

    EXPECT_EQ(output, "1969-12-31 23:59:59.999|999999|999999999\n");
}

TEST(PatternFormatterOutput, PreservesLongAndEmbeddedNullFields)
{
    const std::string loggerName("logger\0name", 11);
    const std::string message(2048, 'x');
    const std::string file("root/fi\0le.cpp", 14);
    const std::string function("func\0tion", 9);
    const nova::log::LogMsg logMessage{
        nova::SystemTimePoint{},
        nova::log::Level::INFO,
        7,
        nova::log::SourceLocation{file, 42, function},
        loggerName,
        message
    };
    nova::log::formatters::PatternFormatter formatter("%n|%v|%g|%s|%!",
                                                      nova::log::formatters::PatternTimeType::UTC);
    std::string output;

    formatter.format(logMessage, output);

    std::string expected = loggerName;
    expected.push_back('|');
    expected.append(message);
    expected.push_back('|');
    expected.append(file);
    expected.push_back('|');
    expected.append(file.data() + 5, file.size() - 5);
    expected.push_back('|');
    expected.append(function);
    expected.push_back('\n');
    EXPECT_EQ(output, expected);
}

TEST(PatternFormatterOutput, ShortFilenameNeverReadsPastStringView)
{
    std::string backing = "root/file.cppTAIL";
    const std::string_view file(backing.data(), std::string_view("root/file.cpp").size());
    const auto message = MakeMessage(nova::SystemTimePoint{}, file);
    nova::log::formatters::PatternFormatter formatter("%s|%g", nova::log::formatters::PatternTimeType::UTC);
    std::string output;

    formatter.format(message, output);

    EXPECT_EQ(output, "file.cpp|root/file.cpp\n");
}

TEST(PatternFormatterConfiguration, RejectsInvalidPatternWithoutChangingCurrentPattern)
{
    nova::log::formatters::PatternFormatter formatter("%v");

    EXPECT_THROW(formatter.setPattern("%q"), std::invalid_argument);
    EXPECT_THROW(formatter.setPattern("trailing%"), std::invalid_argument);

    std::string output;
    formatter.format(MakeMessage(nova::SystemTimePoint{}), output);
    EXPECT_EQ(output, "message\n");

    formatter.setPattern("%%");
    output.clear();
    formatter.format(MakeMessage(nova::SystemTimePoint{}), output);
    EXPECT_EQ(output, "%\n");
}

TEST(PatternFormatterConfiguration, ClonePreservesConfigurationAndResetsElapsedState)
{
    nova::log::formatters::PatternFormatter formatter("%Y|%o|%v", nova::log::formatters::PatternTimeType::UTC,
                                                      "<eol>");
    auto first = MakeMessage(nova::SystemTimePoint{std::chrono::milliseconds{5}});
    auto second = MakeMessage(nova::SystemTimePoint{std::chrono::milliseconds{15}});
    std::string output;

    formatter.format(first, output);
    EXPECT_EQ(output, "1970|0|message<eol>");
    output.clear();
    formatter.format(second, output);
    EXPECT_EQ(output, "1970|10|message<eol>");

    auto cloned = formatter.clone();
    output.clear();
    cloned->format(second, output);
    EXPECT_EQ(output, "1970|0|message<eol>");
}

TEST(PatternFormatterConfiguration, RepeatedElapsedTokensKeepIndependentState)
{
    nova::log::formatters::PatternFormatter formatter("%o|%o", nova::log::formatters::PatternTimeType::UTC);
    const auto first = MakeMessage(nova::SystemTimePoint{std::chrono::milliseconds{5}});
    const auto second = MakeMessage(nova::SystemTimePoint{std::chrono::milliseconds{15}});
    std::string output;

    formatter.format(first, output);
    EXPECT_EQ(output, "0|0\n");
    output.clear();
    formatter.format(second, output);
    EXPECT_EQ(output, "10|10\n");
}

TEST(PatternFormatterConcurrency, IndependentFormattersDoNotMixLocalTimeState)
{
    constexpr int kThreadCount = 16;
    constexpr int kIterations = 50000;
    const auto firstTimestamp = nova::SystemTimePoint{std::chrono::seconds{1000000000}};
    const auto secondTimestamp = nova::SystemTimePoint{std::chrono::seconds{2000000000}};
    std::atomic<bool> valid{true};
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for (int threadIndex = 0; threadIndex < kThreadCount; ++threadIndex) {
        threads.emplace_back([threadIndex, firstTimestamp, secondTimestamp, &valid]() {
            nova::log::formatters::PatternFormatter formatter;
            const bool useFirst = (threadIndex % 2) == 0;
            const auto message = MakeMessage(useFirst ? firstTimestamp : secondTimestamp);
            const std::string expectedYear = useFirst ? "[2001-" : "[2033-";
            for (int iteration = 0; iteration < kIterations && valid.load(); ++iteration) {
                std::string output;
                formatter.format(message, output);
                if (output.rfind(expectedYear, 0) != 0) {
                    valid.store(false);
                }
            }
        });
    }

    for (auto &thread : threads) {
        thread.join();
    }

    EXPECT_TRUE(valid.load());
}

}  // namespace
