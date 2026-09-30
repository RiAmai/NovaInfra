#include <logger/registry.h>

#include <logger/async/async_log_thread_pool.h>
#include <logger/formatters/pattern_formatter.h>
#include <logger/logger.h>
#include <logger/sinks/console_sink.h>

#include <stdexcept>
#include <utility>

NOVAINFRA_NAMESPACE_BEGIN
namespace log {

namespace {

template <typename Apply>
void applyToLoggers(const std::vector<std::shared_ptr<Logger>> &loggers, Apply &&apply)
{
    for (const auto &logger : loggers) {
        apply(logger);
    }
}

}  // namespace

Registry::Registry() : formatter_(std::make_shared<formatters::PatternFormatter>())
{
    auto logger = makeDefaultLogger();
    logger->setLevel(globalLogLevel_);
    logger->setFlushLevel(globalFlushLevel_);
    logger->setFormatter(formatter_->clone());
    loggers_.emplace(logger->name(), logger);
    std::atomic_store_explicit(&defaultLogger_, std::move(logger), std::memory_order_release);
}

Registry::~Registry()
{
    std::shared_ptr<async::AsyncLogThreadPool> retiredWorker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retiredWorker = std::move(worker_);
    }
    retiredWorker.reset();

    Loggers retiredLoggers;
    std::shared_ptr<Logger> retiredDefault;
    std::shared_ptr<formatters::IFormatter> retiredFormatter;
    std::shared_ptr<const ErrHandler> retiredErrHandler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retiredLoggers = std::move(loggers_);
        retiredDefault = std::atomic_exchange_explicit(&defaultLogger_, std::shared_ptr<Logger>{},
                                                       std::memory_order_acq_rel);
        retiredFormatter = std::move(formatter_);
        retiredErrHandler = std::move(errHandler_);
    }
}

void Registry::validateLogger(const std::shared_ptr<Logger> &logger)
{
    if (!logger) {
        NOVAINFRA_THROW(std::invalid_argument("Registry logger must not be null"));
    }
}

std::shared_ptr<Logger> Registry::makeDefaultLogger()
{
    return std::make_shared<Logger>("", std::make_shared<sinks::ConsoleOutSinkMt>());
}

Registry::ConfigSnapshot Registry::configSnapshotLocked(const std::string &loggerName) const
{
    const auto levelIt = logLevels_.find(loggerName);
    const auto level = levelIt == logLevels_.end() ? globalLogLevel_ : levelIt->second;
    return ConfigSnapshot{level, globalFlushLevel_, formatter_, errHandler_, backtraceMsgNum_};
}

std::vector<std::shared_ptr<Logger>> Registry::loggerSnapshotLocked() const
{
    std::vector<std::shared_ptr<Logger>> snapshot;
    snapshot.reserve(loggers_.size());
    for (const auto &entry : loggers_) {
        snapshot.push_back(entry.second);
    }
    return snapshot;
}

void Registry::applyConfig(const std::shared_ptr<Logger> &logger, const ConfigSnapshot &config)
{
    logger->setLevel(config.level);
    logger->setFlushLevel(config.flushLevel);
    logger->setFormatter(config.formatter->clone());
    logger->setErrHandler(config.errHandler ? *config.errHandler : ErrHandler{});
    if (config.backtraceMsgNum == 0) {
        logger->disableBacktrace();
    } else {
        logger->enableBacktrace(config.backtraceMsgNum);
    }
}

void Registry::registerLogger(std::shared_ptr<Logger> newLogger)
{
    validateLogger(newLogger);
    std::lock_guard<std::mutex> updateLock(updateMutex_);

    ConfigSnapshot config;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (loggers_.find(newLogger->name()) != loggers_.end()) {
            NOVAINFRA_THROW(
                std::runtime_error("logger with name '" + newLogger->name() + "' already exists"));
        }
        config = configSnapshotLocked(newLogger->name());
    }

    applyConfig(newLogger, config);

    std::lock_guard<std::mutex> lock(mutex_);
    loggers_.emplace(newLogger->name(), std::move(newLogger));
}

