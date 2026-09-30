#include <config/config.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <limits>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace {
std::filesystem::path temporaryFile(const char *name, const char *contents)
{
    static std::atomic<unsigned> sequence{0};
    auto path = std::filesystem::temp_directory_path() /
                (std::string(name) + "-" + std::to_string(getpid()) + "-" + std::to_string(++sequence));
    std::ofstream out(path);
    out << contents;
    return path;
}

void replaceFile(const std::filesystem::path &path, const char *contents)
{
    const auto previous = std::filesystem::last_write_time(path);
    std::ofstream out(path, std::ios::trunc);
    out << contents;
    out.close();
    std::filesystem::last_write_time(path, previous + std::chrono::seconds(2));
}

bool waitUntil(const std::function<bool()> &condition)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return condition();
}
}

TEST(Config, LoadsDefaultsAndCommandLineOverrides)
{
    novainfra::config::Schema schema;
    schema.optional<std::int64_t>("server.port", 8080);
    schema.optional<std::string>("server.host", "localhost");
    const char *argv[] = {"test", "--server.port=9090"};
    auto result = novainfra::config::ConfigManager::load(std::move(schema), {}, {}, 2, argv);
    ASSERT_TRUE(result) << result.error().message();
    EXPECT_EQ(result.value().snapshot().get<int>("server.port").value(), 9090);
    EXPECT_EQ(result.value().snapshot().get<std::string>("server.host").value(), "localhost");
}

TEST(Config, LoadsTomlAndValidates)
{
    const auto path =
        temporaryFile("novainfra-config-test.toml", "[server]\nport = 1234\nhost = \"example\"\n");
    novainfra::config::Schema schema;
    schema.required<std::int64_t>("server.port", {.min = 1, .max = 65535});
    schema.required<std::string>("server.host");
    auto result = novainfra::config::ConfigManager::load(std::move(schema), {
                                                                                {path, true}
    });
    ASSERT_TRUE(result) << result.error().message();
    EXPECT_EQ(result.value().snapshot().get<int>("server.port").value(), 1234);
    std::filesystem::remove(path);
}

TEST(Config, AggregatesUnknownAndMissingErrors)
{
    const auto path = temporaryFile("novainfra-config-invalid.toml", "unknown = true\n");
    novainfra::config::Schema schema;
    schema.required<std::string>("server.host");
    auto result = novainfra::config::ConfigManager::load(std::move(schema), {
                                                                                {path, true}
    });
    ASSERT_FALSE(result);
    EXPECT_GE(result.error().diagnostics.size(), 2u);
    std::filesystem::remove(path);
}

TEST(Config, MergesObjectsRecursively)
{
    novainfra::config::Value base(novainfra::config::Value::Object{
        {"server", novainfra::config::Value::Object{{"host", "localhost"}, {"port", std::int64_t(80)}}}
    });
    novainfra::config::Value overlay(novainfra::config::Value::Object{
        {"server", novainfra::config::Value::Object{{"port", std::int64_t(443)}}}
    });
    base.merge(overlay);
    EXPECT_EQ(std::get<std::int64_t>(base.at("server.port").storage()), 443);
    EXPECT_EQ(std::get<std::string>(base.at("server.host").storage()), "localhost");
}

TEST(Config, RejectsIntegerNarrowing)
{
    novainfra::config::Snapshot snapshot(novainfra::config::Value::Object{
        {"signed",   novainfra::config::Value(std::int64_t{5000000000})},
        {"negative", novainfra::config::Value(std::int64_t{-1})        },
        {"unsigned", novainfra::config::Value(std::uint64_t{300})      },
    });
    EXPECT_EQ(snapshot.get<int>("signed").error().diagnostics.front().code,
              novainfra::config::ErrorCode::TYPE_MISMATCH);
    EXPECT_EQ(snapshot.get<unsigned>("negative").error().diagnostics.front().code,
              novainfra::config::ErrorCode::TYPE_MISMATCH);
    EXPECT_EQ(snapshot.get<std::uint8_t>("unsigned").error().diagnostics.front().code,
              novainfra::config::ErrorCode::TYPE_MISMATCH);
    EXPECT_EQ(snapshot.get<std::int64_t>("signed").value(), 5000000000);
}

