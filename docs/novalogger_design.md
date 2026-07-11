# Nova Logger Design

## 1. 目标

Nova Logger 是 NovaCore 的日志模块，目标是提供一个可靠、清晰、可测试的 C++17 日志基础设施。

当前优先保证：

- 清晰的模块边界
- RAII 管理资源
- 多线程调用安全
- 可扩展的 Sink / Formatter 架构
- 可演进到文件日志、滚动日志和异步日志

非目标：

- MVP 不追求无锁队列或极限性能
- MVP 不支持运行期复杂动态配置
- MVP 不强制引入第三方格式化库

---

## 2. 核心对象

### 2.1 `LogLevel`

日志级别建议从低到高定义为：

- `Trace`
- `Debug`
- `Info`
- `Warn`
- `Error`
- `Critical`
- `Off`

`Logger` 根据当前 level 过滤日志。低于当前 level 的日志不进入 formatter 和 sink。

### 2.2 `SourceLocation`

用于记录调用点信息：

```cpp
struct SourceLocation {
    std::string_view file;
    int line;
    std::string_view function;
};
```

`SourceLocation` 由宏捕获 `__FILE__`、`__LINE__`、`NOVA_LOG_FUNCTION`。

这些字符串由编译器静态存储，使用 `std::string_view` 安全。

### 2.3 `LogMsg`

`LogMsg` 是一次日志事件的数据载体。

```cpp
struct LogMsg {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::thread::id thread_id;
    SourceLocation source;
    std::string logger_name;
    std::string message;
};
```

规则：

- `message` 必须使用 `std::string` 拥有内容。
- `std::string_view` 只用于 API 入参或静态生命周期字段。
- 任何会进入 sink、文件、队列或后台线程的数据都不能引用调用栈临时对象。

---

## 3. 架构边界

核心结构：

```text
User Code
   |
   v
Logger
   |
   +-- Formatter
   |
   +-- Sink
         |
         +-- ConsoleSink
         +-- FileSink
         +-- RollingFileSink
```

### 3.1 `Logger`

职责：

- 保存 logger name
- 保存当前 `LogLevel`
- 接收用户日志调用
- 构造 `LogMsg`
- 遍历 sinks 输出日志
- 提供 `flush()`

`Logger` 不直接处理文件路径、控制台颜色、文件滚动和异步队列细节。

### 3.2 `Formatter`

职责：

- 将 `LogMsg` 转换为最终输出字符串
- 控制时间、级别、线程、文件行号、正文的展示格式

建议接口：

```cpp
class Formatter {
public:
    virtual ~Formatter() = default;
    virtual std::string format(const LogMsg& msg) = 0;
};
```

每个 `Sink` 持有自己的 formatter。这样不同输出目标可以使用不同格式。

### 3.3 `Sink`

职责：

- 接收 `LogMsg`
- 调用 formatter
- 将格式化后的字符串写入目标
- 保存最近一次运行期错误状态

建议接口：

```cpp
class Sink {
public:
    virtual ~Sink() = default;
    virtual void log(const LogMsg& msg) = 0;
    virtual void flush() = 0;
    virtual std::optional<std::string> lastError() const = 0;
};
```

`log()`、`flush()` 和 `lastError()` 都必须可并发调用。`lastError()` 返回错误字符串副本，sink 内部使用 `std::mutex` 或等价同步保护错误状态。

---

## 4. Phase 1：同步控制台日志 MVP

目标：

- 实现最小可用日志链路
- 支持 `Logger`、`Formatter`、`Sink`、`ConsoleSink`
- 支持日志级别过滤
- 支持调用点信息

范围：

- 输出到 `stdout` / `stderr`
- 默认文本格式
- 不支持文件日志
- 不支持异步日志
- 不支持运行期动态增删 sink

线程安全要求：

- `Logger::log()` 可被多个线程并发调用。
- 每个 sink 内部负责自己的输出串行化。
- `ConsoleSink` 使用 `std::mutex` 或 `std::scoped_lock` 保护输出。
- 初始化完成后，sinks 列表默认不再修改。
- `Logger` 读取当前 level 时不能产生数据竞争。
- Phase 1 不支持日志调用期间并发修改 sink 列表。

推荐 API：

```cpp
logger.info("message");
logger.warn("message");
logger.error("message");
```

