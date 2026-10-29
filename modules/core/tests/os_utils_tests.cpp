#include <gtest/gtest.h>
#include <core/os_utils.h>
#include <thread>
#include <set>
#include <fstream>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <system_error>

using namespace novainfra::core;

namespace {
class ScopedTempDirectory
{
public:
    ScopedTempDirectory()
    {
        const auto base = std::filesystem::temp_directory_path(error_);
        if (error_)
            return;
        auto pattern = (base / "novainfra-core-tests-XXXXXX").string();
        if (const char *created = ::mkdtemp(pattern.data()))
            path_ = created;
        else
            error_ = std::error_code(errno, std::generic_category());
    }

    ~ScopedTempDirectory()
    {
        if (path_.empty())
            return;
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        EXPECT_FALSE(error) << "Cannot remove " << path_ << ": " << error.message();
    }

    ScopedTempDirectory(const ScopedTempDirectory &) = delete;
    ScopedTempDirectory &operator=(const ScopedTempDirectory &) = delete;

    const std::filesystem::path &path() const { return path_; }
    const std::error_code &error() const { return error_; }

private:
    std::filesystem::path path_;
    std::error_code error_;
};

struct FileCloser {
    void operator()(FILE *file) const { std::fclose(file); }
};
using ScopedFile = std::unique_ptr<FILE, FileCloser>;
}  // namespace

TEST(OsUtils, threadId)
{
    // Get thread ID
    size_t tid = threadId();
    EXPECT_GT(tid, 0);

    // Same thread should have same ID
    size_t tid2 = threadId();
    EXPECT_EQ(tid, tid2);
}

TEST(OsUtils, ThreadIdUniqueness)
{
    const int numThreads = 10;
    std::vector<std::thread> threads;
    std::vector<size_t> threadIds(numThreads);

    // Get thread IDs from multiple threads
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([i, &threadIds]() { threadIds[i] = threadId(); });
    }

    for (auto &t : threads) {
        t.join();
    }

    // All thread IDs should be different
    std::set<size_t> uniqueIds(threadIds.begin(), threadIds.end());
    EXPECT_EQ(uniqueIds.size(), numThreads);
}

TEST(OsUtils, ProcessId)
{
    int currentPid = pid();
    EXPECT_GT(currentPid, 0);

    // Process ID should be consistent
    int secondPid = pid();
    EXPECT_EQ(currentPid, secondPid);
}

TEST(OsUtils, isColorTerminal)
{
    // Just verify it doesn't crash
    bool isColor = isColorTerminal();
    (void)isColor;  // Suppress unused warning
}

TEST(OsUtils, inTerminal)
{
    // Test with stdout
    bool isTerminal = inTerminal(stdout);
    (void)isTerminal;

    // Test with stderr
    bool isTerminalErr = inTerminal(stderr);
    (void)isTerminalErr;
}

TEST(OsUtils, StringTrim)
{
    EXPECT_EQ(trim("  hello  "), "hello");
    EXPECT_EQ(trim("hello"), "hello");
    EXPECT_EQ(trim("  hello"), "hello");
    EXPECT_EQ(trim("hello  "), "hello");
    EXPECT_EQ(trim("   "), "");
    EXPECT_EQ(trim(""), "");
    EXPECT_EQ(trim("\t\nhello\r\n"), "hello");
}

TEST(OsUtils, trimLeft)
{
    EXPECT_EQ(trimLeft("  hello  "), "hello  ");
    EXPECT_EQ(trimLeft("hello"), "hello");
    EXPECT_EQ(trimLeft("  hello"), "hello");
    EXPECT_EQ(trimLeft("   "), "");
}

TEST(OsUtils, trimRight)
{
    EXPECT_EQ(trimRight("  hello  "), "  hello");
    EXPECT_EQ(trimRight("hello"), "hello");
    EXPECT_EQ(trimRight("hello  "), "hello");
    EXPECT_EQ(trimRight("   "), "");
}

TEST(OsUtils, TimeOperations)
{
    // Test steadyNowTime
    auto start = steadyNowTime();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto elapsed = elapsedMs(start);
    EXPECT_GE(elapsed, 10);
    EXPECT_LT(elapsed, 100);

    // Test now
    auto timestamp = now();
    EXPECT_GT(timestamp.time_since_epoch().count(), 0);
}

TEST(OsUtils, LocaltimeAndGmtime)
{
    std::time_t current = std::time(nullptr);

    // Test localTime
    std::tm localTm = localTime(current);
    EXPECT_GE(localTm.tm_year, 126);  // Should be at least 2026
    EXPECT_GE(localTm.tm_mon, 0);
    EXPECT_LE(localTm.tm_mon, 11);

    // Test gmTime
    std::tm gmtTm = gmTime(current);
    EXPECT_GE(gmtTm.tm_year, 126);
    EXPECT_GE(gmtTm.tm_mon, 0);
    EXPECT_LE(gmtTm.tm_mon, 11);

    // Test no-arg versions
    std::tm localTm2 = localTime();
    EXPECT_GE(localTm2.tm_year, 126);

    std::tm gmtTm2 = gmTime();
    EXPECT_GE(gmtTm2.tm_year, 126);
}

