#pragma once

#include <logger/types.h>

#include <core/export.h>
#include <core/macros.h>
#include <core/namespace.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

NOVA_NAMESPACE_BEGIN
namespace log {

class Logger;
namespace async {
class AsyncLogThreadPool;
}
namespace formatters {
class IFormatter;
}

class NOVA_LIB_PORT Registry
{
public:
    using LogLevels = std::unordered_map<std::string, Level>;
    using Loggers = std::unordered_map<std::string, std::shared_ptr<Logger>>;

    NOVA_DISABLE_COPY_MOVE(Registry)

    static Registry &instance()
    {
        static Registry pvtInst;
        return pvtInst;
    }

    void registerLogger(std::shared_ptr<Logger> newLogger);
    void registerOrReplaceLogger(std::shared_ptr<Logger> newLogger);
    void initLogger(std::shared_ptr<Logger> newLogger);

    void setDefaultLogger(std::shared_ptr<Logger> newDefaultLogger);
    std::shared_ptr<Logger> defaultLogger() const noexcept;
    std::shared_ptr<Logger> getNameLogger(const std::string &name) const;

    void setLevel(Level level);
    void setLevels(LogLevels levels, const Level *globalLevel = nullptr);
    void setFlushLevel(Level level);
    void setFormatter(std::unique_ptr<formatters::IFormatter> formatter);
    void setErrHandler(ErrHandler handler);
    void enableBacktrace(std::size_t messageCount);
    void disableBacktrace();

    void setWorker(std::shared_ptr<async::AsyncLogThreadPool> worker);
    std::shared_ptr<async::AsyncLogThreadPool> worker() const noexcept;

    void setAutoRegistration(bool on);
    void applyAll(const std::function<void(std::shared_ptr<Logger>)> &func);
    void flushAll();

    void dropLogger(const std::string &name);
    void dropAll();

    // Drain the registry-owned worker, reset global configuration, and install a fresh default logger.
    void shutdown();

private:
    struct ConfigSnapshot {
        Level level;
        Level flushLevel;
        std::shared_ptr<formatters::IFormatter> formatter;
        std::shared_ptr<const ErrHandler> errHandler;
        std::size_t backtraceMsgNum;
        Uint64 revision;
    };

    Registry();
    ~Registry();

    static void validateLogger(const std::shared_ptr<Logger> &logger);
    static std::shared_ptr<Logger> makeDefaultLogger();

    ConfigSnapshot configSnapshotLocked(const std::string &loggerName) const;
    std::vector<std::shared_ptr<Logger>> loggerSnapshotLocked() const;
    static void applyConfig(const std::shared_ptr<Logger> &logger, const ConfigSnapshot &config);

    void applyLatestLevel(const std::shared_ptr<Logger> &logger);
    void applyLatestFlushLevel(const std::shared_ptr<Logger> &logger);
    void applyLatestFormatter(const std::shared_ptr<Logger> &logger);
    void applyLatestErrHandler(const std::shared_ptr<Logger> &logger);
    void applyLatestBacktrace(const std::shared_ptr<Logger> &logger);

    Loggers loggers_;
    LogLevels logLevels_;

    // Access this shared_ptr only through std::atomic_load/store/exchange free functions.
    std::shared_ptr<Logger> defaultLogger_;
    std::shared_ptr<formatters::IFormatter> formatter_;
    std::shared_ptr<const ErrHandler> errHandler_;
    std::shared_ptr<async::AsyncLogThreadPool> worker_;

    Level globalLogLevel_{Level::INFO};
    Level globalFlushLevel_{Level::OFF};
    bool autoRegistration_{true};

    Uint64 configRevision_{0};
    Uint64 levelRevision_{0};
    Uint64 flushLevelRevision_{0};
    Uint64 formatterRevision_{0};
    Uint64 errHandlerRevision_{0};
    Uint64 backtraceRevision_{0};
    std::size_t backtraceMsgNum_{0};

    mutable std::mutex mutex_;
};

}  // namespace log
NOVA_NAMESPACE_END