宏 API：

```cpp
NOVA_LOGGER_INFO(logger_ptr, "message");
NOVA_LOGGER_ERROR(logger_ptr, "message");
```

宏只负责捕获调用点、选择日志级别和应用编译期裁剪，不应隐藏复杂控制流。
`NOVA_LOGGER_*` 的 `logger` 参数必须是 pointer-like 对象，支持 `operator->`，例如 `Logger*` 或 `std::shared_ptr<Logger>`。如果调用方持有 `Logger` 对象本身，应使用成员函数或显式传入 `&logger`。

### 4.1 类 spdlog 宏接口

Nova Logger 的宏接口应接近 spdlog 的使用体验，同时使用项目自己的 `NOVA_LOG_` 前缀，避免污染全局常见宏名。

日志级别常量：

```cpp
#define NOVA_LOG_LEVEL_TRACE 0
#define NOVA_LOG_LEVEL_DEBUG 1
#define NOVA_LOG_LEVEL_INFO 2
#define NOVA_LOG_LEVEL_WARN 3
#define NOVA_LOG_LEVEL_ERROR 4
#define NOVA_LOG_LEVEL_CRITICAL 5
#define NOVA_LOG_LEVEL_OFF 6

#ifndef NOVA_LOG_ACTIVE_LEVEL
#define NOVA_LOG_ACTIVE_LEVEL NOVA_LOG_LEVEL_TRACE
#endif
```

调用点捕获：

```cpp
#ifndef NOVA_LOG_FUNCTION
#define NOVA_LOG_FUNCTION __func__
#endif

#ifndef NOVA_LOG_NO_SOURCE_LOC
#define NOVA_LOGGER_CALL(logger, level, ...) \
    (logger)->log(SourceLocation{__FILE__, __LINE__, NOVA_LOG_FUNCTION}, level, __VA_ARGS__)
#else
#define NOVA_LOGGER_CALL(logger, level, ...) \
    (logger)->log(SourceLocation{}, level, __VA_ARGS__)
#endif
```

按级别裁剪：

```cpp
#if NOVA_LOG_ACTIVE_LEVEL <= NOVA_LOG_LEVEL_TRACE
#define NOVA_LOGGER_TRACE(logger, ...) NOVA_LOGGER_CALL(logger, LogLevel::Trace, __VA_ARGS__)
#else
#define NOVA_LOGGER_TRACE(logger, ...) (void)0
#endif

#if NOVA_LOG_ACTIVE_LEVEL <= NOVA_LOG_LEVEL_DEBUG
#define NOVA_LOGGER_DEBUG(logger, ...) NOVA_LOGGER_CALL(logger, LogLevel::Debug, __VA_ARGS__)
#else
#define NOVA_LOGGER_DEBUG(logger, ...) (void)0
#endif

#if NOVA_LOG_ACTIVE_LEVEL <= NOVA_LOG_LEVEL_INFO
#define NOVA_LOGGER_INFO(logger, ...) NOVA_LOGGER_CALL(logger, LogLevel::Info, __VA_ARGS__)
#else
#define NOVA_LOGGER_INFO(logger, ...) (void)0
#endif

#if NOVA_LOG_ACTIVE_LEVEL <= NOVA_LOG_LEVEL_WARN
#define NOVA_LOGGER_WARN(logger, ...) NOVA_LOGGER_CALL(logger, LogLevel::Warn, __VA_ARGS__)
#else
#define NOVA_LOGGER_WARN(logger, ...) (void)0
#endif

#if NOVA_LOG_ACTIVE_LEVEL <= NOVA_LOG_LEVEL_ERROR
#define NOVA_LOGGER_ERROR(logger, ...) NOVA_LOGGER_CALL(logger, LogLevel::Error, __VA_ARGS__)
#else
#define NOVA_LOGGER_ERROR(logger, ...) (void)0
#endif

#if NOVA_LOG_ACTIVE_LEVEL <= NOVA_LOG_LEVEL_CRITICAL
#define NOVA_LOGGER_CRITICAL(logger, ...) NOVA_LOGGER_CALL(logger, LogLevel::Critical, __VA_ARGS__)
#else
#define NOVA_LOGGER_CRITICAL(logger, ...) (void)0
#endif
```

规则：