TEST(Config, RejectsInvalidNumericOverrides)
{
    novainfra::config::Schema schema;
    schema.required<std::int64_t>("signed");
    schema.required<std::uint64_t>("unsigned");
    schema.required<double>("floating");
    for (const auto &raw : {"9223372036854775808", "-9223372036854775809", "", "12tail"}) {
        auto result = schema.parse("signed", raw);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().diagnostics.front().code, novainfra::config::ErrorCode::TYPE_MISMATCH);
    }
    for (const auto &raw : {"-1", "18446744073709551616", ""})
        EXPECT_FALSE(schema.parse("unsigned", raw));
    for (const auto &raw : {"nan", "inf", "1e9999", ""})
        EXPECT_FALSE(schema.parse("floating", raw));
    EXPECT_EQ(std::get<std::int64_t>(schema.parse("signed", "-0x8000000000000000").value().storage()),
              std::numeric_limits<std::int64_t>::min());
    EXPECT_EQ(std::get<std::int64_t>(schema.parse("signed", "08").value().storage()), 8);
    EXPECT_EQ(std::get<std::uint64_t>(schema.parse("unsigned", "0xff").value().storage()), 255u);
}

TEST(Config, RejectsNonFiniteSchemaValue)
{
    novainfra::config::Schema schema;
    schema.required<double>("floating");
    novainfra::config::Value root(novainfra::config::Value::Object{
        {"floating", novainfra::config::Value(std::numeric_limits<double>::quiet_NaN())},
    });
    auto result = schema.validate(root);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().diagnostics.front().code, novainfra::config::ErrorCode::INVALID_VALUE);
}

TEST(Config, DistinguishesMissingOptionalFileFromReadError)
{
    const auto directory =
        std::filesystem::temp_directory_path() / ("novainfra-config-directory-" + std::to_string(getpid()));
    std::filesystem::create_directory(directory);
    novainfra::config::Schema schema;
    schema.optional<std::int64_t>("value", 7);
    auto unreadable = novainfra::config::ConfigManager::load(schema, {
                                                                         {directory, false}
    });
    ASSERT_FALSE(unreadable);
    EXPECT_EQ(unreadable.error().diagnostics.front().code, novainfra::config::ErrorCode::FILE_READ);
    auto missing =
        novainfra::config::ConfigManager::load(std::move(schema), {
                                                                      {directory / "missing.toml", false}
    });
    ASSERT_TRUE(missing) << missing.error().message();
    EXPECT_EQ(missing.value().snapshot().get<int>("value").value(), 7);
    std::filesystem::remove(directory);
}

TEST(Config, ValueFieldAcceptsObjectChildren)
{
    const auto path = temporaryFile("novainfra-config-plugin.toml", "[plugin]\nenabled = true\n");
    novainfra::config::Schema schema;
    schema.required<novainfra::config::Value>("plugin");
    auto result = novainfra::config::ConfigManager::load(std::move(schema), {
                                                                                {path, true}
    });
    ASSERT_TRUE(result) << result.error().message();
    EXPECT_EQ(result.value().snapshot().get<bool>("plugin.enabled").value(), true);
    std::filesystem::remove(path);
}

TEST(Config, WatchingPublishesWithoutCallbackAndStopsPromptly)
{
    const auto path = temporaryFile("novainfra-config-watch.toml", "value = 1\n");
    novainfra::config::Schema schema;
    schema.required<std::int64_t>("value");
    auto loaded = novainfra::config::ConfigManager::load(std::move(schema), {
                                                                                {path, true}
    });
    ASSERT_TRUE(loaded) << loaded.error().message();
    auto manager = std::move(loaded.value());
    ASSERT_TRUE(manager.startWatching({std::chrono::milliseconds(10), std::chrono::milliseconds(0)}));
    replaceFile(path, "value = 2\n");
    EXPECT_TRUE(waitUntil([&] { return manager.snapshot().get<int>("value").value() == 2; }));
    manager.stopWatching();
    ASSERT_TRUE(manager.startWatching({std::chrono::hours(1), std::chrono::milliseconds(0)}));
    const auto started = std::chrono::steady_clock::now();
    manager.stopWatching();
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(1));
    EXPECT_FALSE(manager.startWatching({std::chrono::milliseconds(0), std::chrono::milliseconds(0)}));
    EXPECT_FALSE(manager.startWatching({std::chrono::milliseconds(1), std::chrono::milliseconds(-1)}));
    std::filesystem::remove(path);
}

