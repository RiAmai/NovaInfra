#include "environment.h"

#include <unistd.h>

NOVAINFRA_NAMESPACE_BEGIN
namespace config {
namespace detail {

std::vector<std::string> currentEnvironment()
{
    std::vector<std::string> result;
    for (char *const *item = environ; item && *item; ++item)
        result.emplace_back(*item);
    return result;
}

}  // namespace detail
}  // namespace config
NOVAINFRA_NAMESPACE_END
