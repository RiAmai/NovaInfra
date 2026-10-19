#include <config/config.h>

#include <iostream>

int main(int argc, const char **argv)
{
    novainfra::config::Schema schema;
    schema.optional<std::int64_t>("server.port", 8080,
                                  novainfra::config::FieldConstraints{1, 65535, {}, {}, {}});
    schema.optional<std::string>("server.host", "127.0.0.1");
    schema.optional<bool>("server.tls", false);

    auto loaded = novainfra::config::ConfigManager::load(std::move(schema), {}, "APP_", argc, argv);
    if (!loaded) {
        std::cerr << loaded.error().message() << '\n';
        return 2;
    }
    const auto snapshot = loaded.value().snapshot();
    std::cout << "host=" << snapshot.get<std::string>("server.host").value() << '\n';
    std::cout << "port=" << snapshot.get<int>("server.port").value() << '\n';
    return 0;
}