- `NOVA_LOG_ACTIVE_LEVEL` 的比较方向与 spdlog 一致，数值越小日志越详细。
- 被编译期裁剪的日志宏必须展开为 `(void)0`，不能求值参数。
- `NOVA_LOG_NO_SOURCE_LOC` 用于关闭调用点捕获，此时 `SourceLocation{}` 表示空调用点。
- `NOVA_LOGGER_*` 是基础宏接口，使用调用方传入的 pointer-like logger。
- `NOVA_LOG_*` 依赖默认 logger registry，不属于 Phase 1 宏接口。

### 4.2 最小公共接口

Phase 1 建议先保持 API 小而稳定：

```cpp
class Logger {
public:
    explicit Logger(std::string name);

    void setLevel(LogLevel level);
    LogLevel level() const;

    void addSink(std::shared_ptr<Sink> sink);

    void log(SourceLocation source, LogLevel level, std::string_view message);

    void trace(std::string_view message);
    void debug(std::string_view message);
    void info(std::string_view message);
    void warn(std::string_view message);
    void error(std::string_view message);
    void critical(std::string_view message);

    void flush();
};
```

约束：

- `Logger` 拥有 logger name。
- `Logger` 通过 `std::shared_ptr<Sink>` 共享 sink 生命周期。
- `addSink()` 应在业务线程开始写日志前完成。
- 如果 `Logger` 没有 sink，日志调用直接返回，不抛异常。
- `Off` 表示关闭日志输出。
- 普通字符串重载必须不依赖 fmtlib。
- Phase 1 不实现变参格式化重载，先验证日志链路、调用点捕获和宏裁剪。

### 4.3 默认输出格式

默认 formatter 输出一行文本，字段顺序建议为：

```text
timestamp - [level] [logger] [thread_id] [file:line]: message
```

示例：

```text
2026-01-01 10:30:45.123 - [INFO] [app] [tid=12345] [main.cpp:42]: service started
```

规则：

- 每条日志以换行符结尾。
- timestamp 使用本地时间或 UTC 均可，但实现和文档必须保持一致。
- level 使用固定大写文本：`TRACE`、`DEBUG`、`INFO`、`WARN`、`ERROR`、`CRITICAL`。
- formatter 不直接写入 IO。

### 4.4 控制台输出规则

`ConsoleSink` 建议按级别分流：

- `Trace`、`Debug`、`Info` 写入 `stdout`。
- `Warn`、`Error`、`Critical` 写入 `stderr`。

Phase 1 不要求彩色输出。后续如加入颜色，应由 `ConsoleSink` 控制，不应污染 `Formatter` 的通用文本格式。

### 4.5 过滤顺序

日志调用的推荐顺序：

1. 比较输入 level 和当前 logger level。
2. 如果被过滤，立即返回。
3. 构造 `LogMsg`，复制 message 内容。
4. 遍历 sink 调用 `Sink::log()`。

被过滤的日志不应构造 `LogMsg`，也不应调用 formatter。

---

## 5. Phase 2：文件日志与格式化接口

目标：

- 实现 `FileSink`
- 支持基础文件写入
- 明确格式化策略

### 5.1 `FileSink`

职责：

- 使用 RAII 管理文件句柄
- 初始化时创建日志目录
- 写入失败时给出明确错误处理策略
- `flush()` 将缓冲区刷入文件

文件打开失败策略：

- 构造失败时抛出异常，或返回工厂函数错误结果。
- 不允许静默失败。

建议优先使用 RAII 类型：

- `std::ofstream`
- `std::filesystem::path`

建议配置：

```cpp
struct FileSinkOptions {
    std::filesystem::path path;
    bool append = true;
    bool flush_on_each_log = false;
};
```

构造规则：

- `path` 必须指向普通文件路径，不能是空路径。
- 父目录不存在时，`FileSink` 负责创建目录。
- 文件默认追加写入，避免覆盖已有日志。
- 如果设置为非追加模式，构造时可以截断旧文件。
- 构造成功后，文件句柄由 `FileSink` 独占管理。

写入规则：

- `FileSink::log()` 内部加锁。
- `FileSink::log()` 调用 formatter 生成完整行，再写入文件流。
- 如果 `flush_on_each_log` 为 true，每次写入后立即 `flush()`。
- `FileSink::flush()` 必须在锁内刷新文件流。

