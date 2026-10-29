#pragma once

#include "schema.h"
#include "snapshot.h"
#include "types.h"

#include <core/macros.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

NOVAINFRA_NAMESPACE_BEGIN
namespace config {

class NOVAINFRA_LIB_PORT ConfigManager
{
public:
    using ChangeCallback = std::function<void(const Snapshot &, const Error *)>;

    ConfigManager();
    ConfigManager(ConfigManager &&) NOVAINFRA_NOEXCEPT = default;
    ConfigManager &operator=(ConfigManager &&other) NOVAINFRA_NOEXCEPT;
    ~ConfigManager();
    ConfigManager(const ConfigManager &) = delete;
    ConfigManager &operator=(const ConfigManager &) = delete;

    static Result<ConfigManager> load(Schema schema, std::vector<FileSpec> files = {},
                                      std::string envPrefix = {}, int argc = 0,
                                      const char *const *argv = nullptr);

    Result<void> reload();
    Snapshot snapshot() const;
    Result<void> startWatching(WatchOptions options = {}, ChangeCallback callback = {});
    void stopWatching();
    bool watching() const NOVAINFRA_NOEXCEPT;
    std::exception_ptr watchFailure() const NOVAINFRA_NOEXCEPT;

private:
    struct Impl;
    static Result<Snapshot> buildSnapshot(Impl &impl);
    explicit ConfigManager(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
    std::shared_ptr<Impl> impl_;
};

}  // namespace config
NOVAINFRA_NAMESPACE_END
