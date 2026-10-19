#include <config/config.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

// 配置验证工具
class ConfigValidator
{
public:
    ConfigValidator(const std::string &schemaName) : schemaName_(schemaName) {}

    void setSchema(novainfra::config::Schema schema) { schema_ = std::move(schema); }

    bool validateFile(const std::filesystem::path &configPath)
    {
        std::cout << "Validating configuration file: " << configPath << '\n';
        std::cout << "Schema: " << schemaName_ << "\n\n";

        if (!std::filesystem::exists(configPath)) {
            std::cerr << "Error: File does not exist: " << configPath << '\n';
            return false;
        }

        auto result = novainfra::config::ConfigManager::load(schema_, {
                                                                          {configPath, true}
        });

        if (!result) {
            std::cerr << "Validation FAILED\n\n";
            printErrors(result.error());
            return false;
        }

        std::cout << "Validation PASSED\n\n";
        printConfigSummary(result.value().snapshot());
        return true;
    }

    bool validateWithEnvironment(const std::filesystem::path &configPath, const std::string &envPrefix)
    {
        std::cout << "Validating configuration with environment variables\n";
        std::cout << "  File: " << configPath << '\n';
        std::cout << "  Environment prefix: " << envPrefix << "\n\n";

        auto result = novainfra::config::ConfigManager::load(schema_,
                                                             {
                                                                 {configPath, false}
        },
                                                             envPrefix);

        if (!result) {
            std::cerr << "Validation FAILED\n\n";
            printErrors(result.error());
            return false;
        }

        std::cout << "Validation PASSED\n\n";
        printConfigSummary(result.value().snapshot());
        return true;
    }

private:
    void printErrors(const novainfra::config::Error &error) const
    {
        std::cout << "Errors found:\n";
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";

        for (const auto &diag : error.diagnostics) {
            std::cout << "  ⚠ ";

            // 错误类型
            switch (diag.code) {
                case novainfra::config::ErrorCode::FILE_NOT_FOUND:
                    std::cout << "[FILE_NOT_FOUND] ";
                    break;
                case novainfra::config::ErrorCode::FILE_READ:
                    std::cout << "[FILE_READ] ";
                    break;
                case novainfra::config::ErrorCode::PARSE:
                    std::cout << "[PARSE_ERROR] ";
                    break;
                case novainfra::config::ErrorCode::INVALID_COMMAND_LINE:
                    std::cout << "[INVALID_COMMAND_LINE] ";
                    break;
                case novainfra::config::ErrorCode::TYPE_MISMATCH:
                    std::cout << "[TYPE_MISMATCH] ";
                    break;
                case novainfra::config::ErrorCode::MISSING_REQUIRED:
                    std::cout << "[MISSING_REQUIRED] ";
                    break;
                case novainfra::config::ErrorCode::INVALID_VALUE:
                    std::cout << "[INVALID_VALUE] ";
                    break;
                case novainfra::config::ErrorCode::UNKNOWN_KEY:
                    std::cout << "[UNKNOWN_KEY] ";
                    break;
                case novainfra::config::ErrorCode::DUPLICATE_KEY:
                    std::cout << "[DUPLICATE_KEY] ";
                    break;
                case novainfra::config::ErrorCode::RELOAD_FAILED:
                    std::cout << "[RELOAD_FAILED] ";
                    break;
            }

            std::cout << diag.message << '\n';

            if (!diag.key.empty()) {
                std::cout << "     Key: " << diag.key << '\n';
            }
            if (!diag.source.empty()) {
                std::cout << "     File: " << diag.source;
                if (diag.line) {
                    std::cout << " (line " << diag.line << ")";
                }
                std::cout << '\n';
            }
            std::cout << '\n';
        }
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    }

    void printConfigSummary(const novainfra::config::Snapshot &snapshot) const
    {
        std::cout << "Configuration Summary:\n";
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        std::cout << "  (Displaying all loaded values would require schema introspection)\n";
        std::cout << "  Configuration loaded successfully with all required fields present.\n";
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    }

    std::string schemaName_;
    novainfra::config::Schema schema_;
};