失败处理：

- 构造阶段创建目录失败或打开文件失败时，直接抛出异常。
- 写入阶段如果文件流进入失败状态，应记录内部错误状态，并允许调用方通过 `lastError()` 观察。
- Phase 2 不要求日志系统自恢复或自动重新打开文件。
- 不允许在写入失败时静默丢弃且没有任何可观测状态。

### 5.2 格式化策略

C++17 没有 `std::format`。

MVP 默认支持普通字符串日志。格式化增强分两步：

1. 先支持 stream 风格或简单字符串。
2. `{}` 风格格式化作为可选增强，优先使用 `fmtlib`。

`fmtlib` 策略：

- 不作为 MVP 强依赖。
- 如果引入，需要在 CMake 中显式声明。
- 公共 API 不应让用户在没有 fmt 时无法使用基础日志功能。
- `NOVA_LOG_*` 宏和 `Logger` 变参便捷函数可以在 fmtlib 可用时支持 `{}` 格式化。
- fmtlib 不可用时，普通字符串日志 API 必须继续可用。

fmtlib 可用时，格式化 API 可以切换到类型检查更强的签名：

```cpp
template <typename... Args>
void log(SourceLocation source, LogLevel level, format_string_t<Args...> fmt, Args&&... args);

template <typename... Args>
void trace(format_string_t<Args...> fmt, Args&&... args);

template <typename... Args>
void debug(format_string_t<Args...> fmt, Args&&... args);

template <typename... Args>
void info(format_string_t<Args...> fmt, Args&&... args);

template <typename... Args>
void warn(format_string_t<Args...> fmt, Args&&... args);

template <typename... Args>
void error(format_string_t<Args...> fmt, Args&&... args);

template <typename... Args>
void critical(format_string_t<Args...> fmt, Args&&... args);
```

其中 `format_string_t` 应由 logger 模块统一封装。没有启用 fmtlib 时，不应向用户暴露依赖 fmtlib 的类型。
变参格式化重载只有在格式化后才能构造 `LogMsg`，进入 sink 的 message 必须拥有内容。

Phase 2 推荐先稳定 formatter 接口，再考虑格式化增强。

可选增强：

```cpp
class PatternFormatter : public Formatter {
public:
    explicit PatternFormatter(std::string pattern);
    std::string format(const LogMsg& msg) override;
};
```

基础 pattern 可先支持：

```text
%Y-%m-%d %H:%M:%S
%l level
%n logger name
%t thread id
%s source file
%# source line
%v message
```

不建议在 Phase 2 立即实现复杂 pattern 解析。若实现 pattern，必须明确不支持的占位符，并在构造时失败，而不是运行期产生不完整输出。

---

## 6. Phase 3：滚动文件与更完整的线程安全

目标：

- 实现 `RollingFileSink`
- 支持按大小滚动
- 补齐文件 flush 和失败处理

### 6.1 按大小滚动

配置项：

```text
base_path
max_file_size
max_files
```

行为：

- 当前文件超过 `max_file_size` 后触发滚动。
- 旧文件按序号或时间戳重命名。
- 超过 `max_files` 的旧文件被删除。
- 滚动过程由 sink 内部锁保护。

建议配置：

```cpp
struct RollingFileSinkOptions {
    std::filesystem::path base_path;
    std::uintmax_t max_file_size;
    std::size_t max_files;
    bool append = true;
};
```

参数约束：

- `base_path` 不能为空。
- `max_file_size` 必须大于 0。
- `max_files` 必须大于 0。
- 参数非法时构造失败，不创建半初始化对象。

### 6.2 滚动时机

滚动检查应在每次写入前或写入后执行，但必须固定一种语义：

- 推荐写入前检查当前文件大小。
- 如果当前文件大小加上本次输出大小会超过 `max_file_size`，先滚动，再写入新文件。
- 单条日志大于 `max_file_size` 时仍写入新文件，不拆分日志行。

### 6.3 命名策略

Phase 3 推荐使用序号文件，规则简单、可测试：

```text
app.log
app.log.1
app.log.2
app.log.3
```

滚动过程：

1. 删除超过 `max_files` 的最旧文件。
2. 将 `app.log.N-1` 重命名为 `app.log.N`。
3. 将当前 `app.log` 重命名为 `app.log.1`。
4. 创建新的 `app.log`。

