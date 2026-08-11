#pragma once

#include <logger/global.h>
#include <logger/types.h>

#ifndef NOVA_LOG_ACTIVE_LEVEL
#  define NOVA_LOG_ACTIVE_LEVEL NOVA_LOG_LEVEL_TRACE
#endif

#ifndef NOVA_LOG_FUNCTION
#  define NOVA_LOG_FUNCTION __func__
#endif

#ifndef NOVA_LOG_NO_SOURCE_LOC
#  define NOVA_LOGGER_CALL(logger, level, ...)                                                               \
      (logger)->log(nova::log::SourceLocation::fromStatic(__FILE__, __LINE__, NOVA_LOG_FUNCTION), level,     \
                    __VA_ARGS__)
#else
#  define NOVA_LOGGER_CALL(logger, level, ...) (logger)->log(nova::log::SourceLocation{}, level, __VA_ARGS__)
#endif

#if NOVA_LOG_ACTIVE_LEVEL <= NOVA_LOG_LEVEL_TRACE
#  define NOVA_LOGGER_TRACE(logger, ...) NOVA_LOGGER_CALL(logger, nova::log::Level::TRACE, __VA_ARGS__)
#  define NOVA_LOG_TRACE(...)            NOVA_LOGGER_TRACE(nova::log::defaultLogger(), __VA_ARGS__)
#else
#  define NOVA_LOGGER_TRACE(logger, ...) (void)0
#  define NOVA_LOG_TRACE(...)            (void)0
#endif

#if NOVA_LOG_ACTIVE_LEVEL <= NOVA_LOG_LEVEL_DEBUG
#  define NOVA_LOGGER_DEBUG(logger, ...) NOVA_LOGGER_CALL(logger, nova::log::Level::DEBUG, __VA_ARGS__)
#  define NOVA_LOG_DEBUG(...)            NOVA_LOGGER_DEBUG(nova::log::defaultLogger(), __VA_ARGS__)
#else
#  define NOVA_LOGGER_DEBUG(logger, ...) (void)0
#  define NOVA_LOG_DEBUG(...)            (void)0
#endif

#if NOVA_LOG_ACTIVE_LEVEL <= NOVA_LOG_LEVEL_INFO
#  define NOVA_LOGGER_INFO(logger, ...) NOVA_LOGGER_CALL(logger, nova::log::Level::INFO, __VA_ARGS__)
#  define NOVA_LOG_INFO(...)            NOVA_LOGGER_INFO(nova::log::defaultLogger(), __VA_ARGS__)
#else
#  define NOVA_LOGGER_INFO(logger, ...) (void)0
#  define NOVA_LOG_INFO(...)            (void)0
#endif

#if NOVA_LOG_ACTIVE_LEVEL <= NOVA_LOG_LEVEL_WARN
#  define NOVA_LOGGER_WARN(logger, ...) NOVA_LOGGER_CALL(logger, nova::log::Level::WARN, __VA_ARGS__)
#  define NOVA_LOG_WARN(...)            NOVA_LOGGER_WARN(nova::log::defaultLogger(), __VA_ARGS__)
#else
#  define NOVA_LOGGER_WARN(logger, ...) (void)0
#  define NOVA_LOG_WARN(...)            (void)0
#endif

#if NOVA_LOG_ACTIVE_LEVEL <= NOVA_LOG_LEVEL_ERROR
#  define NOVA_LOGGER_ERROR(logger, ...) NOVA_LOGGER_CALL(logger, nova::log::Level::ERROR, __VA_ARGS__)
#  define NOVA_LOG_ERROR(...)            NOVA_LOGGER_ERROR(nova::log::defaultLogger(), __VA_ARGS__)
#else
#  define NOVA_LOGGER_ERROR(logger, ...) (void)0
#  define NOVA_LOG_ERROR(...)            (void)0
#endif

#if NOVA_LOG_ACTIVE_LEVEL <= NOVA_LOG_LEVEL_CRITICAL
#  define NOVA_LOGGER_CRITICAL(logger, ...) NOVA_LOGGER_CALL(logger, nova::log::Level::CRITICAL, __VA_ARGS__)
#  define NOVA_LOG_CRITICAL(...)            NOVA_LOGGER_CRITICAL(nova::log::defaultLogger(), __VA_ARGS__)
#else
#  define NOVA_LOGGER_CRITICAL(logger, ...) (void)0
#  define NOVA_LOG_CRITICAL(...)            (void)0
#endif