novainfra::config::Schema createWebServerSchema()
{
    novainfra::config::Schema schema;

    // Server configuration
    schema.required<std::string>("server.host");
    schema.required<std::int64_t>("server.port", novainfra::config::FieldConstraints{1, 65535, {}, {}, {}});
    schema.optional<std::int64_t>("server.max_connections", 1000,
                                  novainfra::config::FieldConstraints{1, 100000, {}, {}, {}});
    schema.optional<std::int64_t>("server.timeout_ms", 30000,
                                  novainfra::config::FieldConstraints{100, 3600000, {}, {}, {}});
    schema.optional<bool>("server.enable_tls", false);

    // TLS configuration
    schema.optional<std::string>("server.tls.cert_file", "");
    schema.optional<std::string>("server.tls.key_file", "");
    schema.optional<std::string>("server.tls.min_version", "TLSv1.2");

    // Database configuration
    schema.required<std::string>("database.url");
    schema.optional<std::int64_t>("database.max_pool_size", 20,
                                  novainfra::config::FieldConstraints{1, 1000, {}, {}, {}});
    schema.optional<std::int64_t>("database.min_pool_size", 5,
                                  novainfra::config::FieldConstraints{1, 100, {}, {}, {}});
    schema.optional<std::int64_t>("database.connection_timeout_ms", 5000,
                                  novainfra::config::FieldConstraints{100, 60000, {}, {}, {}});

    // Logging configuration
    schema.optional<std::string>(
        "logging.level", "info", {}, false,
        [](const novainfra::config::Value &value) -> std::optional<std::string> {
            const auto &level = std::get<std::string>(value.storage());
            const std::vector<std::string> validLevels = {"trace",   "debug", "info",
                                                          "warning", "error", "critical"};
            if (std::find(validLevels.begin(), validLevels.end(), level) == validLevels.end()) {
                return "Invalid log level. Must be one of: trace, debug, info, warning, "
                       "error, critical";
            }
            return std::nullopt;
        });
    schema.optional<std::string>("logging.file", "");
    schema.optional<std::int64_t>("logging.max_file_size_mb", 100,
                                  novainfra::config::FieldConstraints{1, 10000, {}, {}, {}});
    schema.optional<std::int64_t>("logging.max_files", 10,
                                  novainfra::config::FieldConstraints{1, 100, {}, {}, {}});

    // Cache configuration
    schema.optional<bool>("cache.enabled", true);
    schema.optional<std::int64_t>("cache.max_size_mb", 1024,
                                  novainfra::config::FieldConstraints{1, 102400, {}, {}, {}});
    schema.optional<std::int64_t>("cache.ttl_seconds", 3600,
                                  novainfra::config::FieldConstraints{1, 86400, {}, {}, {}});

    return schema;
}

novainfra::config::Schema createDatabaseServiceSchema()
{
    novainfra::config::Schema schema;

    // Service configuration
    schema.required<std::string>("service.name");
    schema.required<std::string>("service.host");
    schema.required<std::int64_t>("service.port", novainfra::config::FieldConstraints{1, 65535, {}, {}, {}});
    schema.optional<std::int64_t>("service.workers", 4,
                                  novainfra::config::FieldConstraints{1, 128, {}, {}, {}});

    // Database configuration
    schema.required<std::string>(
        "database.engine", {}, false,
        [](const novainfra::config::Value &value) -> std::optional<std::string> {
            const auto &engine = std::get<std::string>(value.storage());
            const std::vector<std::string> validEngines = {"postgresql", "mysql", "sqlite"};
            if (std::find(validEngines.begin(), validEngines.end(), engine) == validEngines.end()) {
                return "Invalid database engine. Must be one of: postgresql, mysql, sqlite";
            }
            return std::nullopt;
        });
    schema.required<std::string>("database.host");
    schema.required<std::int64_t>("database.port", novainfra::config::FieldConstraints{1, 65535, {}, {}, {}});
    schema.required<std::string>("database.database");
    schema.required<std::string>("database.username");
    schema.required<std::string>("database.password");

    // Pool configuration
    schema.optional<std::int64_t>("pool.min_connections", 5,
                                  novainfra::config::FieldConstraints{1, 1000, {}, {}, {}});
    schema.optional<std::int64_t>("pool.max_connections", 50,
                                  novainfra::config::FieldConstraints{1, 10000, {}, {}, {}});
    schema.optional<std::int64_t>("pool.idle_timeout_seconds", 600,
                                  novainfra::config::FieldConstraints{1, 86400, {}, {}, {}});

    // Monitoring configuration
    schema.optional<bool>("monitoring.enabled", true);
    schema.optional<std::int64_t>("monitoring.metrics_port", 9090,
                                  novainfra::config::FieldConstraints{1, 65535, {}, {}, {}});
    schema.optional<std::int64_t>("monitoring.health_check_interval_seconds", 30,
                                  novainfra::config::FieldConstraints{1, 3600, {}, {}, {}});

    return schema;
}

int main(int argc, char *argv[])
{
    std::cout << "NovaInfra Config Validator\n";
    std::cout << "==========================\n\n";

    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <schema-type> <config-file> [env-prefix]\n\n";
        std::cout << "Schema types:\n";
        std::cout << "  webserver        - Web server configuration schema\n";
        std::cout << "  database-service - Database service configuration schema\n";
        std::cout << "\nExamples:\n";
        std::cout << "  " << argv[0] << " webserver config.toml\n";
        std::cout << "  " << argv[0] << " webserver config.toml APP_\n";
        return 1;
    }

    std::string schemaType = argv[1];
    std::string configFile = argv[2];
    std::string envPrefix = (argc >= 4) ? argv[3] : "";

    ConfigValidator validator(schemaType);

    if (schemaType == "webserver") {
        validator.setSchema(createWebServerSchema());
    } else if (schemaType == "database-service") {
        validator.setSchema(createDatabaseServiceSchema());
    } else {
        std::cerr << "Unknown schema type: " << schemaType << '\n';
        return 1;
    }

    bool success;
    if (!envPrefix.empty()) {
        success = validator.validateWithEnvironment(configFile, envPrefix);
    } else {
        success = validator.validateFile(configFile);
    }

    return success ? 0 : 1;
}