TEST(Config, CallbackMayStopWatcher)
{
    const auto path = temporaryFile("novainfra-config-self-stop.toml", "value = 1\n");
    novainfra::config::Schema schema;
    schema.required<std::int64_t>("value");
    auto loaded = novainfra::config::ConfigManager::load(std::move(schema), {
                                                                                {path, true}
    });
    ASSERT_TRUE(loaded) << loaded.error().message();
    auto manager = std::move(loaded.value());
    std::promise<void> stopped;
    auto future = stopped.get_future();
    ASSERT_TRUE(manager.startWatching({std::chrono::milliseconds(10), std::chrono::milliseconds(0)},
                                      [&](const auto &, const auto *) {
                                          manager.stopWatching();
                                          stopped.set_value();
                                      }));
    replaceFile(path, "value = 2\n");
    EXPECT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_FALSE(manager.watching());
    std::filesystem::remove(path);
}

TEST(Config, ManualReloadCannotBeOverwrittenByOlderWatchReload)
{
    const auto path = temporaryFile("novainfra-config-concurrent-reload.toml", "value = 1\n");
    std::promise<void> entered;
    std::promise<void> release;
    auto canContinue = release.get_future().share();
    std::atomic<bool> blocked{false};
    novainfra::config::Schema schema;
    schema.required<std::int64_t>(
        "value", {}, false, [&](const novainfra::config::Value &value) -> std::optional<std::string> {
            if (std::get<std::int64_t>(value.storage()) == 2 && !blocked.exchange(true)) {
                entered.set_value();
                canContinue.wait();
            }
            return std::nullopt;
        });
    auto loaded = novainfra::config::ConfigManager::load(std::move(schema), {
                                                                                {path, true}
    });
    ASSERT_TRUE(loaded) << loaded.error().message();
    auto manager = std::move(loaded.value());
    ASSERT_TRUE(manager.startWatching({std::chrono::milliseconds(10), std::chrono::milliseconds(0)}));
    replaceFile(path, "value = 2\n");
    auto enteredFuture = entered.get_future();
    if (enteredFuture.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
        release.set_value();
        manager.stopWatching();
        FAIL() << "watch reload did not reach validator";
    }
    replaceFile(path, "value = 3\n");
    std::promise<void> manualStarted;
    auto startedFuture = manualStarted.get_future();
    auto manual = std::async(std::launch::async, [&] {
        manualStarted.set_value();
        return manager.reload();
    });
    EXPECT_EQ(startedFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(manual.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);
    release.set_value();
    EXPECT_TRUE(manual.get());
    manager.stopWatching();
    EXPECT_EQ(manager.snapshot().get<int>("value").value(), 3);
    std::filesystem::remove(path);
}

#ifndef NOVAINFRA_NO_EXCEPTIONS
TEST(Config, CallbackExceptionStopsWatcherAndIsQueryable)
{
    const auto path = temporaryFile("novainfra-config-callback-error.toml", "value = 1\n");
    novainfra::config::Schema schema;
    schema.required<std::int64_t>("value");
    auto loaded = novainfra::config::ConfigManager::load(std::move(schema), {
                                                                                {path, true}
    });
    ASSERT_TRUE(loaded) << loaded.error().message();
    auto manager = std::move(loaded.value());
    ASSERT_TRUE(manager.startWatching(
        {std::chrono::milliseconds(10), std::chrono::milliseconds(0)},
        [](const auto &, const auto *) { throw std::runtime_error("callback failed"); }));
    replaceFile(path, "value = 2\n");
    ASSERT_TRUE(waitUntil([&] { return !manager.watching(); }));
    ASSERT_TRUE(manager.watchFailure());
    EXPECT_THROW(std::rethrow_exception(manager.watchFailure()), std::runtime_error);
    EXPECT_EQ(manager.snapshot().get<int>("value").value(), 2);
    manager.stopWatching();
    ASSERT_TRUE(manager.startWatching({std::chrono::milliseconds(10), std::chrono::milliseconds(0)}));
    EXPECT_FALSE(manager.watchFailure());
    manager.stopWatching();
    std::filesystem::remove(path);
}
#endif
