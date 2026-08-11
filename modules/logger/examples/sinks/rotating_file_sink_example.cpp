#include <logger/logger.h>
#include <logger/sinks/rotating_file_sink.h>
#include <core/os_utils.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

namespace nova_log = nova::log;

int main()
{
    constexpr auto kLogFile = "/tmp/novacore_rotating_example.log";
    constexpr std::size_t kMaxFileSize = 128;
    constexpr std::size_t kMaxHistoryFiles = 3;
    using RotatingSink = nova::log::sinks::RotatingFileSink<std::mutex>;

    for (std::size_t index = 0; index <= kMaxHistoryFiles; ++index) {
        const auto filename = RotatingSink::calcFilename(kLogFile, index);
        if (nova::core::RemoveIfExists(filename) != 0) {
            std::cerr << "Failed removing old example log: " << filename << '\n';
            return 1;
        }
    }

    {
        auto sink = std::make_shared<RotatingSink>(kLogFile, kMaxFileSize, kMaxHistoryFiles);
        sink->setPattern("[%l] %v");
        nova_log::Logger logger("RotatingExample", sink);
        const std::string payload(40, 'x');

        for (int index = 0; index < 12; ++index) {
            logger.info("record {:02d}: {}", index, payload);
        }
        logger.flush();
    }

    std::cout << "Rotating log files:\n";
    for (std::size_t index = 0; index <= kMaxHistoryFiles; ++index) {
        const auto filename = RotatingSink::calcFilename(kLogFile, index);
        if (nova::core::PathExists(filename)) {
            std::cout << filename << " (" << std::filesystem::file_size(filename) << " bytes)\n";
        }
    }

    return 0;
}
