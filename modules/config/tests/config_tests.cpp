#include <config/config.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
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

void exerciseConcurrentStops(bool restart)
{
    const auto path = temporaryFile("novainfra-config-stop-race.toml", "value = 1\n");
    struct RemoveFile {
        std::filesystem::path path;
        ~RemoveFile()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{path};
    std::promise<void> entered;
    auto callbackEntered = entered.get_future();
    std::promise<void> release;
    auto canContinue = release.get_future().share();
    novainfra::config::Schema schema;
    schema.required<std::int64_t>("value");
    auto loaded = novainfra::config::ConfigManager::load(std::move(schema), {
                                                                                {path, true}
    });
    ASSERT_TRUE(loaded);
    auto manager = std::move(loaded.value());
    ASSERT_TRUE(manager.startWatching({std::chrono::milliseconds(1), std::chrono::milliseconds(0)},
                                      [&](const auto &, const auto *) {
                                          entered.set_value();
                                          canContinue.wait();
                                      }));
    replaceFile(path, "value = 2\n");
    if (callbackEntered.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
        release.set_value();
        manager.stopWatching();
        FAIL() << "watch callback did not start";
    }

    auto first = std::async(std::launch::async, [&] { manager.stopWatching(); });
    const bool stopRequested = waitUntil([&] { return !manager.watching(); });
    std::promise<void> secondEntered;
    auto secondStarted = secondEntered.get_future();
    auto second = std::async(std::launch::async, [&] {
        secondEntered.set_value();
        manager.stopWatching();
    });
    const auto secondStartedStatus = secondStarted.wait_for(std::chrono::seconds(2));
    // Neither stop may return while the old worker is still inside its callback.
    const auto firstBlocked = first.wait_for(std::chrono::milliseconds(0));
    const auto secondBlocked = second.wait_for(std::chrono::milliseconds(10));

    const novainfra::config::WatchOptions options{std::chrono::hours(1), std::chrono::milliseconds(0)};
    std::promise<bool> restartAttempted;
    auto firstRestart = restartAttempted.get_future();
    std::future<bool> restarted;
    bool restartRejected = false;
    if (restart) {
        restarted = std::async(std::launch::async, [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            bool started = bool(manager.startWatching(options));
            restartAttempted.set_value(!started);
            while (!started && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
                started = bool(manager.startWatching(options));
            }
            return started;
        });
        if (firstRestart.wait_for(std::chrono::seconds(2)) == std::future_status::ready)
            restartRejected = firstRestart.get();
    }

    release.set_value();
    bool restartCompleted = true;
    bool restartSucceeded = true;
    if (restart) {
        restartCompleted = restarted.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        restartSucceeded = restarted.get();
    }
    const auto firstCompleted = first.wait_for(std::chrono::seconds(2));
    const auto secondCompleted = second.wait_for(std::chrono::seconds(2));
    // A fresh stop request also releases a waiter that incorrectly joined the new worker.
    // Clean up before asserting so this regression fails without hanging future destructors.
    manager.stopWatching();
    first.get();
    second.get();

    EXPECT_TRUE(stopRequested);
    EXPECT_EQ(secondStartedStatus, std::future_status::ready);
    EXPECT_EQ(firstBlocked, std::future_status::timeout);
    EXPECT_EQ(secondBlocked, std::future_status::timeout);
    EXPECT_TRUE(restartCompleted);
    EXPECT_TRUE(restartSucceeded);
    if (restart)
        EXPECT_TRUE(restartRejected);
    ASSERT_EQ(firstCompleted, std::future_status::ready);
    ASSERT_EQ(secondCompleted, std::future_status::ready);
    ASSERT_TRUE(manager.startWatching(options));
    EXPECT_TRUE(manager.watching());
    manager.stopWatching();
    EXPECT_FALSE(manager.watching());
}

class EnvironmentVariable
{
public:
    EnvironmentVariable(const char *name, const char *value) : name_(name)
    {
        if (const char *previous = std::getenv(name))
            previous_ = previous;
        set(value);
    }

    ~EnvironmentVariable()
    {
        if (previous_)
            setenv(name_.c_str(), previous_->c_str(), 1);
        else
            unsetenv(name_.c_str());
    }

    void set(const char *value)
    {
        if (setenv(name_.c_str(), value, 1) != 0)
            std::abort();
    }

private:
    std::string name_;
    std::optional<std::string> previous_;
};
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

TEST(Config, LoadsEnvironmentAndAppliesSourcePrecedence)
{
    EnvironmentVariable port("NOVAINFRA_CONFIG_TEST_SERVER_PORT", "9090");
    EnvironmentVariable host("NOVAINFRA_CONFIG_TEST_SERVER_HOST", "environment");
    EnvironmentVariable tls("NOVAINFRA_CONFIG_TEST_SERVER_TLS", "on");
    const auto path =
        temporaryFile("novainfra-config-env.toml", "[server]\nport = 1234\nhost = \"file\"\ntls = false\n");
    novainfra::config::Schema schema;
    schema.required<std::int64_t>("server.port");
    schema.required<std::string>("server.host");
    schema.required<bool>("server.tls");
    const char *argv[] = {"test", "--server.host=command-line"};

    auto result = novainfra::config::ConfigManager::load(std::move(schema),
                                                         {
                                                             {path, true}
    },
                                                         "NOVAINFRA_CONFIG_TEST_", 2, argv);
    ASSERT_TRUE(result) << result.error().message();
    EXPECT_EQ(result.value().snapshot().get<int>("server.port").value(), 9090);
    EXPECT_EQ(result.value().snapshot().get<std::string>("server.host").value(), "command-line");
    EXPECT_TRUE(result.value().snapshot().get<bool>("server.tls").value());
    std::filesystem::remove(path);
}

TEST(Config, ReportsInvalidEnvironmentValue)
{
    EnvironmentVariable port("NOVAINFRA_CONFIG_TEST_SERVER_PORT", "not-a-number");
    novainfra::config::Schema schema;
    schema.required<std::int64_t>("server.port");

    auto result = novainfra::config::ConfigManager::load(std::move(schema), {}, "NOVAINFRA_CONFIG_TEST_");
    ASSERT_FALSE(result);
    ASSERT_FALSE(result.error().diagnostics.empty());
    EXPECT_EQ(result.error().diagnostics.front().code, novainfra::config::ErrorCode::TYPE_MISMATCH);
}

TEST(Config, ValidatesUnknownEnvironmentKeys)
{
    EnvironmentVariable plugin("NOVAINFRA_CONFIG_TEST_PLUGIN_ENABLED", "yes");
    novainfra::config::Schema strict;
    auto rejected = novainfra::config::ConfigManager::load(strict, {}, "NOVAINFRA_CONFIG_TEST_");
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().diagnostics.front().code, novainfra::config::ErrorCode::UNKNOWN_KEY);

    novainfra::config::Schema permissive;
    permissive.allowUnknown("plugin");
    auto allowed =
        novainfra::config::ConfigManager::load(std::move(permissive), {}, "NOVAINFRA_CONFIG_TEST_");
    ASSERT_TRUE(allowed) << allowed.error().message();
    EXPECT_EQ(allowed.value().snapshot().get<std::string>("plugin.enabled").value(), "yes");
}