void Registry::registerOrReplaceLogger(std::shared_ptr<Logger> newLogger)
{
    validateLogger(newLogger);
    std::lock_guard<std::mutex> updateLock(updateMutex_);

    ConfigSnapshot config;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config = configSnapshotLocked(newLogger->name());
    }
    applyConfig(newLogger, config);

    std::shared_ptr<Logger> retiredLogger;
    std::shared_ptr<Logger> retiredDefault;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto currentDefault = std::atomic_load_explicit(&defaultLogger_, std::memory_order_acquire);
        const bool replacesDefault = currentDefault && currentDefault->name() == newLogger->name();
        auto it = loggers_.find(newLogger->name());
        if (it == loggers_.end()) {
            loggers_.emplace(newLogger->name(), newLogger);
        } else {
            retiredLogger = std::move(it->second);
            it->second = newLogger;
        }
        if (replacesDefault) {
            retiredDefault =
                std::atomic_exchange_explicit(&defaultLogger_, newLogger, std::memory_order_acq_rel);
        }
    }
}

void Registry::initLogger(std::shared_ptr<Logger> newLogger)
{
    validateLogger(newLogger);
    std::lock_guard<std::mutex> updateLock(updateMutex_);

    ConfigSnapshot config;
    bool autoRegistration = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config = configSnapshotLocked(newLogger->name());
        autoRegistration = autoRegistration_;
        if (autoRegistration && loggers_.find(newLogger->name()) != loggers_.end()) {
            NOVAINFRA_THROW(
                std::runtime_error("logger with name '" + newLogger->name() + "' already exists"));
        }
    }

    applyConfig(newLogger, config);
    if (!autoRegistration) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (loggers_.find(newLogger->name()) != loggers_.end()) {
            NOVAINFRA_THROW(
                std::runtime_error("logger with name '" + newLogger->name() + "' already exists"));
        }
        loggers_.emplace(newLogger->name(), std::move(newLogger));
    }
}

void Registry::setDefaultLogger(std::shared_ptr<Logger> newDefaultLogger)
{
    validateLogger(newDefaultLogger);
    std::lock_guard<std::mutex> updateLock(updateMutex_);

    ConfigSnapshot config;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config = configSnapshotLocked(newDefaultLogger->name());
    }
    applyConfig(newDefaultLogger, config);

    std::shared_ptr<Logger> retiredLogger;
    std::shared_ptr<Logger> retiredDefault;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = loggers_.find(newDefaultLogger->name());
        if (it == loggers_.end()) {
            loggers_.emplace(newDefaultLogger->name(), newDefaultLogger);
        } else {
            retiredLogger = std::move(it->second);
            it->second = newDefaultLogger;
        }
        retiredDefault =
            std::atomic_exchange_explicit(&defaultLogger_, newDefaultLogger, std::memory_order_acq_rel);
    }
}

std::shared_ptr<Logger> Registry::defaultLogger() const NOVAINFRA_NOEXCEPT
{
    return std::atomic_load_explicit(&defaultLogger_, std::memory_order_acquire);
}

std::shared_ptr<Logger> Registry::getNameLogger(const std::string &name) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = loggers_.find(name);
    return it == loggers_.end() ? nullptr : it->second;
}

void Registry::setLevel(Level level)
{
    std::lock_guard<std::mutex> updateLock(updateMutex_);
    std::vector<std::shared_ptr<Logger>> loggers;
    LogLevels retiredLevels;
    Level effectiveGlobalLevel;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        globalLogLevel_ = level;
        retiredLevels = std::move(logLevels_);
        effectiveGlobalLevel = globalLogLevel_;
        loggers = loggerSnapshotLocked();
    }
    applyToLoggers(loggers,
                   [effectiveGlobalLevel](const auto &logger) { logger->setLevel(effectiveGlobalLevel); });
}

