# NovaCore

Modern C++ Infrastructure for Linux Applications

NovaCore is a modular Modern C++ framework that provides reusable infrastructure components for building reliable, maintainable and scalable Linux applications.

## Why NovaCore?

During daily development, many common components such as logging, configuration management, thread pools, event dispatching and database management are repeatedly implemented across projects.

NovaCore aims to provide a clean, modular and reusable Modern C++ infrastructure that can be shared across different applications while serving as a long-term engineering practice.

## Architecture

```tex
NovaCore
├── Core
├── Logger
├── Config
├── ThreadPool
├── SqlitePool
├── EventBus
├── Simulator
└── Examples
```

## Features

- Modern C++17/20
- Cross-platform architecture
- Modular design
- CMake build system
- Unit testing
- Thread-safe components
- RAII-based resource management
- Extensible architecture

## Roadmap

- [x] Project initialization
- [x] Logger
- [ ] ThreadPool
- [ ] Config
- [ ] SqlitePool
- [ ] EventBus
- [ ] Plugin
- [ ] Device Simulator

## Logger

The Logger module is complete and provides:

- Synchronous and asynchronous loggers with bounded queue overflow policies
- Thread-safe and single-threaded console, basic file, and rotating file sinks
- fmt-style formatting, configurable patterns, level filtering, and flush levels
- Default and named logger management through the Registry
- Compile-time log macros with optional source locations
- Backtrace buffering, file event callbacks, and configurable error handling

For complete usage instructions, see the [Nova Logger Guide](docs/logger.md).

Nova Logger requires C++17 and Linux. Multiple asynchronous workers require thread-safe sinks and do
not guarantee ordering between messages. Call `nova::log::shutdown()` before process exit to drain the
Registry worker and flush queued messages.

Configure and build NovaCore with the Logger examples enabled by default:

```bash
cmake --preset linux-ninja-debug
cmake --build --preset build-linux-ninja-debug -j2
```

Create a synchronous console logger:

```cpp
#include <logger/novalog.h>

int main()
{
    auto logger = nova::log::factories::createConsoleLoggerMt("app");
    logger->setLevel(nova::log::Level::INFO);
    logger->info("server started on port {}", 8080);
    logger->flush();
}
```

Create an asynchronous console logger backed by the Registry worker:

```cpp
#include <logger/novalog.h>

int main()
{
    nova::log::async::initAsyncLogThreadPool(8192, 1);
    auto logger = nova::log::factories::createConsoleLoggerMt<
        nova::log::factories::AsyncFactory>("async-app");

    logger->info("request {} accepted", 17);
    nova::log::shutdown();
}
```

## Design Principles

NovaCore follows several engineering principles:

- Modern C++ first
- RAII over manual resource management
- Composition over inheritance
- Thread safety by design
- Clean and maintainable architecture
- Small and focused modules

## Build

`Nova::Logger` bundles the pinned fmt 12.2.0 headers in header-only mode. Installed consumers only need
`find_package(NovaCore)` and `target_link_libraries(... Nova::Logger)`; NovaCore does not install or require
a separate fmt library or fmt CMake package. Installations include NovaCore's `LICENSE`,
`THIRD_PARTY_NOTICES.md`, and the upstream fmt license under `share/licenses/NovaCore`.

## Third-Party Software

NovaCore contains MIT-licensed code derived from spdlog and bundles fmt in header-only mode. Copyright,
license text, and source mappings are documented in [Third-Party Notices](THIRD_PARTY_NOTICES.md).

## Philosophy

NovaCore is not intended to replace mature open-source libraries.

Instead, it is an engineering playground for exploring Modern C++ design, architecture, concurrency and software engineering practices through carefully designed reusable components.
