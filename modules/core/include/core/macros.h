#pragma once

#include <cstdio>
#include <cstdlib>
#include <exception>

#if defined(NOVAINFRA_LOG_NO_EXCEPTIONS) && !defined(NOVAINFRA_NO_EXCEPTIONS)
#  define NOVAINFRA_NO_EXCEPTIONS
#endif

#ifndef NOVAINFRA_INLINE
#  define NOVAINFRA_INLINE inline
#endif

#ifndef NOVAINFRA_NOEXCEPT
#  define NOVAINFRA_NOEXCEPT noexcept
#endif

#ifndef NOVAINFRA_FINAL
#  define NOVAINFRA_FINAL final
#endif

#ifndef NOVAINFRA_OVERRIDE
#  define NOVAINFRA_OVERRIDE override
#endif

#ifndef NOVAINFRA_CONSTEXPR
#  define NOVAINFRA_CONSTEXPR constexpr
#endif

#ifndef NOVAINFRA_UNUSED
#  define NOVAINFRA_UNUSED(x) (void)(x)
#endif

#ifndef NOVAINFRA_STRINGIFY_HELPER
#  define NOVAINFRA_STRINGIFY_HELPER(x) #x
#endif

#ifndef NOVAINFRA_STRINGIFY
#  define NOVAINFRA_STRINGIFY(x) NOVAINFRA_STRINGIFY_HELPER(x)
#endif

#ifndef NOVAINFRA_CONCAT_HELPER
#  define NOVAINFRA_CONCAT_HELPER(a, b) a##b
#endif

#ifndef NOVAINFRA_CONCAT
#  define NOVAINFRA_CONCAT(a, b) NOVAINFRA_CONCAT_HELPER(a, b)
#endif

#ifndef NOVAINFRA_DISABLE_COPY
#  define NOVAINFRA_DISABLE_COPY(Class)                                                                      \
      Class(const Class &) = delete;                                                                         \
      Class &operator=(const Class &) = delete;
#endif

#ifndef NOVAINFRA_DISABLE_MOVE
#  define NOVAINFRA_DISABLE_MOVE(Class)                                                                      \
      Class(Class &&) = delete;                                                                              \
      Class &operator=(Class &&) = delete;
#endif

#ifndef NOVAINFRA_DISABLE_COPY_MOVE
#  define NOVAINFRA_DISABLE_COPY_MOVE(Class)                                                                 \
      NOVAINFRA_DISABLE_COPY(Class)                                                                          \
      NOVAINFRA_DISABLE_MOVE(Class)
#endif

// Exception syntax is centralized here. Handler bodies are removed entirely
// when exceptions are disabled, so call sites stay valid under -fno-exceptions.
#ifdef NOVAINFRA_NO_EXCEPTIONS
#  define NOVAINFRA_TRY
#  define NOVAINFRA_THROW(ex)                                                                                \
      do {                                                                                                   \
          const auto &novainfraException = (ex);                                                             \
          std::fprintf(stderr, "novainfra fatal error: %s\n", novainfraException.what());                    \
          std::abort();                                                                                      \
      } while (0)
#  define NOVAINFRA_CATCH_STD
#  define NOVAINFRA_CATCH_STD_BODY(...)
#  define NOVAINFRA_CATCH_ALL_BODY(...)
#  define NOVAINFRA_RETHROW_CURRENT() std::terminate()
#  define NOVAINFRA_RETHROW_EXCEPTION(exceptionPtr)                                                          \
      do {                                                                                                   \
          NOVAINFRA_UNUSED(exceptionPtr);                                                                    \
          std::terminate();                                                                                  \
      } while (0)
#else
#  define NOVAINFRA_TRY       try
#  define NOVAINFRA_THROW(ex) throw(ex)
#  define NOVAINFRA_CATCH_STD                                                                                \
      catch (const std::exception &) {}
#  define NOVAINFRA_CATCH_STD_BODY(...)                                                                      \
      catch (const std::exception &ex)                                                                       \
      {                                                                                                      \
          __VA_ARGS__                                                                                        \
      }
#  define NOVAINFRA_CATCH_ALL_BODY(...)                                                                      \
      catch (...)                                                                                            \
      {                                                                                                      \
          __VA_ARGS__                                                                                        \
      }
#  define NOVAINFRA_RETHROW_CURRENT()               throw
#  define NOVAINFRA_RETHROW_EXCEPTION(exceptionPtr) std::rethrow_exception(exceptionPtr)
#endif
