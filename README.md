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
- [ ] Logger
- [ ] ThreadPool
- [ ] Config
- [ ] SqlitePool
- [ ] EventBus
- [ ] Plugin
- [ ] Device Simulator

## Example

## Design Principles

NovaCore follows several engineering principles:

- Modern C++ first
- RAII over manual resource management
- Composition over inheritance
- Thread safety by design
- Clean and maintainable architecture
- Small and focused modules

## Build

## Philosophy

NovaCore is not intended to replace mature open-source libraries.

Instead, it is an engineering playground for exploring Modern C++ design, architecture, concurrency and software engineering practices through carefully designed reusable components.
