#pragma once

#include <cstdio>
#include <cstdlib>
#include <exception>

#if defined(NOVA_LOG_NO_EXCEPTIONS) && !defined(NOVA_NO_EXCEPTIONS)
#  define NOVA_NO_EXCEPTIONS
#endif

#ifndef NOVA_INLINE
#  define NOVA_INLINE inline
#endif

#ifndef NOVA_NOEXCEPT
#  define NOVA_NOEXCEPT noexcept
#endif

#ifndef NOVA_CONSTEXPR
#  define NOVA_CONSTEXPR constexpr
#endif

#ifndef NOVA_UNUSED
#  define NOVA_UNUSED(x) (void)(x)
#endif

#ifndef NOVA_STRINGIFY_HELPER
#  define NOVA_STRINGIFY_HELPER(x) #x
#endif

#ifndef NOVA_STRINGIFY
#  define NOVA_STRINGIFY(x) NOVA_STRINGIFY_HELPER(x)
#endif

#ifndef NOVA_CONCAT_HELPER
#  define NOVA_CONCAT_HELPER(a, b) a##b
#endif

#ifndef NOVA_CONCAT
#  define NOVA_CONCAT(a, b) NOVA_CONCAT_HELPER(a, b)
#endif

#ifndef NOVA_DISABLE_COPY
#  define NOVA_DISABLE_COPY(Class)                                                                           \
      Class(const Class &) = delete;                                                                         \
      Class &operator=(const Class &) = delete;
#endif

#ifndef NOVA_DISABLE_MOVE
#  define NOVA_DISABLE_MOVE(Class)                                                                           \
      Class(Class &&) = delete;                                                                              \
      Class &operator=(Class &&) = delete;
#endif

#ifndef NOVA_DISABLE_COPY_MOVE
#  define NOVA_DISABLE_COPY_MOVE(Class)                                                                      \
      NOVA_DISABLE_COPY(Class)                                                                               \
      NOVA_DISABLE_MOVE(Class)
#endif

// Exception syntax is centralized here. Handler bodies are removed entirely
// when exceptions are disabled, so call sites stay valid under -fno-exceptions.
#ifdef NOVA_NO_EXCEPTIONS
#  define NOVA_TRY
#  define NOVA_THROW(ex)                                                                                     \
      do {                                                                                                   \
          const auto &novaException = (ex);                                                                  \
          std::fprintf(stderr, "nova fatal error: %s\n", novaException.what());                              \
          std::abort();                                                                                      \
      } while (0)
#  define NOVA_CATCH_STD
#  define NOVA_CATCH_STD_BODY(...)
#  define NOVA_CATCH_ALL_BODY(...)
#  define NOVA_RETHROW_CURRENT() std::terminate()
#  define NOVA_RETHROW_EXCEPTION(exceptionPtr)                                                               \
      do {                                                                                                   \
          NOVA_UNUSED(exceptionPtr);                                                                         \
          std::terminate();                                                                                  \
      } while (0)
#else
#  define NOVA_TRY       try
#  define NOVA_THROW(ex) throw(ex)
#  define NOVA_CATCH_STD                                                                                     \
      catch (const std::exception &) {}
#  define NOVA_CATCH_STD_BODY(...) catch (const std::exception &ex) { __VA_ARGS__ }
#  define NOVA_CATCH_ALL_BODY(...) catch (...) { __VA_ARGS__ }
#  define NOVA_RETHROW_CURRENT() throw
#  define NOVA_RETHROW_EXCEPTION(exceptionPtr) std::rethrow_exception(exceptionPtr)
#endif
