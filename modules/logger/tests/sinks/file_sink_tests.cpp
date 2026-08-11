#include <logger/sinks/basic_file_sink.h>
#include <logger/logger.h>
#include <core/os_utils.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>

namespace {

class ScopedTempDirectory
{
public:
    ScopedTempDirectory()
        : path_(std::filesystem::temp_directory_path() /
                ("novacore_logger_tests_" + std::to_string(nova::core::Pid()) + "_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        std::filesystem::create_directories(path_);
    }

    ~ScopedTempDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path &path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::string ReadFile(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void WriteMessage(const std::filesystem::path &path, bool truncate, std::string_view message)
{
    auto sink = std::make_shared<nova::log::sinks::BasicFileSink<std::mutex>>(path.string(), truncate);
    sink->setPattern("%v");
    nova::log::Logger logger("file-test", sink);
    logger.info(message);
    logger.flush();
}

TEST(BasicFileSink, WritesFormattedMessageAndCreatesParentDirectories)
{
    const ScopedTempDirectory tempDirectory;
    const auto path = tempDirectory.path() / "nested" / "logger.log";
    auto sink = std::make_shared<nova::log::sinks::BasicFileSink<std::mutex>>(path.string(), true);
    sink->setPattern("%v");
    nova::log::Logger logger("file-test", sink);

    logger.info("value {}", 42);
    logger.flush();

    EXPECT_EQ(sink->filename(), path.string());
    EXPECT_EQ(ReadFile(path), "value 42\n");
}

TEST(BasicFileSink, SupportsAppendConstructorAndExplicitTruncate)
{
    const ScopedTempDirectory tempDirectory;
    const auto path = tempDirectory.path() / "logger.log";

    WriteMessage(path, true, "first");
    WriteMessage(path, false, "second");
    EXPECT_EQ(ReadFile(path), "first\nsecond\n");

    {
        auto sink = std::make_shared<nova::log::sinks::BasicFileSink<std::mutex>>(path.string());
        sink->setPattern("%v");
        sink->truncate();
        nova::log::Logger logger("file-test", sink);
        logger.info("third");
        logger.flush();
    }
    EXPECT_EQ(ReadFile(path), "third\n");

    WriteMessage(path, true, "replacement");
    EXPECT_EQ(ReadFile(path), "replacement\n");
}

TEST(FileAssistant, RejectsOperationsWhileClosed)
{
    nova::log::details::FileAssistant file;

    EXPECT_THROW(file.write("message"), std::runtime_error);
    EXPECT_THROW(file.flush(), std::runtime_error);
    EXPECT_THROW(file.sync(), std::runtime_error);
    EXPECT_THROW(file.size(), std::runtime_error);
    EXPECT_NO_THROW(file.close());
}

TEST(FileAssistant, CloseStillReleasesFileWhenCallbacksThrow)
{
    const ScopedTempDirectory tempDirectory;
    const auto path = tempDirectory.path() / "callback.log";
    bool afterCloseCalled = false;
    nova::log::sinks::FileEventHandlers handlers;
    handlers.beforeClose = [](const nova::Filename &, std::FILE *) {
        throw std::runtime_error("before close failure");
    };
    handlers.afterClose = [&](const nova::Filename &) { afterCloseCalled = true; };

    nova::log::details::FileAssistant file(handlers);
    file.open(path.string(), true);
    EXPECT_THROW(file.close(), std::runtime_error);
    EXPECT_TRUE(afterCloseCalled);
    EXPECT_THROW(file.write("closed"), std::runtime_error);
    EXPECT_TRUE(std::filesystem::remove(path));
}

TEST(FileAssistant, DestructorContainsCloseCallbackFailures)
{
    const ScopedTempDirectory tempDirectory;
    const auto path = tempDirectory.path() / "destructor.log";
    nova::log::sinks::FileEventHandlers handlers;
    handlers.afterClose = [](const nova::Filename &) { throw std::runtime_error("after close failure"); };

    testing::internal::CaptureStderr();
    {
        nova::log::details::FileAssistant file(handlers);
        file.open(path.string(), true);
    }
    const auto error = testing::internal::GetCapturedStderr();

    EXPECT_NE(error.find("LOG FILE ERROR"), std::string::npos);
    EXPECT_TRUE(std::filesystem::remove(path));
}

}  // namespace
