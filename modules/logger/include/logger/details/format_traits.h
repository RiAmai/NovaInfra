#pragma once

#include <logger/types.h>

#include <core/namespace.h>

#include <fmt/format.h>

#include <type_traits>

NOVA_NAMESPACE_BEGIN
namespace log::details {

template <class T>
using RemoveCvrefType = typename std::remove_cv<typename std::remove_reference<T>::type>::type;

// clang doesn't like a SFINAE-disabled constructor in std::is_convertible, so repeat fmt's
// basic_format_string condition here. fmt runtime strings convert to basic_format_string but not
// basic_string_view.
template <class T, class Char = char>
struct IsConvertibleToBasicFormatString
    : std::integral_constant<bool, std::is_convertible<T, fmt::basic_string_view<Char>>::value ||
                                       std::is_same<RemoveCvrefType<T>, FmtRuntimeString<Char>>::value> {
};

template <class T>
struct IsConvertibleToAnyFormatString
    : std::integral_constant<bool, IsConvertibleToBasicFormatString<T, char>::value ||
                                       IsConvertibleToBasicFormatString<T, wchar_t>::value> {
};

}  // namespace log::details
NOVA_NAMESPACE_END
