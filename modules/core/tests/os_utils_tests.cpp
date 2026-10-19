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

TEST(OsUtils, ThreadId)
{
    // Get thread ID
    size_t tid = ThreadId();
    EXPECT_GT(tid, 0);

    // Same thread should have same ID
    size_t tid2 = ThreadId();
    EXPECT_EQ(tid, tid2);
}

TEST(OsUtils, ThreadIdUniqueness)
{
    const int numThreads = 10;
    std::vector<std::thread> threads;
    std::vector<size_t> threadIds(numThreads);

    // Get thread IDs from multiple threads
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([i, &threadIds]() { threadIds[i] = ThreadId(); });
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
    int pid = Pid();
    EXPECT_GT(pid, 0);

    // Process ID should be consistent
    int pid2 = Pid();
    EXPECT_EQ(pid, pid2);
}

TEST(OsUtils, IsColorTerminal)
{
    // Just verify it doesn't crash
    bool isColor = IsColorTerminal();
    (void)isColor;  // Suppress unused warning
}

TEST(OsUtils, InTerminal)
{
    // Test with stdout
    bool isTerminal = InTerminal(stdout);
    (void)isTerminal;

    // Test with stderr
    bool isTerminalErr = InTerminal(stderr);
    (void)isTerminalErr;
}

TEST(OsUtils, StringTrim)
{
    EXPECT_EQ(Trimed("  hello  "), "hello");
    EXPECT_EQ(Trimed("hello"), "hello");
    EXPECT_EQ(Trimed("  hello"), "hello");
    EXPECT_EQ(Trimed("hello  "), "hello");
    EXPECT_EQ(Trimed("   "), "");
    EXPECT_EQ(Trimed(""), "");
    EXPECT_EQ(Trimed("\t\nhello\r\n"), "hello");
}

TEST(OsUtils, TrimedLeft)
{
    EXPECT_EQ(TrimedLeft("  hello  "), "hello  ");
    EXPECT_EQ(TrimedLeft("hello"), "hello");
    EXPECT_EQ(TrimedLeft("  hello"), "hello");
    EXPECT_EQ(TrimedLeft("   "), "");
}

TEST(OsUtils, TrimedRight)
{
    EXPECT_EQ(TrimedRight("  hello  "), "  hello");
    EXPECT_EQ(TrimedRight("hello"), "hello");
    EXPECT_EQ(TrimedRight("hello  "), "hello");
    EXPECT_EQ(TrimedRight("   "), "");
}

TEST(OsUtils, TimeOperations)
{
    // Test SteadyNowTime
    auto start = SteadyNowTime();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto elapsed = ElapsedMs(start);
    EXPECT_GE(elapsed, 10);
    EXPECT_LT(elapsed, 100);

    // Test Now
    auto now = Now();
    EXPECT_GT(now.time_since_epoch().count(), 0);
}

TEST(OsUtils, LocaltimeAndGmtime)
{
    std::time_t now = std::time(nullptr);

    // Test Localtime
    std::tm localTm = Localtime(now);
    EXPECT_GE(localTm.tm_year, 126);  // Should be at least 2026
    EXPECT_GE(localTm.tm_mon, 0);
    EXPECT_LE(localTm.tm_mon, 11);

    // Test Gmtime
    std::tm gmtTm = Gmtime(now);
    EXPECT_GE(gmtTm.tm_year, 126);
    EXPECT_GE(gmtTm.tm_mon, 0);
    EXPECT_LE(gmtTm.tm_mon, 11);

    // Test no-arg versions
    std::tm localTm2 = Localtime();
    EXPECT_GE(localTm2.tm_year, 126);

    std::tm gmtTm2 = Gmtime();
    EXPECT_GE(gmtTm2.tm_year, 126);
}

TEST(OsUtils, PathExists)
{
    const ScopedTempDirectory temporary;
    ASSERT_FALSE(temporary.path().empty()) << temporary.error().message();
    EXPECT_TRUE(PathExists(temporary.path().string()));

    // Test with non-existent path
    EXPECT_FALSE(PathExists((temporary.path() / "missing").string()));
}

