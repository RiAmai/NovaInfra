#include <config/config.h>

#include <iostream>
#include <regex>

int main(int argc, const char **argv)
{
    novainfra::config::Schema schema;

    // 基本类型约束
    schema.required<std::int64_t>("server.port",
                                  novainfra::config::FieldConstraints{1024, 65535, {}, {}, {}});
    schema.required<std::int64_t>("server.max_connections",
                                  novainfra::config::FieldConstraints{1, 10000, {}, {}, {}});
    schema.required<std::int64_t>("server.timeout_ms",
                                  novainfra::config::FieldConstraints{100, 300000, {}, {}, {}});

    // 自定义验证器：验证 email 格式
    schema.required<std::string>(
        "admin.email", {}, false, [](const novainfra::config::Value &value) -> std::optional<std::string> {
            const auto &email = std::get<std::string>(value.storage());
            // 简单的 email 正则验证
            std::regex emailRegex(R"([a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,})");
            if (!std::regex_match(email, emailRegex)) {
                return "Invalid email format. Expected format: user@domain.com";
            }
            return std::nullopt;  // 验证通过
        });

    // 自定义验证器：验证 URL 格式
    schema.required<std::string>(
        "database.url", {}, false, [](const novainfra::config::Value &value) -> std::optional<std::string> {
            const auto &url = std::get<std::string>(value.storage());
            if (url.empty()) {
                return "Database URL cannot be empty";
            }
            // 检查是否以支持的协议开头
            const std::vector<std::string> validPrefixes = {"postgresql://", "mysql://", "sqlite://"};
            bool validPrefix = false;
            for (const auto &prefix : validPrefixes) {
                if (url.substr(0, prefix.size()) == prefix) {
                    validPrefix = true;
                    break;
                }
            }
            if (!validPrefix) {
                return "Database URL must start with postgresql://, mysql://, or sqlite://";
            }
            return std::nullopt;
        });

    // 自定义验证器：验证日志级别
    schema.required<std::string>(
        "logging.level", {}, false, [](const novainfra::config::Value &value) -> std::optional<std::string> {
            const auto &level = std::get<std::string>(value.storage());
            const std::vector<std::string> validLevels = {"trace",   "debug", "info",
                                                          "warning", "error", "critical"};
            if (std::find(validLevels.begin(), validLevels.end(), level) == validLevels.end()) {
                return "Invalid log level. Must be one of: trace, debug, info, warning, "
                       "error, critical";
            }
            return std::nullopt;
        });

    // 自定义验证器：验证路径存在性
    schema.optional<std::string>("logging.file", "", {}, false,
                                 [](const novainfra::config::Value &value) -> std::optional<std::string> {
                                     const auto &path = std::get<std::string>(value.storage());
                                     if (path.empty()) {
                                         return std::nullopt;  // 空路径是可选的
                                     }
                                     // 检查父目录是否存在
                                     auto parentPath = std::filesystem::path(path).parent_path();
                                     if (!parentPath.empty() && !std::filesystem::exists(parentPath)) {
                                         return "Parent directory does not exist: " + parentPath.string();
                                     }
                                     return std::nullopt;
                                 });

    // 跨字段验证：使用自定义验证器
    schema.optional<bool>("server.tls", false);
    schema.optional<std::string>("server.tls_cert", "");
    schema.optional<std::string>("server.tls_key", "");

    // 加载配置
    std::cout << "Loading and validating configuration...\n\n";

    auto result = novainfra::config::ConfigManager::load(std::move(schema),
                                                         {
                                                             {"config.toml", false}
    },
                                                         "APP_", argc, argv);

    if (!result) {
        std::cerr << "Configuration validation failed!\n\n";
        std::cerr << "Errors:\n";
        for (const auto &diag : result.error().diagnostics) {
            std::cerr << "  • " << diag.message;
            if (!diag.key.empty()) {
                std::cerr << " (key: " << diag.key << ")";
            }
            if (!diag.source.empty()) {
                std::cerr << "\n    File: " << diag.source;
                if (diag.line) {
                    std::cerr << ":" << diag.line;
                }
            }
            std::cerr << "\n";
        }
        return 1;
    }

    std::cout << "Configuration validated successfully!\n\n";

    // 打印配置
    auto manager = std::move(result.value());
    const auto snapshot = manager.snapshot();

    std::cout << "=== Validated Configuration ===\n";
    std::cout << "Server:\n";
    std::cout << "  Port: " << snapshot.get<int>("server.port").value() << '\n';
    std::cout << "  Max Connections: " << snapshot.get<int>("server.max_connections").value() << '\n';
    std::cout << "  Timeout: " << snapshot.get<int>("server.timeout_ms").value() << " ms\n";
    std::cout << "  TLS: " << (snapshot.get<bool>("server.tls").value() ? "Enabled" : "Disabled") << '\n';

    std::cout << "\nDatabase:\n";
    std::cout << "  URL: " << snapshot.get<std::string>("database.url").value() << '\n';

    std::cout << "\nLogging:\n";
    std::cout << "  Level: " << snapshot.get<std::string>("logging.level").value() << '\n';
    auto logFile = snapshot.get<std::string>("logging.file");
    if (logFile && !logFile.value().empty()) {
        std::cout << "  File: " << logFile.value() << '\n';
    }

    std::cout << "\nAdmin:\n";
    std::cout << "  Email: " << snapshot.get<std::string>("admin.email").value() << '\n';

    std::cout << "==============================\n";

    // 演示跨字段验证
    if (snapshot.get<bool>("server.tls").value()) {
        auto cert = snapshot.get<std::string>("server.tls_cert");
        auto key = snapshot.get<std::string>("server.tls_key");
        if (!cert || cert.value().empty() || !key || key.value().empty()) {
            std::cerr << "\n  Warning: TLS is enabled but certificate or key path is not configured!\n";
        }
    }

    return 0;
}
