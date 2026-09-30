#pragma once

#include <core/namespace.h>

#include <atomic>

NOVAINFRA_NAMESPACE_BEGIN
namespace log::details {

using LevelType = std::atomic<int>;

}  // namespace log::details
NOVAINFRA_NAMESPACE_END