`max_files` 表示保留的历史文件数量，不包含当前正在写入的 `base_path`。

### 6.4 失败处理

滚动中的任何文件系统操作失败，都不能破坏当前对象的不变量。

建议策略：

- 滚动失败时，本次 `log()` 不抛异常，记录 sink 内部错误状态。
- 当前文件仍可写时，继续写入当前文件。
- 当前文件不可写时，丢弃本次日志并记录错误状态。
- `flush()` 只负责刷新当前打开文件，不触发滚动。

Phase 3 不要求跨进程安全。多个进程同时写同一个滚动文件路径属于非目标。

---

## 7. Phase 4：异步日志候选设计

异步日志不是 MVP，但前期对象设计必须避免阻断后续演进。

目标：

- 降低业务线程写日志的 IO 阻塞。
- 将文件写入和滚动操作移动到后台线程。
- 保持 `Logger` 和 `Sink` 的公共概念不大幅变化。

建议结构：

```text
Logger
  |
  v
AsyncSink
  |
  +-- bounded queue
  +-- worker thread
  +-- inner Sink
```

`AsyncSink` 包装一个真实 sink，例如 `FileSink` 或 `RollingFileSink`。业务线程把完整拥有数据的 `LogMsg` 放入队列，后台线程取出后调用 inner sink。

关键规则：

- 入队的 `LogMsg` 必须拥有所有跨线程数据。
- `SourceLocation` 只能保存静态生命周期字段。
- `AsyncSink` 析构时必须停止后台线程并 flush 剩余日志。
- 后台线程异常不能逃逸出线程函数。
- 异步 worker 退出前必须 drain 已入队日志并 join 后台线程。

队列策略需要在实现前明确：

- 队列满时阻塞调用方。
- 队列满时丢弃新日志。
- 队列满时丢弃旧日志。

Nova Logger 默认建议先选择阻塞策略，行为最容易理解，适合作为工程基础库的第一版异步实现。

如果未来提供非阻塞入队接口，返回值必须标记 `[[nodiscard]]`，调用方必须能知道日志是否成功入队。

异步模式下 `flush()` 的语义：

- 等待当前已经提交的日志全部被 worker 消费。
- 调用下游 sink 的 `flush()`。
- 不要求阻止其他线程继续提交新日志，除非接口文档明确说明。

---

## 8. 全局 Logger Registry 与默认 Logger

全局 logger registry 用于支持类 spdlog 的默认 logger 便捷调用。

目标：

- 允许 `NOVA_LOG_INFO("message")` 直接写入默认 logger。
- 允许应用在启动阶段替换默认 logger。
- 避免业务代码到处传递同一个 logger 对象。

推荐接口：

```cpp
std::shared_ptr<Logger> defaultLogger();

Logger* defaultLoggerRaw();

void setDefaultLogger(std::shared_ptr<Logger> logger);

inline void trace(std::string_view message) {
    defaultLogger()->trace(message);
}

inline void debug(std::string_view message) {
    defaultLogger()->debug(message);
}

inline void info(std::string_view message) {
    defaultLogger()->info(message);
}

inline void warn(std::string_view message) {
    defaultLogger()->warn(message);
}

inline void error(std::string_view message) {
    defaultLogger()->error(message);
}

inline void critical(std::string_view message) {
    defaultLogger()->critical(message);
}
```

registry 规则：

- registry 内部持有 `std::shared_ptr<Logger>`。
- `defaultLogger()` 返回当前默认 logger 的 `std::shared_ptr` 快照，适合普通便捷函数和全局宏使用。
- `defaultLoggerRaw()` 返回裸指针，不转移所有权；只有在默认 logger 不会被并发替换时才可使用，调用方不能保存该指针。
- 默认 logger 应在 registry 初始化时指向一个可用的控制台 logger。
- `setDefaultLogger(nullptr)` 不允许，避免默认宏调用空指针。
- `setDefaultLogger()` 必须线程安全，但推荐只在应用启动阶段调用。
- registry 替换默认 logger 时必须原子地替换 `std::shared_ptr`，不能销毁正在被其他线程通过快照持有的 logger。
- 若需要跨语句保存 logger，必须使用 `defaultLogger()`。

与宏的关系：

