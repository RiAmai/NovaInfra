#pragma once

#include "namespace.h"

NOVA_NAMESPACE_BEGIN
namespace core {
constexpr int kMajor = 0;
constexpr int kMinor = 1;
constexpr int kPatch = 0;

inline int MajorVersion()
{
    return kMajor;
}
inline int MinorVersion()
{
    return kMinor;
}
inline int PatchVersion()
{
    return kPatch;
}
}  // namespace core
NOVA_NAMESPACE_END  // namespace nova