TEST(OsUtils, FileOperations)
{
    const ScopedTempDirectory temporary;
    ASSERT_FALSE(temporary.path().empty()) << temporary.error().message();
    const auto testFilename = (temporary.path() / "file.txt").string();

    // Create a test file
    FILE *fp = nullptr;
    bool openFailed = Fopen_s(&fp, testFilename, "w");
    ScopedFile file(fp);
    ASSERT_FALSE(openFailed);
    ASSERT_NE(fp, nullptr);

    // Write some data
    const char *testData = "Hello, NovaInfra!";
    bool writeSuccess = FwriteBytes(testData, strlen(testData), fp);
    EXPECT_TRUE(writeSuccess);

    // Close file
    file.reset();

    // Verify file exists
    EXPECT_TRUE(PathExists(testFilename));

    // Open for reading and check size
    fp = nullptr;
    openFailed = Fopen_s(&fp, testFilename, "r");
    file.reset(fp);
    ASSERT_FALSE(openFailed);
    ASSERT_NE(fp, nullptr);

    size_t fileSize = Filesize(fp);
    EXPECT_EQ(fileSize, strlen(testData));

    file.reset();

    // Test rename
    const auto renamedFilename = (temporary.path() / "renamed.txt").string();
    int renameResult = Rename(testFilename, renamedFilename);
    EXPECT_EQ(renameResult, 0);
    EXPECT_TRUE(PathExists(renamedFilename));
    EXPECT_FALSE(PathExists(testFilename));

    // Test remove
    int removeResult = Remove(renamedFilename);
    EXPECT_EQ(removeResult, 0);
    EXPECT_FALSE(PathExists(renamedFilename));

    // Test RemoveIfExists on non-existent file (should not fail)
    int removeResult2 = RemoveIfExists(renamedFilename);
    EXPECT_EQ(removeResult2, 0);
}

TEST(OsUtils, DirectoryOperations)
{
    const ScopedTempDirectory temporary;
    ASSERT_FALSE(temporary.path().empty()) << temporary.error().message();
    const auto testDir = (temporary.path() / "dir").string();
    const auto nestedDir = (temporary.path() / "dir/nested/deep").string();
    EXPECT_FALSE(PathExists(testDir));

    // Create directory
    bool createSuccess = CreateDir(testDir);
    EXPECT_TRUE(createSuccess);
    EXPECT_TRUE(PathExists(testDir));

    // Create nested directory
    createSuccess = CreateDir(nestedDir);
    EXPECT_TRUE(createSuccess);
    EXPECT_TRUE(PathExists(nestedDir));

    // CreateDir on existing directory should succeed
    createSuccess = CreateDir(testDir);
    EXPECT_TRUE(createSuccess);

    // Test DirName
    auto dirName = DirName(testDir + "/file.txt");
    EXPECT_EQ(dirName, testDir);

    auto dirName2 = DirName(testDir + "/");
    EXPECT_EQ(dirName2, testDir);

    auto dirName3 = DirName("file.txt");
    EXPECT_EQ(dirName3, "");
}

TEST(OsUtils, Getenv)
{
    // Test getting PATH environment variable (should exist on all systems)
    std::string path = Getenv("PATH");
    EXPECT_FALSE(path.empty());

    // Test non-existent variable
    std::string nonExistent = Getenv("NOVAINFRA_NONEXISTENT_VAR_12345");
    EXPECT_TRUE(nonExistent.empty());
}

TEST(OsUtils, SleepForMillis)
{
    auto start = SteadyNowTime();
    SleepForMillis(50);
    auto elapsed = ElapsedMs(start);

    EXPECT_GE(elapsed, 50);
    EXPECT_LT(elapsed, 150);  // Allow some tolerance
}

TEST(OsUtils, FilenameToStr)
{
    std::string filename = "test.txt";
    std::string result = FilenameToStr(filename);
    EXPECT_EQ(result, filename);

    std::string emptyFilename = "";
    std::string emptyResult = FilenameToStr(emptyFilename);
    EXPECT_EQ(emptyResult, "");
}

TEST(OsUtils, Fsync)
{
    const ScopedTempDirectory temporary;
    ASSERT_FALSE(temporary.path().empty()) << temporary.error().message();
    const auto testFilename = (temporary.path() / "fsync.txt").string();

    FILE *fp = nullptr;
    bool openFailed = Fopen_s(&fp, testFilename, "w");
    ScopedFile file(fp);
    ASSERT_FALSE(openFailed);
    ASSERT_NE(fp, nullptr);

    const char *testData = "Fsync test data";
    FwriteBytes(testData, strlen(testData), fp);

    // Test fsync
    bool fsyncSuccess = Fsync(fp);
    EXPECT_TRUE(fsyncSuccess);
}