```cpp
#if NOVA_LOG_ACTIVE_LEVEL <= NOVA_LOG_LEVEL_TRACE
#define NOVA_LOG_TRACE(...) NOVA_LOGGER_TRACE(defaultLogger(), __VA_ARGS__)
#else
#define NOVA_LOG_TRACE(...) (void)0
#endif

#if NOVA_LOG_ACTIVE_LEVEL <= NOVA_LOG_LEVEL_DEBUG
#define NOVA_LOG_DEBUG(...) NOVA_LOGGER_DEBUG(defaultLogger(), __VA_ARGS__)
#else
#define NOVA_LOG_DEBUG(...) (void)0
#endif

#if NOVA_LOG_ACTIVE_LEVEL <= NOVA_LOG_LEVEL_INFO
#define NOVA_LOG_INFO(...) NOVA_LOGGER_INFO(defaultLogger(), __VA_ARGS__)
#else
#define NOVA_LOG_INFO(...) (void)0
#endif

#if NOVA_LOG_ACTIVE_LEVEL <= NOVA_LOG_LEVEL_WARN
#define NOVA_LOG_WARN(...) NOVA_LOGGER_WARN(defaultLogger(), __VA_ARGS__)
#else
#define NOVA_LOG_WARN(...) (void)0
#endif

#if NOVA_LOG_ACTIVE_LEVEL <= NOVA_LOG_LEVEL_ERROR
#define NOVA_LOG_ERROR(...) NOVA_LOGGER_ERROR(defaultLogger(), __VA_ARGS__)
#else
#define NOVA_LOG_ERROR(...) (void)0
#endif

#if NOVA_LOG_ACTIVE_LEVEL <= NOVA_LOG_LEVEL_CRITICAL
#define NOVA_LOG_CRITICAL(...) NOVA_LOGGER_CRITICAL(defaultLogger(), __VA_ARGS__)
#else
#define NOVA_LOG_CRITICAL(...) (void)0
#endif
```

因此全局便捷宏依赖默认 logger，但 `NOVA_LOGGER_INFO(logger, ...)` 不依赖 registry。
Registry 第一版只要求支持普通字符串便捷函数；变参模板便捷函数随 fmtlib 可选集成一起增加。

全局 registry 不应进入最小 MVP。推荐在基础同步 logger 和文件日志稳定后实现，否则会过早引入全局状态、测试隔离和生命周期复杂度。

---

## 9. 与其他模块的未来集成

### 9.1 Config 集成

Logger 不应直接依赖 Config 模块。推荐由上层应用读取配置后构造 logger。

未来可以提供轻量适配函数：

```cpp
Logger create_logger_from_config(const LoggerConfig& config);
```

`LoggerConfig` 应属于 logger 模块或应用层 DTO，不应让 logger 核心依赖某个具体配置解析器。

### 9.2 ThreadPool 集成

异步日志优先使用 logger 自己管理的后台线程。只有当 ThreadPool 模块稳定后，才考虑把后台执行策略抽象为可注入 executor。

Phase 1 到 Phase 3 不依赖 ThreadPool。

---

## 10. 错误处理策略

Logger 模块需要区分构造失败和运行期写入失败。

构造失败：

- 参数非法。
- 文件路径非法。
- 创建目录失败。
- 打开文件失败。

这些错误应直接返回失败结果或抛异常，不能构造不可用对象。

运行期失败：

- 文件写入失败。
- 文件 flush 失败。
- 滚动重命名失败。
- 异步队列后台线程处理失败。

运行期失败不建议从 `Logger::info()` 这类便捷 API 抛出异常。Nova Logger 默认采用 sink 保存错误状态，并通过线程安全的 `lastError()` 查询。

统一接口：

```cpp
class Sink {
public:
    virtual ~Sink() = default;
    virtual void log(const LogMsg& msg) = 0;
    virtual void flush() = 0;
    virtual std::optional<std::string> lastError() const = 0;
};
```

Phase 1 的 `ConsoleSink::lastError()` 可以始终返回 `std::nullopt`。Phase 2 起，`FileSink` 和后续文件类 sink 必须在写入、flush 或滚动失败后更新 `lastError()`。
`lastError()` 必须返回错误字符串副本，不能返回内部状态引用。

---

## 11. 线程安全与生命周期

核心原则：

