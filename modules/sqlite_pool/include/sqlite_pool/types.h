#pragma once

#include <core/namespace.h>
#include <core/types.h>
#include "sqlite3.h"

#include <unordered_map>
#include <string>
#include <vector>
#include <functional>

NOVAINFRA_NAMESPACE_BEGIN
namespace sqlitepool {

using Row = std::unordered_map<std::string, std::string>;
using RowSet = std::vector<Row>;

struct PoolConfig {
    Uint32 readConnections = 0;  // Number of read-only connections (0 = max(hardware_concurrency, 4))
    int pageCacheSize = -8000;   // SQLite page cache (negative = KiB; e.g. -8000 -> 8MB)
    int busyTimeoutMs = 5000;    // Busy-wait timeout in ms
    Uint32 healthCheckIntervalMs = 30000;  // Background health check interval (0 = disabled)
    std::string journalMode = "WAL";       // Journal mode (WAL strongly recommended)
    std::vector<std::string> pragmas;      // Extra PRAGMAs applied to every connection, e.g. "foreign_key=ON"
    std::function<void(sqlite3 *, bool /*is_write*/)> postOpen = nullptr;  // Encryption / post-open hook
};

struct HealthCheckResult {
    Size checked{0};
    Size healthy{0};
    Size replaced{0};
    Size failed{0};
    Size skippedBusy{0};
};

}  // namespace sqlitepool
NOVAINFRA_NAMESPACE_END
