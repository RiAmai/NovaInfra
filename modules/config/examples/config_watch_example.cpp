#include <config/config.h>

#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, const char **argv)
{
    using namespace std::chrono_literals;

    // 定义配置 Schema
    novainfra::config::Schema schema;
    schema.optional<std::int64_t>("server.port", 8080,
                                  novainfra::config::FieldConstraints{1, 65535, {}, {}, {}});
    schema.optional<std::string>("server.host", "127.0.0.1");
    schema.optional<bool>("server.debug", false);
    schema.optional<std::int64_t>("server.max_connections", 100,
                                  novainfra::config::FieldConstraints{1, 10000, {}, {}, {}});

    // 加载配置
    auto loaded = novainfra::config::ConfigManager::load(std::move(schema),
                                                         {
                                                             {"config.toml", false}
    },
                                                         "APP_", argc, argv);

    if (!loaded) {
        std::cerr << "Failed to load config: " << loaded.error().message() << '\n';
        return 1;
    }

    auto manager = std::move(loaded.value());

    // 打印初始配置
    auto printConfig = [](const novainfra::config::Snapshot &snapshot) {
        std::cout << "\n=== Current Configuration ===\n";
        std::cout << "Host: " << snapshot.get<std::string>("server.host").value() << '\n';
        std::cout << "Port: " << snapshot.get<int>("server.port").value() << '\n';
        std::cout << "Debug: " << (snapshot.get<bool>("server.debug").value() ? "ON" : "OFF") << '\n';
        std::cout << "Max Connections: " << snapshot.get<int>("server.max_connections").value() << '\n';
        std::cout << "============================\n\n";
    };

    printConfig(manager.snapshot());

    // 启动配置监视
    std::cout << "Starting config watcher (checking every 2 seconds)...\n";
    std::cout << "Modify config.toml or set environment variables to see hot reload in action.\n";
    std::cout << "Press Ctrl+C to exit.\n\n";

    auto watchResult = manager.startWatching({2s, 500ms},  // 每 2 秒检查，500ms debounce
                                             [&printConfig](const novainfra::config::Snapshot &newSnapshot,
                                                            const novainfra::config::Error *error) {
                                                 if (error) {
                                                     std::cerr << "Config reload failed: " << error->message()
                                                               << '\n';
                                                     for (const auto &diag : error->diagnostics) {
                                                         std::cerr << "   - " << diag.message;
                                                         if (!diag.source.empty()) {
                                                             std::cerr << " (" << diag.source;
                                                             if (diag.line)
                                                                 std::cerr << ":" << diag.line;
                                                             std::cerr << ")";
                                                         }
                                                         std::cerr << '\n';
                                                     }
                                                     return;
                                                 }

                                                 std::cout << "Configuration reloaded successfully!\n";
                                                 printConfig(newSnapshot);
                                             });

    if (!watchResult) {
        std::cerr << "Failed to start config watcher: " << watchResult.error().message() << '\n';
        return 1;
    }

    // 保持程序运行
    while (true) {
        std::this_thread::sleep_for(1s);

        // 检查监视器是否因异常而停止
        if (!manager.watching()) {
            std::cerr << "Config watcher stopped unexpectedly\n";
#ifndef NOVAINFRA_NO_EXCEPTIONS
            if (auto failure = manager.watchFailure()) {
                try {
                    std::rethrow_exception(failure);
                } catch (const std::exception &e) {
                    std::cerr << "Watch failure: " << e.what() << '\n';
                }
            }
#endif
            break;
        }
    }

    manager.stopWatching();
    return 0;
}
