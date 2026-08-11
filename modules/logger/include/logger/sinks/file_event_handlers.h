#pragma once

#include <core/namespace.h>
#include <core/types.h>

#include <cstdio>
#include <functional>

NOVA_NAMESPACE_BEGIN
namespace log::sinks {

struct FileEventHandlers {
    FileEventHandlers() : beforeOpen(nullptr), afterOpen(nullptr), beforeClose(nullptr), afterClose(nullptr)
    {
    }

    std::function<void(const Filename &filename)> beforeOpen;
    std::function<void(const Filename &filename, std::FILE *fileStream)> afterOpen;
    std::function<void(const Filename &filename, std::FILE *fileStream)> beforeClose;
    std::function<void(const Filename &filename)> afterClose;
};

}  // namespace log::sinks
NOVA_NAMESPACE_END