TEST(Config, ReloadReadsUpdatedEnvironmentAndPreservesSnapshotOnFailure)
{
    EnvironmentVariable port("NOVAINFRA_CONFIG_TEST_SERVER_PORT", "1000");
    novainfra::config::Schema schema;
    schema.required<std::int64_t>("server.port");
    auto loaded = novainfra::config::ConfigManager::load(std::move(schema), {}, "NOVAINFRA_CONFIG_TEST_");
    ASSERT_TRUE(loaded) << loaded.error().message();
    auto manager = std::move(loaded.value());
    EXPECT_EQ(manager.snapshot().get<int>("server.port").value(), 1000);

    port.set("invalid");
    auto failed = manager.reload();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().diagnostics.front().code, novainfra::config::ErrorCode::TYPE_MISMATCH);
    EXPECT_EQ(manager.snapshot().get<int>("server.port").value(), 1000);

    port.set("2000");
    auto reloaded = manager.reload();
    ASSERT_TRUE(reloaded) << reloaded.error().message();
    EXPECT_EQ(manager.snapshot().get<int>("server.port").value(), 2000);
}

TEST(Config, LoadsTomlAndValidates)
{
    const auto path =
        temporaryFile("novainfra-config-test.toml", "[server]\nport = 1234\nhost = \"example\"\n");
    novainfra::config::Schema schema;
    schema.required<std::int64_t>("server.port", novainfra::config::FieldConstraints{1, 65535, {}, {}, {}});
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

TEST(Config, ConcurrentStopsWaitForActiveCallback)
{
    exerciseConcurrentStops(false);
}

TEST(Config, ConcurrentStopsAllowRestart)
{
    for (int iteration = 0; iteration < 100; ++iteration) {
        SCOPED_TRACE(iteration);
        ASSERT_NO_FATAL_FAILURE(exerciseConcurrentStops(true));
    }
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

TEST(Config, SnapshotsShareImmutableDataAndKeepOldValuesAfterReload)
{
    const auto path = temporaryFile("novainfra-shared-snapshot.toml", "value = 1\n");
    novainfra::config::Schema schema;
    schema.required<std::int64_t>("value");
    auto loaded = novainfra::config::ConfigManager::load(std::move(schema), {
                                                                                {path, true}
    });
    ASSERT_TRUE(loaded);
    auto manager = std::move(loaded.value());
    const auto before = manager.snapshot();
    const auto copy = before;
    EXPECT_EQ(&before.root(), &copy.root());
    EXPECT_EQ(&before.root(), &manager.snapshot().root());
    replaceFile(path, "value = 2\n");
    ASSERT_TRUE(manager.reload());
    EXPECT_EQ(before.get<int>("value").value(), 1);
    EXPECT_EQ(manager.snapshot().get<int>("value").value(), 2);
    std::filesystem::remove(path);
}

TEST(Config, RedactsAllSecretTypesAndSubtrees)
{
    novainfra::config::Schema schema;
    schema.optional<std::int64_t>("pin", 123456, {}, true);
    schema.optional<bool>("enabled", true, {}, true);
    schema.optional<std::string>("host", "localhost");
    schema.required<novainfra::config::Value>("credentials", {}, true);
    const auto path = temporaryFile("novainfra-secret.toml", "[credentials]\ncode = 987654\n");
    auto loaded = novainfra::config::ConfigManager::load(std::move(schema), {
                                                                                {path, true}
    });
    ASSERT_TRUE(loaded);
    auto snapshot = loaded.value().snapshot();
    EXPECT_EQ(snapshot.dump(), "{\"credentials\":\"<redacted>\",\"enabled\":\"<redacted>\",\"host\":\"<"
                               "redacted>\",\"pin\":\"<redacted>\"}");
    EXPECT_NE(snapshot.dump(false).find("123456"), std::string::npos);
    EXPECT_NE(snapshot.dump(false).find("987654"), std::string::npos);
    std::filesystem::remove(path);
}

TEST(Config, SerializesEscapedKeysAndStringControlCharacters)
{
    using novainfra::config::Value;
    Value value(Value::Object{
        {"a\"b", Value(std::string("x\\\n\t\r\b\f\x01", 8))}
    });
    EXPECT_EQ(value.toString(), "{\"a\\\"b\":\"x\\\\\\n\\t\\r\\b\\f\\u0001\"}");
}

TEST(Config, TomlNonnegativeIntegerSupportsUnsignedSchema)
{
    const auto path = temporaryFile("novainfra-unsigned.toml", "count = 9223372036854775807\n");
    novainfra::config::Schema schema;
    schema.required<std::uint64_t>("count");
    auto loaded = novainfra::config::ConfigManager::load(schema, {
                                                                     {path, true}
    });
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded.value().snapshot().find("count")->type(),
              novainfra::config::Value::Type::UNSIGNED_INTEGER);
    EXPECT_EQ(loaded.value().snapshot().get<std::uint64_t>("count").value(),
              static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
    replaceFile(path, "count = -1\n");
    EXPECT_FALSE(loaded.value().reload());
    std::filesystem::remove(path);
    novainfra::config::Value root(novainfra::config::Value::Object{});
    root["count"] = novainfra::config::Value(std::numeric_limits<std::uint64_t>::max());
    novainfra::config::Schema signedSchema;
    signedSchema.required<std::int64_t>("count");
    EXPECT_FALSE(signedSchema.validate(root));
}

TEST(Config, MoveAssignmentStopsOldWatcherAndReleasesCallback)
{
    novainfra::config::ConfigManager manager;
    auto capture = std::make_shared<int>(1);
    std::weak_ptr<int> weak = capture;
    ASSERT_TRUE(manager.startWatching({}, [capture](const auto &, const auto *) {}));
    capture.reset();
    manager = novainfra::config::ConfigManager{};
    EXPECT_TRUE(weak.expired());
    EXPECT_FALSE(manager.watching());
}

#ifndef NOVAINFRA_NO_EXCEPTIONS
TEST(Config, ValidatorExceptionStopsWatcherAndPreservesPublishedSnapshot)
{
    const auto path = temporaryFile("novainfra-validator-throw.toml", "value = 1\n");
    novainfra::config::Schema schema;
    schema.required<std::int64_t>("value", {}, false, [](const auto &value) -> std::optional<std::string> {
        if (std::get<std::int64_t>(value.storage()) == 2)
            throw std::runtime_error("validator failed");
        return std::nullopt;
    });
    auto loaded = novainfra::config::ConfigManager::load(std::move(schema), {
                                                                                {path, true}
    });
    ASSERT_TRUE(loaded);
    auto manager = std::move(loaded.value());
    ASSERT_TRUE(manager.startWatching({std::chrono::milliseconds(10), std::chrono::milliseconds(0)}));
    replaceFile(path, "value = 2\n");
    EXPECT_TRUE(waitUntil([&] { return !manager.watching(); }));
    EXPECT_TRUE(manager.watchFailure());
    EXPECT_EQ(manager.snapshot().get<int>("value").value(), 1);
    manager.stopWatching();
    std::filesystem::remove(path);
}
#endif


TEST(Config, DefaultDoesNotReplaceNonObjectParents)
{
    using namespace novainfra::config;
    for (const auto &parent :
         {Value("broken"), Value(std::int64_t(7)), Value(false), Value(Value::Array{}), Value()}) {
        for (const auto &path : {"server.port", "server.http.port"}) {
            Schema schema;
            schema.optional<std::int64_t>(path, 8080);
            Value root(Value::Object{
                {"server", parent}
            });
            const auto before = root.toString();
            auto result = schema.validate(root);
            ASSERT_FALSE(result) << path;
            EXPECT_EQ(result.error().diagnostics.front().code, ErrorCode::TYPE_MISMATCH);
            EXPECT_EQ(root.toString(), before);
        }
    }
    Schema schema;
    schema.optional<std::int64_t>("server.port", 8080);
    Value root("broken");
    EXPECT_FALSE(schema.validate(root));
    EXPECT_EQ(root.toString(), "\"broken\"");
}

TEST(Config, RedactsLiteralDottedSecretKeysAlongsideNestedKeys)
{
    using namespace novainfra::config;
    const auto path = temporaryFile("novainfra-literal-secret.toml",
                                    "\"credentials.pin\" = 112233\n"
                                    "[credentials]\npin = 445566\n\"token.code\" = 778899\n");
    Schema schema;
    schema.optional<std::int64_t>("credentials.pin", 0, {}, true);
    schema.optional<std::int64_t>("credentials.token.code", 0, {}, true);
    auto loaded = ConfigManager::load(std::move(schema), {
                                                             {path, true}
    });
    std::filesystem::remove(path);
    ASSERT_TRUE(loaded) << loaded.error().message();
    const auto snapshot = loaded.value().snapshot();
    for (const auto &secret : {"112233", "445566", "778899"}) {
        EXPECT_EQ(snapshot.dump().find(secret), std::string::npos);
        EXPECT_NE(snapshot.dump(false).find(secret), std::string::npos);
    }
}

TEST(Config, IntegerBoundsPreservePrecisionAndHandleRepresentationLimits)
{
    using namespace novainfra::config;
    const auto validate = [](Value value, double bound, bool minimum) {
        Schema schema;
        FieldConstraints constraints;
        if (minimum)
            constraints.min = bound;
        else
            constraints.max = bound;
        if (value.type() == Value::Type::UNSIGNED_INTEGER)
            schema.required<std::uint64_t>("value", constraints);
        else
            schema.required<std::int64_t>("value", constraints);
        Value root(Value::Object{
            {"value", std::move(value)}
        });
        return bool(schema.validate(root));
    };
    for (const bool unsignedValue : {false, true}) {
        const auto integer = [=](std::uint64_t value) {
            return unsignedValue ? Value(value) : Value(static_cast<std::int64_t>(value));
        };
        EXPECT_FALSE(validate(integer(9007199254740993ULL), 9007199254740992.0, false));
        EXPECT_FALSE(validate(integer(9007199254740995ULL), 9007199254740996.0, true));
        EXPECT_TRUE(validate(integer(9007199254740992ULL), 9007199254740992.0, false));
        EXPECT_TRUE(validate(integer(9007199254740996ULL), 9007199254740996.0, true));
        EXPECT_FALSE(validate(integer(1), 1.5, true));
        EXPECT_TRUE(validate(integer(1), 1.5, false));
        EXPECT_FALSE(validate(integer(2), 1.5, false));
        EXPECT_TRUE(validate(integer(2), 1.5, true));
        EXPECT_FALSE(validate(integer(0), -0.5, false));
        EXPECT_TRUE(validate(integer(0), -0.5, true));
        EXPECT_FALSE(validate(integer(0), std::numeric_limits<double>::quiet_NaN(), false));
        EXPECT_FALSE(validate(integer(0), std::numeric_limits<double>::infinity(), true));
        EXPECT_TRUE(validate(integer(0), std::numeric_limits<double>::infinity(), false));
    }
    EXPECT_FALSE(validate(Value(std::int64_t(-9007199254740993LL)), -9007199254740992.0, true));
    EXPECT_TRUE(validate(Value(std::int64_t(-1)), -1.5, true));
    EXPECT_FALSE(validate(Value(std::int64_t(-1)), -1.5, false));
    EXPECT_TRUE(validate(Value(std::numeric_limits<std::int64_t>::max()), std::ldexp(1.0, 63), false));
    EXPECT_FALSE(validate(Value(std::numeric_limits<std::int64_t>::max()), std::ldexp(1.0, 63), true));
    EXPECT_TRUE(validate(Value(std::numeric_limits<std::int64_t>::min()), -std::ldexp(1.0, 63), true));
    EXPECT_TRUE(validate(Value(std::numeric_limits<std::uint64_t>::max()), std::ldexp(1.0, 64), false));
    EXPECT_FALSE(validate(Value(std::numeric_limits<std::uint64_t>::max()), std::ldexp(1.0, 64), true));
    EXPECT_FALSE(validate(Value(std::numeric_limits<std::uint64_t>::max()),
                          std::nextafter(std::ldexp(1.0, 64), 0.0), false));
}

TEST(Config, FloatingConversionRejectsOverflowAndNonFiniteValues)
{
    using namespace novainfra::config;
    for (const double value :
         {1e100, -1e100, 1e-100, -1e-100, std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        Snapshot snapshot(Value(Value::Object{
            {"value", Value(value)}
        }));
        const auto result = snapshot.get<float>("value");
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().diagnostics.front().code, ErrorCode::TYPE_MISMATCH);
        EXPECT_EQ(bool(snapshot.get<double>("value")), std::isfinite(value));
    }
    for (const float value : {0.0f, 1.5f, std::numeric_limits<float>::max(),
                              -std::numeric_limits<float>::max(), std::numeric_limits<float>::denorm_min()}) {
        Snapshot snapshot(Value(Value::Object{
            {"value", Value(static_cast<double>(value))}
        }));
        ASSERT_TRUE(snapshot.get<float>("value"));
        EXPECT_EQ(snapshot.get<float>("value").value(), value);
    }
    Snapshot integer(Value(Value::Object{
        {"value", Value(std::numeric_limits<std::uint64_t>::max())}
    }));
    EXPECT_TRUE(integer.get<float>("value"));
}
