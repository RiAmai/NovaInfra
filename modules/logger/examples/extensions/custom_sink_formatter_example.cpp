#include <logger/sinks/base_sink.h>
#include <logger/logger.h>

#include <fmt/format.h>

#include <cstddef>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace novainfra_log = novainfra::log;

namespace {

std::string_view levelName(novainfra_log::Level level)
{
    switch (level) {
        case novainfra_log::Level::TRACE:
            return "TRACE";
        case novainfra_log::Level::DEBUG:
            return "DEBUG";
        case novainfra_log::Level::INFO:
            return "INFO";
        case novainfra_log::Level::WARN:
            return "WARN";
        case novainfra_log::Level::ERROR:
            return "ERROR";
        case novainfra_log::Level::CRITICAL:
            return "CRITICAL";
        case novainfra_log::Level::OFF:
            return "OFF";
    }
    return "UNKNOWN";
}

class KeyValueFormatter NOVAINFRA_FINAL : public novainfra::log::formatters::IFormatter
{
public:
    void format(const novainfra_log::LogMsg &msg, std::string &dst) NOVAINFRA_OVERRIDE
    {
        fmt::format_to(std::back_inserter(dst), "level={} logger={} tid={} message={}\n",
                       levelName(msg.level), msg.loggerName, msg.threadId, msg.message);
    }

    std::unique_ptr<novainfra::log::formatters::IFormatter> clone() const NOVAINFRA_OVERRIDE
    {
        return std::make_unique<KeyValueFormatter>();
    }
};

class MemorySink NOVAINFRA_FINAL : public novainfra::log::sinks::BaseSink<std::mutex>
{
public:
    std::vector<std::string> records() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return records_;
    }

    std::size_t flushCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return flushCount_;
    }

private:
    void _log(const novainfra_log::LogMsg &msg) NOVAINFRA_OVERRIDE { records_.push_back(formatMessage(msg)); }
    void _flush() NOVAINFRA_OVERRIDE { ++flushCount_; }

    std::vector<std::string> records_;
    std::size_t flushCount_{0};
};

}  // namespace

int main()
{
    auto allMessages = std::make_shared<MemorySink>();
    auto warningsOnly = std::make_shared<MemorySink>();
    warningsOnly->setLevel(novainfra_log::Level::WARN);

    novainfra_log::Logger logger("CustomExtension",
                                 novainfra::log::sinks::SinkInitList{allMessages, warningsOnly});
    logger.setFormatter(std::make_unique<KeyValueFormatter>());
    logger.info("request {} accepted", 17);
    logger.warn("request {} is slow", 18);
    logger.flush();

    const auto allRecords = allMessages->records();
    const auto warningRecords = warningsOnly->records();
    for (const auto &record : allRecords) {
        std::cout << record;
    }

    const bool valid = allRecords.size() == 2 && warningRecords.size() == 1 &&
                       warningRecords.front().find("level=WARN") != std::string::npos &&
                       allMessages->flushCount() == 1 && warningsOnly->flushCount() == 1;
    return valid ? 0 : 1;
}
