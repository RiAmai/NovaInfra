#include <config/config_manager.h>

#define TOML_EXCEPTIONS 0
#include <toml.hpp>

#include <algorithm>
#include <condition_variable>
#include <core/macros.h>
#include <fstream>
#include <mutex>
#include <unordered_map>

extern char **environ;

NOVAINFRA_NAMESPACE_BEGIN
namespace config {

namespace {
Diagnostic diagnostic(ErrorCode code, const std::string &source, const std::string &message,
                      const std::string &key = {})
{
    return Diagnostic{Severity::ERROR, code, key, source, 0, 0, message};
}

Value fromToml(const toml::node &node)
{
    if (node.is_boolean())
        return Value(*node.value<bool>());
    if (node.is_integer())
        return Value(*node.value<std::int64_t>());
    if (node.is_floating_point())
        return Value(*node.value<double>());
    if (node.is_string())
        return Value(*node.value<std::string>());
    if (const auto array = node.as_array()) {
        Value::Array result;
        result.reserve(array->size());
        for (const auto &item : *array)
            result.push_back(fromToml(item));
        return Value(std::move(result));
    }
    if (const auto table = node.as_table()) {
        Value::Object result;
        for (const auto &[key, item] : *table)
            result.emplace(std::string(key), fromToml(item));
        return Value(std::move(result));
    }
    return Value{};
}

Result<Value> readToml(const FileSpec &file)
{
    const auto path = file.path.string();
    std::error_code ec;
    const auto status = std::filesystem::status(file.path, ec);
    if (status.type() == std::filesystem::file_type::not_found ||
        ec == std::errc::no_such_file_or_directory) {
        if (!file.required)
            return Value(Value::Object{});
        return Error(diagnostic(ErrorCode::FILE_NOT_FOUND, path, "configuration file does not exist"));
    }
    if (ec || !std::filesystem::is_regular_file(status))
        return Error(
            diagnostic(ErrorCode::FILE_READ, path, "configuration file is not a readable regular file"));
    std::ifstream stream(file.path);
    if (!stream)
        return Error(diagnostic(ErrorCode::FILE_READ, path, "configuration file could not be opened"));
    auto parsed = toml::parse(stream, path);
    if (stream.bad())
        return Error(diagnostic(ErrorCode::FILE_READ, path, "configuration file could not be read"));
    if (!parsed) {
        const auto &err = parsed.error();
        auto item = diagnostic(ErrorCode::PARSE, path, std::string(err.description()));
        item.line = err.source().begin.line;
        item.column = err.source().begin.column;
        return Error(std::move(item));
    }
    return fromToml(parsed.table());
}

Result<Value> readEnvironment(const std::string &prefix, const Schema &schema)
{
    Value root(Value::Object{});
    if (prefix.empty())
        return root;
    Error error;
    for (char **item = environ; item && *item; ++item) {
        std::string entry(*item);
        const auto separator = entry.find('=');
        if (separator == std::string::npos || entry.compare(0, prefix.size(), prefix) != 0)
            continue;
        auto key = entry.substr(prefix.size(), separator - prefix.size());
        std::replace(key.begin(), key.end(), '_', '.');
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (key.empty())
            error.diagnostics.push_back(
                diagnostic(ErrorCode::INVALID_VALUE, "environment", "empty environment key"));
        else {
            auto parsed = schema.parse(key, entry.substr(separator + 1));
            if (!parsed)
                error.diagnostics.insert(error.diagnostics.end(), parsed.error().diagnostics.begin(),
                                         parsed.error().diagnostics.end());
            else
                root[key] = std::move(parsed.value());
        }
    }
    return error.empty() ? Result<Value>(std::move(root)) : Result<Value>(std::move(error));
}

Result<Value> readCommandLine(int argc, const char *const *argv, const Schema &schema)
{
    Value root(Value::Object{});
    Error error;
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i] ? argv[i] : "");
        if (arg == "--")
            break;
        if (arg == "--set") {
            if (i + 1 >= argc) {
                error.diagnostics.push_back(
                    diagnostic(ErrorCode::INVALID_COMMAND_LINE, "command line", "--set requires key=value"));
                break;
            }
            arg = argv[++i];
        } else if (arg.rfind("--", 0) == 0) {
            arg.erase(0, 2);
            if (arg.rfind("no-", 0) == 0) {
                const auto key = arg.substr(3);
                auto parsed = schema.parse(key, "false");
                if (!parsed)
                    error.diagnostics.insert(error.diagnostics.end(), parsed.error().diagnostics.begin(),
                                             parsed.error().diagnostics.end());
                else
                    root[key] = std::move(parsed.value());
                continue;
            }
        } else
            continue;
        const auto equal = arg.find('=');
        if (equal == std::string::npos || equal == 0)
            error.diagnostics.push_back(
                diagnostic(ErrorCode::INVALID_COMMAND_LINE, "command line", "expected --key=value"));
        else {
            const auto key = arg.substr(0, equal);
            auto parsed = schema.parse(key, arg.substr(equal + 1));
            if (!parsed)
                error.diagnostics.insert(error.diagnostics.end(), parsed.error().diagnostics.begin(),
                                         parsed.error().diagnostics.end());
            else
                root[key] = std::move(parsed.value());
        }
    }
    return error.empty() ? Result<Value>(std::move(root)) : Result<Value>(std::move(error));
}