TEST(OsUtils, pathExists)
{
    const ScopedTempDirectory temporary;
    ASSERT_FALSE(temporary.path().empty()) << temporary.error().message();
    EXPECT_TRUE(pathExists(temporary.path().string()));

    // Test with non-existent path
    EXPECT_FALSE(pathExists((temporary.path() / "missing").string()));
}

TEST(OsUtils, FileOperations)
{
    const ScopedTempDirectory temporary;
    ASSERT_FALSE(temporary.path().empty()) << temporary.error().message();
    const auto testFilename = (temporary.path() / "file.txt").string();

    // Create a test file
    FILE *fp = nullptr;
    bool openFailed = openFile(&fp, testFilename, "w");
    ScopedFile file(fp);
    ASSERT_FALSE(openFailed);
    ASSERT_NE(fp, nullptr);

    // Write some data
    const char *testData = "Hello, NovaInfra!";
    bool writeSuccess = writeBytes(testData, strlen(testData), fp);
    EXPECT_TRUE(writeSuccess);

    // Close file
    file.reset();

    // Verify file exists
    EXPECT_TRUE(pathExists(testFilename));

    // Open for reading and check size
    fp = nullptr;
    openFailed = openFile(&fp, testFilename, "r");
    file.reset(fp);
    ASSERT_FALSE(openFailed);
    ASSERT_NE(fp, nullptr);

    size_t size = fileSize(fp);
    EXPECT_EQ(size, strlen(testData));

    file.reset();

    // Test rename
    const auto renamedFilename = (temporary.path() / "renamed.txt").string();
    int renameResult = renameFile(testFilename, renamedFilename);
    EXPECT_EQ(renameResult, 0);
    EXPECT_TRUE(pathExists(renamedFilename));
    EXPECT_FALSE(pathExists(testFilename));

    // Test remove
    int removeResult = removeFile(renamedFilename);
    EXPECT_EQ(removeResult, 0);
    EXPECT_FALSE(pathExists(renamedFilename));

    // Test removeIfExists on non-existent file (should not fail)
    int removeResult2 = removeIfExists(renamedFilename);
    EXPECT_EQ(removeResult2, 0);
}

TEST(OsUtils, DirectoryOperations)
{
    const ScopedTempDirectory temporary;
    ASSERT_FALSE(temporary.path().empty()) << temporary.error().message();
    const auto testDir = (temporary.path() / "dir").string();
    const auto nestedDir = (temporary.path() / "dir/nested/deep").string();
    EXPECT_FALSE(pathExists(testDir));

    // Create directory
    bool createSuccess = createDir(testDir);
    EXPECT_TRUE(createSuccess);
    EXPECT_TRUE(pathExists(testDir));

    // Create nested directory
    createSuccess = createDir(nestedDir);
    EXPECT_TRUE(createSuccess);
    EXPECT_TRUE(pathExists(nestedDir));

    // createDir on existing directory should succeed
    createSuccess = createDir(testDir);
    EXPECT_TRUE(createSuccess);

    // Test dirName
    auto parent = dirName(testDir + "/file.txt");
    EXPECT_EQ(parent, testDir);

    auto parent2 = dirName(testDir + "/");
    EXPECT_EQ(parent2, testDir);

    auto parent3 = dirName("file.txt");
    EXPECT_EQ(parent3, "");
}

TEST(OsUtils, getEnv)
{
    // Test getting PATH environment variable (should exist on all systems)
    std::string path = getEnv("PATH");
    EXPECT_FALSE(path.empty());

    // Test non-existent variable
    std::string nonExistent = getEnv("NOVAINFRA_NONEXISTENT_VAR_12345");
    EXPECT_TRUE(nonExistent.empty());
}

TEST(OsUtils, sleepForMillis)
{
    auto start = steadyNowTime();
    sleepForMillis(50);
    auto elapsed = elapsedMs(start);

    EXPECT_GE(elapsed, 50);
    EXPECT_LT(elapsed, 150);  // Allow some tolerance
}

TEST(OsUtils, filenameToStr)
{
    std::string filename = "test.txt";
    std::string result = filenameToStr(filename);
    EXPECT_EQ(result, filename);

    std::string emptyFilename = "";
    std::string emptyResult = filenameToStr(emptyFilename);
    EXPECT_EQ(emptyResult, "");
}

TEST(OsUtils, fsyncFile)
{
    const ScopedTempDirectory temporary;
    ASSERT_FALSE(temporary.path().empty()) << temporary.error().message();
    const auto testFilename = (temporary.path() / "fsync.txt").string();

    FILE *fp = nullptr;
    bool openFailed = openFile(&fp, testFilename, "w");
    ScopedFile file(fp);
    ASSERT_FALSE(openFailed);
    ASSERT_NE(fp, nullptr);

    const char *testData = "fsyncFile test data";
    writeBytes(testData, strlen(testData), fp);

    // Test fsync
    bool fsyncSuccess = fsyncFile(fp);
    EXPECT_TRUE(fsyncSuccess);
}
