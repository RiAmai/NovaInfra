#pragma once

#include <cstdlib>
#include <iostream>
#include <string_view>
#include "./namespace.h"

NOVA_NAMESPACE_BEGIN
inline void AssertFailed(std::string_view expression, std::string_view file, int line,
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
NOVA_NAMESPACE_END  // namespace nova

#if !defined(NDEBUG) || defined(NOVA_ENABLE_ASSERT)
#  define NOVA_ASSERT(expr)                                                                                  \
      do {                                                                                                   \
          if (!(expr)) {                                                                                     \
              ::nova::AssertFailed(#expr, __FILE__, __LINE__);                                               \
          }                                                                                                  \
      } while (0)

#  define NOVA_ASSERT_MSG(expr, msg)                                                                         \
      do {                                                                                                   \
          if (!(expr)) {                                                                                     \
              ::nova::AssertFailed(#expr, __FILE__, __LINE__, msg);                                          \
          }                                                                                                  \
      } while (0)

#else
#  define NOVA_ASSERT(expr)                                                                                  \
      do {                                                                                                   \
          (void)sizeof(expr);                                                                                \
      } while (0)
#  define NOVA_ASSERT_MSG(expr, msg)                                                                         \
      do {                                                                                                   \
          (void)sizeof(expr);                                                                                \
          (void)sizeof(msg);                                                                                 \
      } while (0)
#endif