std::unordered_map<std::string, std::filesystem::file_time_type> fileTimes(const std::vector<FileSpec> &files)
{
    std::unordered_map<std::string, std::filesystem::file_time_type> result;
    for (const auto &file : files) {
        std::error_code ec;
        result[file.path.string()] = std::filesystem::last_write_time(file.path, ec);
    }
    return result;
}
}  // namespace

struct ConfigManager::Impl {
    Schema schema;
    std::vector<FileSpec> files;
    std::string envPrefix;
    int argc = 0;
    std::vector<std::string> argvStorage;
    std::mutex reloadMutex;
    mutable std::mutex watchMutex;
    std::condition_variable wake;
    std::shared_ptr<const Snapshot> current = std::make_shared<const Snapshot>();
    std::atomic<bool> watching{false};
    bool workerRunning = false;
    bool stopRequested = false;
    bool joining = false;
    std::thread watcher;
    WatchOptions watchOptions;
    ChangeCallback callback;
    std::exception_ptr failure;
    std::unordered_map<std::string, std::filesystem::file_time_type> times;

    ~Impl() { stop(); }
    void stop()
    {
        std::unique_lock<std::mutex> lock(watchMutex);
        stopRequested = true;
        watching.store(false);
        wake.notify_all();
        if (!watcher.joinable())
            return;
        if (watcher.get_id() == std::this_thread::get_id()) {
            watcher.detach();
            return;
        }
        joining = true;
        auto thread = std::move(watcher);
        lock.unlock();
        thread.join();
        lock.lock();
        joining = false;
    }
};

ConfigManager::ConfigManager() : impl_(std::make_shared<Impl>()) {}
ConfigManager::~ConfigManager()
{
    if (impl_)
        impl_->stop();
}

Result<Snapshot> ConfigManager::buildSnapshot(ConfigManager::Impl &impl)
{
    Value root(Value::Object{});
    for (const auto &file : impl.files) {
        auto result = readToml(file);
        if (!result)
            return result.error();
        root.merge(result.value());
    }
    auto environment = readEnvironment(impl.envPrefix, impl.schema);
    if (!environment)
        return environment.error();
    root.merge(environment.value());
    if (impl.argc > 0) {
        std::vector<const char *> pointers;
        pointers.reserve(impl.argvStorage.size());
        for (const auto &arg : impl.argvStorage)
            pointers.push_back(arg.c_str());
        auto commandLine = readCommandLine(impl.argc, pointers.data(), impl.schema);
        if (!commandLine)
            return commandLine.error();
        root.merge(commandLine.value());
    }
    auto validation = impl.schema.validate(root);
    if (!validation)
        return validation.error();
    return Snapshot(std::move(root));
}

Result<ConfigManager> ConfigManager::load(Schema schema, std::vector<FileSpec> files, std::string envPrefix,
                                          int argc, const char *const *argv)
{
    auto impl = std::make_shared<Impl>();
    impl->schema = std::move(schema);
    impl->files = std::move(files);
    impl->envPrefix = std::move(envPrefix);
    impl->argc = argc;
    if (argv)
        for (int i = 0; i < argc; ++i)
            impl->argvStorage.emplace_back(argv[i] ? argv[i] : "");
    auto snapshot = buildSnapshot(*impl);
    if (!snapshot)
        return snapshot.error();
    std::atomic_store(&impl->current, std::make_shared<const Snapshot>(std::move(snapshot.value())));
    return ConfigManager(std::move(impl));
}

