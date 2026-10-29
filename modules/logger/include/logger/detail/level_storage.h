#pragma once

#include <logger/types.h>

#include <core/namespace.h>

#include <atomic>

NOVAINFRA_NAMESPACE_BEGIN
namespace log::detail {

using LevelType = std::atomic<Level>;

}  // namespace log::detail
NOVAINFRA_NAMESPACE_END
