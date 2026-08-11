#pragma once

#include <core/namespace.h>

NOVA_NAMESPACE_BEGIN
namespace log::async {

enum class AsyncOverflowPolicy {
    BLOCK,
    OVER_RUN_OLDEST,
    DISCARD_NEW,
};

}  // namespace log::async
NOVA_NAMESPACE_END
