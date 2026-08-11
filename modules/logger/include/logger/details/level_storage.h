#pragma once

#include <core/namespace.h>

#include <atomic>

NOVA_NAMESPACE_BEGIN
namespace log::details {

using LevelType = std::atomic<int>;

}  // namespace log::details
NOVA_NAMESPACE_END
