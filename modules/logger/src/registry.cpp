#include <logger/registry.h>

#include <logger/async/async_log_thread_pool.h>
#include <logger/formatters/pattern_formatter.h>
#include <logger/logger.h>
#include <logger/sinks/console_sink.h>

#include <stdexcept>
#include <utility>

NOVA_NAMESPACE_BEGIN
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
        NOVA_THROW(std::invalid_argument("Registry logger must not be null"));
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
    return ConfigSnapshot{level,       globalFlushLevel_, formatter_,
                          errHandler_, backtraceMsgNum_,  configRevision_};
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

    for (;;) {
        ConfigSnapshot config;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (loggers_.find(newLogger->name()) != loggers_.end()) {
                NOVA_THROW(std::runtime_error("logger with name '" + newLogger->name() + "' already exists"));
            }
            config = configSnapshotLocked(newLogger->name());
        }

        applyConfig(newLogger, config);

        std::lock_guard<std::mutex> lock(mutex_);
        if (configRevision_ != config.revision) {
            continue;
        }
        if (loggers_.find(newLogger->name()) != loggers_.end()) {
            NOVA_THROW(std::runtime_error("logger with name '" + newLogger->name() + "' already exists"));
        }
        loggers_.emplace(newLogger->name(), std::move(newLogger));
        return;
    }
}

void Registry::registerOrReplaceLogger(std::shared_ptr<Logger> newLogger)
{
    validateLogger(newLogger);

    for (;;) {
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
            if (configRevision_ != config.revision) {
                continue;
            }

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
        return;
    }
}

void Registry::initLogger(std::shared_ptr<Logger> newLogger)
{
    validateLogger(newLogger);

    for (;;) {
        ConfigSnapshot config;
        bool autoRegistration = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            config = configSnapshotLocked(newLogger->name());
            autoRegistration = autoRegistration_;
            if (autoRegistration && loggers_.find(newLogger->name()) != loggers_.end()) {
                NOVA_THROW(std::runtime_error("logger with name '" + newLogger->name() + "' already exists"));
            }
        }

        applyConfig(newLogger, config);

        std::lock_guard<std::mutex> lock(mutex_);
        if (configRevision_ != config.revision || autoRegistration_ != autoRegistration) {
            continue;
        }
        if (!autoRegistration) {
            return;
        }
        if (loggers_.find(newLogger->name()) != loggers_.end()) {
            NOVA_THROW(std::runtime_error("logger with name '" + newLogger->name() + "' already exists"));
        }
        loggers_.emplace(newLogger->name(), std::move(newLogger));
        return;
    }
}

void Registry::setDefaultLogger(std::shared_ptr<Logger> newDefaultLogger)
{
    validateLogger(newDefaultLogger);

    for (;;) {
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
            if (configRevision_ != config.revision) {
                continue;
            }

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
        return;
    }
}

std::shared_ptr<Logger> Registry::defaultLogger() const noexcept
{
    return std::atomic_load_explicit(&defaultLogger_, std::memory_order_acquire);
}

std::shared_ptr<Logger> Registry::getNameLogger(const std::string &name) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = loggers_.find(name);
    return it == loggers_.end() ? nullptr : it->second;
}

void Registry::applyLatestLevel(const std::shared_ptr<Logger> &logger)
{
    for (;;) {
        Level level;
        Uint64 revision;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = logLevels_.find(logger->name());
            level = it == logLevels_.end() ? globalLogLevel_ : it->second;
            revision = levelRevision_;
        }
        logger->setLevel(level);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (revision == levelRevision_) {
                return;
            }
        }
    }
}

void Registry::applyLatestFlushLevel(const std::shared_ptr<Logger> &logger)
{
    for (;;) {
        Level level;
        Uint64 revision;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            level = globalFlushLevel_;
            revision = flushLevelRevision_;
        }
        logger->setFlushLevel(level);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (revision == flushLevelRevision_) {
                return;
            }
        }
    }
}

void Registry::applyLatestFormatter(const std::shared_ptr<Logger> &logger)
{
    for (;;) {
        std::shared_ptr<formatters::IFormatter> formatter;
        Uint64 revision;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            formatter = formatter_;
            revision = formatterRevision_;
        }
        logger->setFormatter(formatter->clone());
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (revision == formatterRevision_) {
                return;
            }
        }
    }
}

void Registry::applyLatestErrHandler(const std::shared_ptr<Logger> &logger)
{
    for (;;) {
        std::shared_ptr<const ErrHandler> handler;
        Uint64 revision;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            handler = errHandler_;
            revision = errHandlerRevision_;
        }
        logger->setErrHandler(handler ? *handler : ErrHandler{});
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (revision == errHandlerRevision_) {
                return;
            }
        }
    }
}

void Registry::applyLatestBacktrace(const std::shared_ptr<Logger> &logger)
{
    for (;;) {
        std::size_t messageCount;
        Uint64 revision;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            messageCount = backtraceMsgNum_;
            revision = backtraceRevision_;
        }
        if (messageCount == 0) {
            logger->disableBacktrace();
        } else {
            logger->enableBacktrace(messageCount);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (revision == backtraceRevision_) {
                return;
            }
        }
    }
}

