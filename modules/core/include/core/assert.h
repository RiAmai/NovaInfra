#pragma once

#include <cstdlib>
#include <iostream>
#include <string_view>
#include "./namespace.h"

NOVAINFRA_NAMESPACE_BEGIN
inline void assertFailed(std::string_view expression, std::string_view file, int line,
                         std::string_view message = "")
{
    std::cerr << "[ASSERTION FAILED]\n"
              << "  Expression: " << expression << "\n"
              << "  File      : " << file << ":" << line << "\n";
    if (!message.empty()) {
        std::cerr << "  Message   : " << message << "\n";
    }
    std::abort();
}
NOVAINFRA_NAMESPACE_END  // namespace novainfra

#if !defined(NDEBUG) || defined(NOVAINFRA_ENABLE_ASSERT)
#  define NOVAINFRA_ASSERT(expr)                                                                             \
      do {                                                                                                   \
          if (!(expr)) {                                                                                     \
              ::novainfra::assertFailed(#expr, __FILE__, __LINE__);                                          \
          }                                                                                                  \
      } while (0)

#  define NOVAINFRA_ASSERT_MSG(expr, msg)                                                                    \
      do {                                                                                                   \
          if (!(expr)) {                                                                                     \
              ::novainfra::assertFailed(#expr, __FILE__, __LINE__, msg);                                     \
          }                                                                                                  \
      } while (0)

#else
#  define NOVAINFRA_ASSERT(expr)                                                                             \
      do {                                                                                                   \
          (void)sizeof(expr);                                                                                \
      } while (0)
#  define NOVAINFRA_ASSERT_MSG(expr, msg)                                                                    \
      do {                                                                                                   \
          (void)sizeof(expr);                                                                                \
          (void)sizeof(msg);                                                                                 \
      } while (0)
#endif