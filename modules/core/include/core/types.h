#pragma once

#include "./namespace.h"

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <memory>

NOVA_NAMESPACE_BEGIN
using Uint8 = std::uint8_t;
using Uint16 = std::uint16_t;
using Uint32 = std::uint32_t;
using Uint64 = std::uint64_t;

using Int8 = std::int8_t;
using Int16 = std::int16_t;
using Int32 = std::int32_t;
using Int64 = std::int64_t;

using Byte = Uint8;
using ByteBuffer = std::vector<Byte>;

#if defined(_WIN32) && defined(NOVA_WCHAR_FILENAMES)
using Filename = std::wstring;
#  define NOVA_FILENAME_INNER(s) L##s
#  define NOVA_FILENAME(s)       NOVA_FILENAME_INNER(s)
#else
using Filename = std::string;
#  define NOVA_FILENAME(s) s
#endif

/** @brief Millisecond duration type. */
using Milliseconds = std::chrono::milliseconds;
using SystemClock = std::chrono::system_clock;
/** @brief Steady-clock time point for measuring intervals. */
using SteadyTimePoint = std::chrono::steady_clock::time_point;
using SystemTimePoint = std::chrono::system_clock::time_point;

/** @brief Generic callable task with no arguments and no return value. */
using Task = std::function<void()>;

#if __cplusplus >= 201402L  // C++14 and beyond
using std::enable_if_t;
using std::make_unique;
#else
template <bool B, class T = void>
using enable_if_t = typename std::enable_if<B, T>::type;

template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args &&...args)
{
    static_assert(!std::is_array<T>::value, "arrays not supported");
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
#endif

template <typename T, typename U, enable_if_t<!std::is_same<T, U>::value, int> = 0>
constexpr T conditional_static_cast(U value)
{
    return static_cast<T>(value);
}

template <typename T, typename U, enable_if_t<std::is_same<T, U>::value, int> = 0>
constexpr T conditional_static_cast(U value)
{
    return value;
}
NOVA_NAMESPACE_END  // namespace nova