void Registry::setLevel(Level level)
{
    std::vector<std::shared_ptr<Logger>> loggers;
    LogLevels retiredLevels;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        globalLogLevel_ = level;
        retiredLevels = std::move(logLevels_);
        ++levelRevision_;
        ++configRevision_;
        loggers = loggerSnapshotLocked();
    }
    applyToLoggers(loggers, [this](const auto &logger) { applyLatestLevel(logger); });
}

void Registry::setLevels(LogLevels levels, const Level *globalLevel)
{
    std::vector<std::shared_ptr<Logger>> loggers;
    LogLevels retiredLevels;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retiredLevels = std::move(logLevels_);
        logLevels_ = std::move(levels);
        if (globalLevel) {
            globalLogLevel_ = *globalLevel;
        }
        ++levelRevision_;
        ++configRevision_;
        loggers = loggerSnapshotLocked();
    }
    applyToLoggers(loggers, [this](const auto &logger) { applyLatestLevel(logger); });
}

void Registry::setFlushLevel(Level level)
{
    std::vector<std::shared_ptr<Logger>> loggers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        globalFlushLevel_ = level;
        ++flushLevelRevision_;
        ++configRevision_;
        loggers = loggerSnapshotLocked();
    }
    applyToLoggers(loggers, [this](const auto &logger) { applyLatestFlushLevel(logger); });
}

void Registry::setFormatter(std::unique_ptr<formatters::IFormatter> formatter)
{
    if (!formatter) {
        NOVA_THROW(std::invalid_argument("Registry formatter must not be null"));
    }

    auto newFormatter = std::shared_ptr<formatters::IFormatter>(std::move(formatter));
    std::shared_ptr<formatters::IFormatter> retiredFormatter;
    std::vector<std::shared_ptr<Logger>> loggers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retiredFormatter = std::move(formatter_);
        formatter_ = std::move(newFormatter);
        ++formatterRevision_;
        ++configRevision_;
        loggers = loggerSnapshotLocked();
    }
    applyToLoggers(loggers, [this](const auto &logger) { applyLatestFormatter(logger); });
}

void Registry::setErrHandler(ErrHandler handler)
{
    auto newHandler = handler ? std::make_shared<const ErrHandler>(std::move(handler)) : nullptr;
    std::shared_ptr<const ErrHandler> retiredHandler;
    std::vector<std::shared_ptr<Logger>> loggers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retiredHandler = std::move(errHandler_);
        errHandler_ = std::move(newHandler);
        ++errHandlerRevision_;
        ++configRevision_;
        loggers = loggerSnapshotLocked();
    }
    applyToLoggers(loggers, [this](const auto &logger) { applyLatestErrHandler(logger); });
}

void Registry::enableBacktrace(std::size_t messageCount)
{
    std::vector<std::shared_ptr<Logger>> loggers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        backtraceMsgNum_ = messageCount;
        ++backtraceRevision_;
        ++configRevision_;
        loggers = loggerSnapshotLocked();
    }
    applyToLoggers(loggers, [this](const auto &logger) { applyLatestBacktrace(logger); });
}

void Registry::disableBacktrace()
{
    enableBacktrace(0);
}

void Registry::setWorker(std::shared_ptr<async::AsyncLogThreadPool> worker)
{
    std::shared_ptr<async::AsyncLogThreadPool> retiredWorker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retiredWorker = std::move(worker_);
        worker_ = std::move(worker);
    }
}

std::shared_ptr<async::AsyncLogThreadPool> Registry::worker() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return worker_;
}

void Registry::setAutoRegistration(bool on)
{
    std::lock_guard<std::mutex> lock(mutex_);
    autoRegistration_ = on;
}

void Registry::applyAll(const std::function<void(std::shared_ptr<Logger>)> &func)
{
    if (!func) {
        NOVA_THROW(std::invalid_argument("Registry callback must not be empty"));
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
    std::shared_ptr<Logger> retiredLogger;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto currentDefault = std::atomic_load_explicit(&defaultLogger_, std::memory_order_acquire);
        if (currentDefault && currentDefault->name() == name) {
            NOVA_THROW(std::invalid_argument("The default logger cannot be dropped"));
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
    for (;;) {
        const auto currentDefault = defaultLogger();
        Loggers replacement;
        replacement.emplace(currentDefault->name(), currentDefault);

        Loggers retiredLoggers;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (std::atomic_load_explicit(&defaultLogger_, std::memory_order_acquire) != currentDefault) {
                continue;
            }
            retiredLoggers = std::move(loggers_);
            loggers_ = std::move(replacement);
        }
        return;
    }
}

void Registry::shutdown()
{
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
        ++levelRevision_;
        ++flushLevelRevision_;
        ++formatterRevision_;
        ++errHandlerRevision_;
        ++backtraceRevision_;
        ++configRevision_;

        backtraceMsgNum_ = 0;

        loggers_ = std::move(freshLoggers);
    }

    // Destruction drains and joins the worker without holding the registry mutex.
    retiredWorker.reset();
}

}  // namespace log
NOVA_NAMESPACE_END
