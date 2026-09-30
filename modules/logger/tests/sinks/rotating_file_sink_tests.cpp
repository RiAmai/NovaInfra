#include <logger/logger.h>
#include <logger/sinks/rotating_file_sink.h>
#include <core/os_utils.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

namespace {

using RotatingSink = novainfra::log::sinks::RotatingFileSink<std::mutex>;

class ScopedTempDirectory
{
public:
    ScopedTempDirectory()
        : path_(std::filesystem::temp_directory_path() /
                ("novainfra_rotating_tests_" + std::to_string(novainfra::core::Pid()) + "_" +
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

TEST(RotatingFileSink, CalculatesDotSeparatedNumericSuffixBeforeExtension)
{
    EXPECT_EQ(RotatingSink::calcFilename("/tmp/app.log", 0), "/tmp/app.log");
    EXPECT_EQ(RotatingSink::calcFilename("/tmp/app.log", 1), "/tmp/app.1.log");
    EXPECT_EQ(RotatingSink::calcFilename("/tmp/app.log", 12), "/tmp/app.12.log");
    EXPECT_EQ(RotatingSink::calcFilename("/tmp/app", 1), "/tmp/app.1");
    EXPECT_EQ(RotatingSink::calcFilename("/tmp/.hidden", 1), "/tmp/.hidden.1");
}

TEST(RotatingFileSink, RejectsInvalidLimitsBeforeOpeningFile)
{
    const ScopedTempDirectory tempDirectory;
    const auto path = tempDirectory.path() / "app.log";

    EXPECT_THROW(RotatingSink(path.string(), 0, 3), std::invalid_argument);
    EXPECT_THROW(RotatingSink(path.string(), 1, RotatingSink::kMaxFileNum + 1), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(RotatingFileSink, RuntimeLimitsAreValidatedAndSynchronized)
{
    const ScopedTempDirectory tempDirectory;
    const auto path = tempDirectory.path() / "runtime-limits.log";
    auto sink = std::make_shared<RotatingSink>(path.string(), 1024 * 1024, 2);
    sink->setPattern("%v");
    novainfra::log::Logger logger("rotate-test", sink);

    EXPECT_THROW(sink->setMaxSize(0), std::invalid_argument);
    EXPECT_THROW(sink->setMaxFileNum(RotatingSink::kMaxFileNum + 1), std::invalid_argument);
    EXPECT_EQ(sink->getMaxSize(), 1024U * 1024U);
    EXPECT_EQ(sink->getMaxFileNum(), 2U);

    std::thread writer([&] {
        for (int index = 0; index < 1000; ++index) {
            logger.info("message {}", index);
        }
    });
    std::thread configurator([&] {
        for (int index = 0; index < 1000; ++index) {
            sink->setMaxSize(index % 2 == 0 ? 1024U * 1024U : 2U * 1024U * 1024U);
            sink->setMaxFileNum(index % 2 == 0 ? 2U : 3U);
        }
    });

    writer.join();
    configurator.join();
    logger.flush();

    EXPECT_GT(std::filesystem::file_size(path), 0U);
}

TEST(RotatingFileSink, RotatesBeforeOverflowAndKeepsConfiguredHistory)
{
    const ScopedTempDirectory tempDirectory;
    const auto path = tempDirectory.path() / "app.log";
    auto sink = std::make_shared<RotatingSink>(path.string(), 10, 3);
    sink->setPattern("%v");
    novainfra::log::Logger logger("rotate-test", sink);

    for (int index = 0; index < 10; ++index) {
        logger.info("{:04d}", index);
    }
    logger.flush();

    EXPECT_EQ(ReadFile(path), "0008\n0009\n");
    EXPECT_EQ(ReadFile(RotatingSink::calcFilename(path.string(), 1)), "0006\n0007\n");
    EXPECT_EQ(ReadFile(RotatingSink::calcFilename(path.string(), 2)), "0004\n0005\n");
    EXPECT_EQ(ReadFile(RotatingSink::calcFilename(path.string(), 3)), "0002\n0003\n");
    EXPECT_FALSE(std::filesystem::exists(RotatingSink::calcFilename(path.string(), 4)));
}

TEST(RotatingFileSink, RotateNowResetsCurrentSize)
{
    const ScopedTempDirectory tempDirectory;
    const auto path = tempDirectory.path() / "app.log";
    auto sink = std::make_shared<RotatingSink>(path.string(), 10, 2);
    sink->setPattern("%v");
    novainfra::log::Logger logger("rotate-test", sink);

    logger.info("aaaa");
    sink->rotateNow();
    logger.info("bbbb");
    logger.info("cccc");
    logger.flush();

    EXPECT_EQ(ReadFile(path), "bbbb\ncccc\n");
    EXPECT_EQ(ReadFile(RotatingSink::calcFilename(path.string(), 1)), "aaaa\n");
    EXPECT_FALSE(std::filesystem::exists(RotatingSink::calcFilename(path.string(), 2)));
}

TEST(RotatingFileSink, RotateOnOpenArchivesExistingFile)
{
    const ScopedTempDirectory tempDirectory;
    const auto path = tempDirectory.path() / "app.log";
    {
        std::ofstream output(path, std::ios::binary);
        output << "existing\n";
    }

    {
        auto sink = std::make_shared<RotatingSink>(path.string(), 100, 2, true);
        sink->setPattern("%v");
        novainfra::log::Logger logger("rotate-test", sink);
        logger.info("fresh");
        logger.flush();
    }

    EXPECT_EQ(ReadFile(path), "fresh\n");
    EXPECT_EQ(ReadFile(RotatingSink::calcFilename(path.string(), 1)), "existing\n");
}

}  // namespace
