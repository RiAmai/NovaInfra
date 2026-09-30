#pragma once

#include <logger/global.h>
#include <logger/types.h>

#ifndef NOVAINFRA_LOG_ACTIVE_LEVEL
#  define NOVAINFRA_LOG_ACTIVE_LEVEL NOVAINFRA_LOG_LEVEL_TRACE
#endif

#ifndef NOVAINFRA_LOG_FUNCTION
#  define NOVAINFRA_LOG_FUNCTION __func__
#endif

#ifndef NOVAINFRA_LOG_NO_SOURCE_LOC
#  define NOVAINFRA_LOGGER_CALL(logger, level, ...)                                                          \
      (logger)->log(novainfra::log::SourceLocation::fromStatic(__FILE__, __LINE__, NOVAINFRA_LOG_FUNCTION),  \
                    level, __VA_ARGS__)
#else
#  define NOVAINFRA_LOGGER_CALL(logger, level, ...)                                                          \
      (logger)->log(novainfra::log::SourceLocation{}, level, __VA_ARGS__)
#endif

#if NOVAINFRA_LOG_ACTIVE_LEVEL <= NOVAINFRA_LOG_LEVEL_TRACE
#  define NOVAINFRA_LOGGER_TRACE(logger, ...)                                                                \
      NOVAINFRA_LOGGER_CALL(logger, novainfra::log::Level::TRACE, __VA_ARGS__)
#  define NOVAINFRA_LOG_TRACE(...) NOVAINFRA_LOGGER_TRACE(novainfra::log::defaultLogger(), __VA_ARGS__)
#else
#  define NOVAINFRA_LOGGER_TRACE(logger, ...) (void)0
#  define NOVAINFRA_LOG_TRACE(...)            (void)0
#endif

#if NOVAINFRA_LOG_ACTIVE_LEVEL <= NOVAINFRA_LOG_LEVEL_DEBUG
#  define NOVAINFRA_LOGGER_DEBUG(logger, ...)                                                                \
      NOVAINFRA_LOGGER_CALL(logger, novainfra::log::Level::DEBUG, __VA_ARGS__)
#  define NOVAINFRA_LOG_DEBUG(...) NOVAINFRA_LOGGER_DEBUG(novainfra::log::defaultLogger(), __VA_ARGS__)
#else
#  define NOVAINFRA_LOGGER_DEBUG(logger, ...) (void)0
#  define NOVAINFRA_LOG_DEBUG(...)            (void)0
#endif

#if NOVAINFRA_LOG_ACTIVE_LEVEL <= NOVAINFRA_LOG_LEVEL_INFO
#  define NOVAINFRA_LOGGER_INFO(logger, ...)                                                                 \
      NOVAINFRA_LOGGER_CALL(logger, novainfra::log::Level::INFO, __VA_ARGS__)
#  define NOVAINFRA_LOG_INFO(...) NOVAINFRA_LOGGER_INFO(novainfra::log::defaultLogger(), __VA_ARGS__)
#else
#  define NOVAINFRA_LOGGER_INFO(logger, ...) (void)0
#  define NOVAINFRA_LOG_INFO(...)            (void)0
#endif

#if NOVAINFRA_LOG_ACTIVE_LEVEL <= NOVAINFRA_LOG_LEVEL_WARN
#  define NOVAINFRA_LOGGER_WARN(logger, ...)                                                                 \
      NOVAINFRA_LOGGER_CALL(logger, novainfra::log::Level::WARN, __VA_ARGS__)
#  define NOVAINFRA_LOG_WARN(...) NOVAINFRA_LOGGER_WARN(novainfra::log::defaultLogger(), __VA_ARGS__)
#else
#  define NOVAINFRA_LOGGER_WARN(logger, ...) (void)0
#  define NOVAINFRA_LOG_WARN(...)            (void)0
#endif

#if NOVAINFRA_LOG_ACTIVE_LEVEL <= NOVAINFRA_LOG_LEVEL_ERROR
#  define NOVAINFRA_LOGGER_ERROR(logger, ...)                                                                \
      NOVAINFRA_LOGGER_CALL(logger, novainfra::log::Level::ERROR, __VA_ARGS__)
#  define NOVAINFRA_LOG_ERROR(...) NOVAINFRA_LOGGER_ERROR(novainfra::log::defaultLogger(), __VA_ARGS__)
#else
#  define NOVAINFRA_LOGGER_ERROR(logger, ...) (void)0
#  define NOVAINFRA_LOG_ERROR(...)            (void)0
#endif

#if NOVAINFRA_LOG_ACTIVE_LEVEL <= NOVAINFRA_LOG_LEVEL_CRITICAL
#  define NOVAINFRA_LOGGER_CRITICAL(logger, ...)                                                             \
      NOVAINFRA_LOGGER_CALL(logger, novainfra::log::Level::CRITICAL, __VA_ARGS__)
#  define NOVAINFRA_LOG_CRITICAL(...) NOVAINFRA_LOGGER_CRITICAL(novainfra::log::defaultLogger(), __VA_ARGS__)
#else
#  define NOVAINFRA_LOGGER_CRITICAL(logger, ...) (void)0
#  define NOVAINFRA_LOG_CRITICAL(...)            (void)0
#endif
