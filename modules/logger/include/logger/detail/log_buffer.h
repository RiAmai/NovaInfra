#pragma once

#include <core/namespace.h>

#include <fmt/format.h>

#include <cstddef>

NOVAINFRA_NAMESPACE_BEGIN
namespace log::detail {

constexpr std::size_t kInlineLogBufferSize = 256;
using LogMemoryBuffer = fmt::basic_memory_buffer<char, kInlineLogBufferSize>;

}  // namespace log::detail
NOVAINFRA_NAMESPACE_END
