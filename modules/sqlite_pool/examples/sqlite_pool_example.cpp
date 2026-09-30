#include <sqlite_pool/connection_pool.h>
#include <sqlite_pool/sqlite_error.h>

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    const std::string path = argc > 1 ? argv[1] : "sqlite_pool_example.db";
    try {
        novainfra::sqlitepool::PoolConfig config;
        config.readConnections = 2;
        config.pragmas.emplace_back("foreign_keys=ON");
        novainfra::sqlitepool::ConnectionPool pool(path, config);

        {
            auto db = pool.write();
            db.exec("CREATE TABLE IF NOT EXISTS notes ("
                    "id INTEGER PRIMARY KEY, title TEXT NOT NULL, body TEXT, priority REAL NOT NULL)");
            db.transaction([](novainfra::sqlitepool::WriteConn &tx) {
                auto insert =
                    tx.prepare("INSERT INTO notes(title, body, priority) VALUES(:title, :body, :priority)");
                insert.bind(":title", std::string("sqlite_pool"))
                    .bind(":body", std::string("RAII connections and statements"))
                    .bind(":priority", 1.5)
                    .exec();
            });
        }

        {
            auto reader = pool.read();
            auto rows = reader.query("SELECT id, title, priority FROM notes ORDER BY id");
            for (const auto &row : rows) {
                std::cout << row.at("id") << ": " << row.at("title") << " (priority=" << row.at("priority")
                          << ")\n";
            }
        }

        pool.integrityCheck();
        pool.optimize();
        pool.checkpoint();
        std::cout << "database: " << std::filesystem::absolute(path) << "\n";
        return 0;
    } catch (const novainfra::sqlitepool::SqliteError &error) {
        std::cerr << "sqlite error (" << error.code() << "): " << error.what() << '\n';
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
    }
    return 1;
}