- `Logger::log()` 可并发调用。
- `Sink::log()` 可并发调用，sink 内部自行同步。
- `Sink::lastError()` 可与 `log()`、`flush()` 并发调用，必须返回一致的错误状态副本。
- formatter 默认不保证共享可变状态安全。
- 每个 sink 持有自己的 formatter，避免多个 sink 共享 formatter 产生数据竞争。

生命周期规则：

- `Logger` 拥有 logger name。
- `Logger` 共享持有 sink。
- sink 独占持有 formatter。
- `LogMsg::message` 必须拥有字符串内容。
- 任何进入异步队列的数据必须完整拥有，不能引用调用方栈对象。

可变配置规则：

- Phase 1 允许运行期修改 logger level。
- Phase 1 不允许日志调用期间并发修改 sink 列表。
- 如果未来支持动态增删 sink，需要引入额外同步或 copy-on-write sink 列表。

---

## 12. 测试计划

### 12.1 Phase 1 测试

- `LogLevel` 过滤：低于当前 level 的日志不输出。
- `Off` 级别：所有日志都不输出。
- `Logger` 构造：name 被正确写入 `LogMsg`。
- `SourceLocation`：宏能捕获文件名、函数名和行号。
- `NOVA_LOG_NO_SOURCE_LOC`：宏传入空调用点。
- `NOVA_LOG_ACTIVE_LEVEL`：被裁剪级别的宏不求值参数。
- `NOVA_LOGGER_*`：使用传入的 pointer-like logger，不依赖默认 registry。
- `ConsoleSink`：`Info` 输出到 `stdout`，`Error` 输出到 `stderr`。
- `Formatter`：默认格式包含时间、级别、logger、线程、位置和正文。
- 并发写入：多个线程同时写日志不产生交错的半行输出。
- `ConsoleSink::lastError()`：默认返回 `std::nullopt`，并且可并发调用。

### 12.2 Phase 2 测试

- `FileSink` 创建不存在的父目录。
- `FileSink` 打开非法路径时失败。
- 默认 append 模式不清空旧文件。
- 非 append 模式按设计截断旧文件。
- `flush()` 后文件内容可被读取。
- 写入失败时错误状态可被观察。
- `FileSink::lastError()` 与 `log()`、`flush()` 并发调用时不产生数据竞争。
- 不同 sink 可以使用不同 formatter。

### 12.3 Phase 3 测试

- 当前文件超过大小限制后触发滚动。
- `max_files` 之外的旧文件被删除。
- 单条超大日志不被拆成多行或多个文件。
- 滚动失败时对象不崩溃，错误状态可观察。
- 并发写入和滚动不产生数据竞争。

### 12.4 默认 Logger Registry 测试

- registry 初始化后 `defaultLoggerRaw()` 不为空。
- `defaultLogger()` 返回可持有的 `std::shared_ptr<Logger>` 快照。
- `setDefaultLogger()` 能替换默认 logger。
- `setDefaultLogger(nullptr)` 被拒绝。
- `NOVA_LOG_*`：使用默认 logger。
- 全局便捷函数调用默认 logger。
- 替换默认 logger 时不破坏已有 logger 的共享生命周期。
- `defaultLoggerRaw()` 只在默认 logger 不并发替换的场景使用，返回值不被跨语句保存。

### 12.5 Phase 4 测试

- `AsyncSink` 析构时 flush 队列中剩余日志。
- 队列满时符合选定策略。
- 后台线程异常不会导致进程终止。
- 入队后的日志不依赖调用方临时字符串生命周期。

---

## 13. 演进路线

推荐实现顺序：

1. Phase 1：`LogLevel`、`SourceLocation`、`LogMsg`、`Formatter`、`Sink`、`ConsoleSink`、`Logger`、类 spdlog 宏接口。
2. Phase 2：`FileSink`、文件失败处理、formatter pattern 的最小增强。
3. Phase 3：`RollingFileSink`、滚动策略、错误状态查询。
4. Phase 4：`AsyncSink`、后台线程、队列策略、优雅关闭。
5. Registry 增强：默认 logger registry、`defaultLoggerRaw()`、`setDefaultLogger()`、全局便捷函数。
6. 后续增强：Config 适配、fmtlib 可选集成。

全局 logger registry 不应早于文件日志。过早引入全局状态会增加测试和生命周期复杂度。