void Registry::setLevels(LogLevels levels, const Level *globalLevel)
{
    std::lock_guard<std::mutex> updateLock(updateMutex_);
    std::vector<std::shared_ptr<Logger>> loggers;
    LogLevels retiredLevels;
    LogLevels levelsSnapshot;
    Level effectiveGlobalLevel;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retiredLevels = std::move(logLevels_);
        logLevels_ = std::move(levels);
        if (globalLevel) {
            globalLogLevel_ = *globalLevel;
        }
        levelsSnapshot = logLevels_;
        effectiveGlobalLevel = globalLogLevel_;
        loggers = loggerSnapshotLocked();
    }
    applyToLoggers(loggers, [&levelsSnapshot, effectiveGlobalLevel](const auto &logger) {
        const auto it = levelsSnapshot.find(logger->name());
        logger->setLevel(it == levelsSnapshot.end() ? effectiveGlobalLevel : it->second);
    });
}

void Registry::setFlushLevel(Level level)
{
    std::lock_guard<std::mutex> updateLock(updateMutex_);
    std::vector<std::shared_ptr<Logger>> loggers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        globalFlushLevel_ = level;
        loggers = loggerSnapshotLocked();
    }
    applyToLoggers(loggers, [level](const auto &logger) { logger->setFlushLevel(level); });
}

void Registry::setFormatter(std::unique_ptr<formatters::IFormatter> formatter)
{
    if (!formatter) {
        NOVAINFRA_THROW(std::invalid_argument("Registry formatter must not be null"));
    }

    auto newFormatter = std::shared_ptr<formatters::IFormatter>(std::move(formatter));
    std::lock_guard<std::mutex> updateLock(updateMutex_);
    std::shared_ptr<formatters::IFormatter> retiredFormatter;
    std::shared_ptr<formatters::IFormatter> formatterSnapshot;
    std::vector<std::shared_ptr<Logger>> loggers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retiredFormatter = std::move(formatter_);
        formatter_ = std::move(newFormatter);
        formatterSnapshot = formatter_;
        loggers = loggerSnapshotLocked();
    }
    applyToLoggers(loggers, [&formatterSnapshot](const auto &logger) {
        logger->setFormatter(formatterSnapshot->clone());
    });
}

void Registry::setErrHandler(ErrHandler handler)
{
    auto newHandler = handler ? std::make_shared<const ErrHandler>(std::move(handler)) : nullptr;
    std::lock_guard<std::mutex> updateLock(updateMutex_);
    std::shared_ptr<const ErrHandler> retiredHandler;
    std::shared_ptr<const ErrHandler> handlerSnapshot;
    std::vector<std::shared_ptr<Logger>> loggers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retiredHandler = std::move(errHandler_);
        errHandler_ = std::move(newHandler);
        handlerSnapshot = errHandler_;
        loggers = loggerSnapshotLocked();
    }
    applyToLoggers(loggers, [&handlerSnapshot](const auto &logger) {
        logger->setErrHandler(handlerSnapshot ? *handlerSnapshot : ErrHandler{});
    });
}

void Registry::enableBacktrace(std::size_t messageCount)
{
    std::lock_guard<std::mutex> updateLock(updateMutex_);
    std::vector<std::shared_ptr<Logger>> loggers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        backtraceMsgNum_ = messageCount;
        loggers = loggerSnapshotLocked();
    }
    applyToLoggers(loggers, [messageCount](const auto &logger) {
        if (messageCount == 0) {
            logger->disableBacktrace();
        } else {
            logger->enableBacktrace(messageCount);
        }
    });
}

void Registry::disableBacktrace()
{
    enableBacktrace(0);
}

void Registry::setWorker(std::shared_ptr<async::AsyncLogThreadPool> worker)
{
    std::unique_lock<std::mutex> updateLock(updateMutex_);
    std::shared_ptr<async::AsyncLogThreadPool> retiredWorker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retiredWorker = std::move(worker_);
        worker_ = std::move(worker);
    }
    updateLock.unlock();
    retiredWorker.reset();
}