Result<void> ConfigManager::reload()
{
    if (!impl_)
        return Error(diagnostic(ErrorCode::RELOAD_FAILED, "manager", "configuration manager is empty"));
    std::lock_guard<std::mutex> lock(impl_->reloadMutex);
    auto built = buildSnapshot(*impl_);
    if (!built)
        return built.error();
    std::atomic_store(&impl_->current, std::make_shared<const Snapshot>(std::move(built.value())));
    return {};
}

Snapshot ConfigManager::snapshot() const
{
    return impl_ ? *std::atomic_load(&impl_->current) : Snapshot{};
}

Result<void> ConfigManager::startWatching(WatchOptions options, ChangeCallback callback)
{
    if (!impl_)
        return Error(diagnostic(ErrorCode::RELOAD_FAILED, "manager", "configuration manager is empty"));
    if (options.interval <= std::chrono::milliseconds::zero() ||
        options.debounce < std::chrono::milliseconds::zero())
        return Error(diagnostic(ErrorCode::INVALID_VALUE, "watch options",
                                "interval must be positive and debounce nonnegative"));
    std::unique_lock<std::mutex> lock(impl_->watchMutex);
    if (impl_->joining)
        return Error(diagnostic(ErrorCode::RELOAD_FAILED, "manager", "previous watcher is still stopping"));
    if (impl_->workerRunning)
        return impl_->watching.load() ? Result<void>()
                                      : Result<void>(Error(diagnostic(ErrorCode::RELOAD_FAILED, "manager",
                                                                      "previous watcher is still stopping")));
    if (impl_->watcher.joinable())
        impl_->watcher.join();
    impl_->watchOptions = options;
    impl_->callback = std::move(callback);
    impl_->times = fileTimes(impl_->files);
    impl_->failure = nullptr;
    impl_->stopRequested = false;
    impl_->workerRunning = true;
    impl_->watching.store(true);
    NOVAINFRA_TRY
    {
        impl_->watcher = std::thread([impl = impl_] {
            std::unique_lock<std::mutex> lock(impl->watchMutex);
            while (!impl->stopRequested) {
                if (impl->wake.wait_for(lock, impl->watchOptions.interval,
                                        [&] { return impl->stopRequested; }))
                    break;
                lock.unlock();
                const auto now = fileTimes(impl->files);
                lock.lock();
                if (impl->stopRequested)
                    break;
                if (now == impl->times)
                    continue;
                if (impl->wake.wait_for(lock, impl->watchOptions.debounce,
                                        [&] { return impl->stopRequested; }))
                    break;
                lock.unlock();
                const auto stable = fileTimes(impl->files);
                lock.lock();
                if (impl->stopRequested)
                    break;
                impl->times = stable;
                lock.unlock();
                std::unique_lock<std::mutex> reloadLock(impl->reloadMutex);
                auto result = ConfigManager::buildSnapshot(*impl);
                std::shared_ptr<const Snapshot> published;
                if (result) {
                    published = std::make_shared<const Snapshot>(std::move(result.value()));
                    std::atomic_store(&impl->current, published);
                }
                reloadLock.unlock();
                if (impl->callback) {
                    auto current = published ? published : std::atomic_load(&impl->current);
                    NOVAINFRA_TRY
                    {
                        impl->callback(*current, result ? nullptr : &result.error());
                    }
                    NOVAINFRA_CATCH_ALL_BODY({
                        std::lock_guard<std::mutex> guard(impl->watchMutex);
                        impl->failure = std::current_exception();
                        impl->stopRequested = true;
                        impl->watching.store(false);
                        impl->wake.notify_all();
                    })
                }
                lock.lock();
            }
            impl->workerRunning = false;
            impl->watching.store(false);
            impl->wake.notify_all();
        });
    }
    NOVAINFRA_CATCH_ALL_BODY({
        impl_->workerRunning = false;
        impl_->watching.store(false);
        impl_->stopRequested = true;
        return Error(diagnostic(ErrorCode::RELOAD_FAILED, "manager", "watcher thread could not be started"));
    })
    return {};
}

void ConfigManager::stopWatching()
{
    if (impl_)
        impl_->stop();
}
bool ConfigManager::watching() const noexcept
{
    return impl_ && impl_->watching.load();
}

std::exception_ptr ConfigManager::watchFailure() const noexcept
{
    if (!impl_)
        return nullptr;
    std::lock_guard<std::mutex> lock(impl_->watchMutex);
    return impl_->failure;
}

}  // namespace config
NOVAINFRA_NAMESPACE_END
