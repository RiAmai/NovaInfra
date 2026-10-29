#include <logger/logger.h>

#include <core/os_utils.h>
#include <mutex>
#include <stdexcept>

NOVAINFRA_NAMESPACE_BEGIN
namespace log {

Logger::Logger(const Logger &other)
    : name_(other.name_), level_(other.level_.load(std::memory_order_relaxed)),
      flushLevel_(other.flushLevel_.load(std::memory_order_relaxed)), sinks_(other.sinks_),
      cstErrHandler_(std::atomic_load_explicit(&other.cstErrHandler_, std::memory_order_acquire)),
      tracer_(other.tracer_)
{
}

Logger::Logger(Logger &&other) NOVAINFRA_NOEXCEPT
    : name_(std::move(other.name_)),
      level_(other.level_.load(std::memory_order_relaxed)),
      flushLevel_(other.flushLevel_.load(std::memory_order_relaxed)),
      sinks_(std::move(other.sinks_)),
      cstErrHandler_(std::atomic_exchange_explicit(&other.cstErrHandler_, std::shared_ptr<const ErrHandler>{},
                                                   std::memory_order_acq_rel)),
      tracer_(std::move(other.tracer_))
{
}

void Logger::validateSinks() const
{
    for (const auto &sink : sinks_) {
        if (!sink) {
            NOVAINFRA_THROW(std::invalid_argument("Logger sink must not be null"));
        }
    }
}

void Logger::_flush()
{
    for (auto &sink : sinks_) {
        if (!sink) {
            errHandler("Logger sink is null");
            continue;
        }
        NOVAINFRA_TRY
        {
            sink->flush();
        }
        NOVAINFRA_LOG_DETAIL_CATCH(SourceLocation())
    }
}

void Logger::setErrHandler(ErrHandler handler)
{
    auto newHandler = handler ? std::make_shared<const ErrHandler>(std::move(handler)) : nullptr;
    auto retiredHandler =
        std::atomic_exchange_explicit(&cstErrHandler_, std::move(newHandler), std::memory_order_acq_rel);
}

std::shared_ptr<Logger> Logger::clone(std::string name)
{
    auto cloned = std::make_shared<Logger>(*this);
    cloned->name_ = std::move(name);
    return cloned;
}

void Logger::sinkIt(const detail::LogMsg &msg)
{
    for (auto &sink : sinks_) {
        if (!sink) {
            errHandler("Logger sink is null");
            continue;
        }
        if (sink->shouldLog(msg.level)) {
            NOVAINFRA_TRY
            {
                sink->log(msg);
            }
            NOVAINFRA_LOG_DETAIL_CATCH(msg.source)
        }
    }
    if (shouldFlush(msg.level)) {
        flush();
    }
}

void Logger::errHandler(const std::string &msg) NOVAINFRA_NOEXCEPT
{
    const auto handler = std::atomic_load_explicit(&cstErrHandler_, std::memory_order_acquire);
    if (handler) {
        NOVAINFRA_TRY
        {
            (*handler)(msg);
        }
        NOVAINFRA_CATCH_ALL_BODY({
            std::fputs("[*** LOG ERROR HANDLER FAILURE ***] custom error handler threw an exception\n",
                       stderr);
        })
    } else {
        static std::mutex mutex;
        static SystemTimePoint lastReportTime;
        static size_t errCnt = 0;
        std::lock_guard<std::mutex> lk{mutex};
        auto now = SystemClock::now();
        errCnt++;
        if (now - lastReportTime < std::chrono::seconds(1)) {
            return;
        }
        lastReportTime = now;
        auto tmTime = core::localTime(SystemClock::to_time_t(now));
        char dateBuf[64];
        std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d %H:%M:%S", &tmTime);
        std::fprintf(stderr, "[*** LOG ERROR #%04zu ***] [%s] [%s] %s\n", errCnt, dateBuf, name_.c_str(),
                     msg.c_str());
    }
}
}  // namespace log
NOVAINFRA_NAMESPACE_END