std::shared_ptr<async::AsyncLogThreadPool> Registry::worker() const NOVAINFRA_NOEXCEPT
{
    std::lock_guard<std::mutex> lock(mutex_);
    return worker_;
}

void Registry::setAutoRegistration(bool on)
{
    std::lock_guard<std::mutex> updateLock(updateMutex_);
    std::lock_guard<std::mutex> lock(mutex_);
    autoRegistration_ = on;
}

void Registry::applyAll(const std::function<void(std::shared_ptr<Logger>)> &func)
{
    if (!func) {
        NOVAINFRA_THROW(std::invalid_argument("Registry callback must not be empty"));
    }

    std::vector<std::shared_ptr<Logger>> loggers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loggers = loggerSnapshotLocked();
    }
    for (const auto &logger : loggers) {
        func(logger);
    }
}

void Registry::flushAll()
{
    std::vector<std::shared_ptr<Logger>> loggers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loggers = loggerSnapshotLocked();
    }
    for (const auto &logger : loggers) {
        logger->flush();
    }
}

void Registry::dropLogger(const std::string &name)
{
    std::lock_guard<std::mutex> updateLock(updateMutex_);
    std::shared_ptr<Logger> retiredLogger;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto currentDefault = std::atomic_load_explicit(&defaultLogger_, std::memory_order_acquire);
        if (currentDefault && currentDefault->name() == name) {
            NOVAINFRA_THROW(std::invalid_argument("The default logger cannot be dropped"));
        }
        const auto it = loggers_.find(name);
        if (it != loggers_.end()) {
            retiredLogger = std::move(it->second);
            loggers_.erase(it);
        }
    }
}

void Registry::dropAll()
{
    std::lock_guard<std::mutex> updateLock(updateMutex_);
    const auto currentDefault = defaultLogger();
    Loggers replacement;
    replacement.emplace(currentDefault->name(), currentDefault);

    Loggers retiredLoggers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retiredLoggers = std::move(loggers_);
        loggers_ = std::move(replacement);
    }
}

void Registry::shutdown()
{
    std::unique_lock<std::mutex> updateLock(updateMutex_);
    auto freshDefault = makeDefaultLogger();
    auto freshFormatter = std::make_shared<formatters::PatternFormatter>();
    freshDefault->setLevel(Level::INFO);
    freshDefault->setFlushLevel(Level::OFF);
    freshDefault->setFormatter(freshFormatter->clone());
    freshDefault->setErrHandler(ErrHandler{});
    Loggers freshLoggers;
    freshLoggers.emplace(freshDefault->name(), freshDefault);

    Loggers retiredLoggers;
    LogLevels retiredLevels;
    std::shared_ptr<Logger> retiredDefault;
    std::shared_ptr<formatters::IFormatter> retiredFormatter;
    std::shared_ptr<const ErrHandler> retiredErrHandler;
    std::shared_ptr<async::AsyncLogThreadPool> retiredWorker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retiredLoggers = std::move(loggers_);
        retiredLevels = std::move(logLevels_);
        retiredDefault =
            std::atomic_exchange_explicit(&defaultLogger_, freshDefault, std::memory_order_acq_rel);
        retiredFormatter = std::move(formatter_);
        formatter_ = std::move(freshFormatter);
        retiredErrHandler = std::move(errHandler_);
        retiredWorker = std::move(worker_);

        globalLogLevel_ = Level::INFO;
        globalFlushLevel_ = Level::OFF;
        autoRegistration_ = true;
        backtraceMsgNum_ = 0;

        loggers_ = std::move(freshLoggers);
    }

    // Destruction drains and joins the worker without holding either Registry lock.
    updateLock.unlock();
    retiredWorker.reset();
}

}  // namespace log
NOVAINFRA_NAMESPACE_END
