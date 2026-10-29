#include <logger/factories/async_factory.h>
#include <logger/sinks/basic_file_sink.h>
#include <logger/sinks/callback_sink.h>
#include <logger/logger.h>
#include <logger/log.h>
#include <logger/sinks/rotating_file_sink.h>
#include <core/os_utils.h>

#include <fstream>
#include <memory>
#include <mutex>
#include <string>

namespace {

class CountingSink NOVAINFRA_FINAL : public novainfra::log::sinks::ISink
{
public:
    void log(const novainfra::log::LogMsg &msg) NOVAINFRA_OVERRIDE
    {
        ++count;
        lastMessage = msg.message;
    }
    void setPattern(const std::string &) NOVAINFRA_OVERRIDE {}
    void setFormatter(std::unique_ptr<novainfra::log::formatters::IFormatter>) NOVAINFRA_OVERRIDE {}
    void flush() NOVAINFRA_OVERRIDE {}

    int count = 0;
    std::string lastMessage;
};

}  // namespace

int main()
{
    const std::string filename =
        "/tmp/novainfra_logger_no_exceptions_" + std::to_string(novainfra::core::pid()) + ".log";
    const std::string rotatingFilename =
        "/tmp/novainfra_rotating_no_exceptions_" + std::to_string(novainfra::core::pid()) + ".log";
    using RotatingSink = novainfra::log::sinks::RotatingFileSink<std::mutex>;
    const auto rotatingHistoryFilename = RotatingSink::calcFilename(rotatingFilename, 1);
    bool valid = false;
    bool asyncValid = false;
    bool callbackValid = false;
    bool registryValid = false;

    {
        auto countingSink = std::make_shared<CountingSink>();
        auto fileSink = std::make_shared<novainfra::log::sinks::BasicFileSink<std::mutex>>(filename, true);
        auto rotatingSink = std::make_shared<RotatingSink>(rotatingFilename, 10, 1);
        fileSink->setPattern("%v");
        rotatingSink->setPattern("%v");
        novainfra::log::Logger logger(
            "no-exceptions", novainfra::log::sinks::SinkInitList{countingSink, fileSink, rotatingSink});
        logger.info("message");
        logger.info("value {}", 7);
        logger.flush();

        std::ifstream input(filename);
        std::string firstLine;
        std::string secondLine;
        std::getline(input, firstLine);
        std::getline(input, secondLine);

        std::ifstream rotatingInput(rotatingFilename);
        std::string rotatingLine;
        std::getline(rotatingInput, rotatingLine);

        std::ifstream rotatingHistoryInput(rotatingHistoryFilename);
        std::string rotatingHistoryLine;
        std::getline(rotatingHistoryInput, rotatingHistoryLine);

        valid = countingSink->count == 2 && countingSink->lastMessage == "value 7" &&
                firstLine == "message" && secondLine == "value 7" &&
                input.peek() == std::ifstream::traits_type::eof() && rotatingLine == "value 7" &&
                rotatingInput.peek() == std::ifstream::traits_type::eof() &&
                rotatingHistoryLine == "message" &&
                rotatingHistoryInput.peek() == std::ifstream::traits_type::eof();
    }

    {
        int callbackCount = 0;
        auto callbackSink = std::make_shared<novainfra::log::sinks::CallbackSinkSt>(
            [&callbackCount](const novainfra::log::LogMsg &msg) {
                if (msg.message == "callback message") {
                    ++callbackCount;
                }
            });
        novainfra::log::Logger logger("callback-no-exceptions", callbackSink);
        logger.info("callback message");
        callbackValid = callbackCount == 1;
    }

    {
        novainfra::log::setWorker(std::make_shared<novainfra::log::async::AsyncLogThreadPool>(8, 1));
        auto asyncLogger =
            novainfra::log::factories::AsyncFactory::create<CountingSink>("async-no-exceptions");
        auto asyncSink = std::static_pointer_cast<CountingSink>(asyncLogger->sinks().front());
        asyncLogger->info("async message {}", 9);
        asyncLogger->flush();
        novainfra::log::shutdown();
        asyncValid = asyncSink->count == 1 && asyncSink->lastMessage == "async message 9";
    }

    {
        auto registrySink = std::make_shared<CountingSink>();
        auto registryLogger =
            std::make_shared<novainfra::log::Logger>("registry-no-exceptions", registrySink);
        novainfra::log::setDefaultLogger(registryLogger);
        NOVAINFRA_LOG_INFO("registry message {}", 10);
        registryValid = registrySink->count == 1 && registrySink->lastMessage == "registry message 10" &&
                        novainfra::log::defaultLogger() == registryLogger;
        novainfra::log::shutdown();
    }

    novainfra::core::removeIfExists(filename);
    novainfra::core::removeIfExists(rotatingFilename);
    novainfra::core::removeIfExists(rotatingHistoryFilename);
    return valid && asyncValid && callbackValid && registryValid ? 0 : 1;
}
