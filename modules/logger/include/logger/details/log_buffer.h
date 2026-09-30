#pragma once

#include <core/namespace.h>

#include <fmt/format.h>

#include <cstddef>

NOVAINFRA_NAMESPACE_BEGIN
namespace log::details {

constexpr std::size_t kInlineLogBufferSize = 256;
using LogMemoryBuffer = fmt::basic_memory_buffer<char, kInlineLogBufferSize>;

}  // namespace log::details
NOVAINFRA_NAMESPACE_END
