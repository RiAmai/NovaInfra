#pragma once

#include <core/namespace.h>

NOVAINFRA_NAMESPACE_BEGIN
namespace log::async {

enum class AsyncOverflowPolicy {
    BLOCK,
    OVER_RUN_OLDEST,
    DISCARD_NEW,
};

}  // namespace log::async
NOVAINFRA_NAMESPACE_END
