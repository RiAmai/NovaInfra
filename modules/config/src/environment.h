#pragma once

#include <core/namespace.h>

#include <string>
#include <vector>

NOVAINFRA_NAMESPACE_BEGIN
namespace config {
namespace detail {

// Returns a point-in-time copy of the current process environment.
std::vector<std::string> currentEnvironment();

}  // namespace detail
}  // namespace config
NOVAINFRA_NAMESPACE_END
