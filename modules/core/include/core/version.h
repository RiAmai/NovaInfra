#pragma once

#include "namespace.h"

NOVAINFRA_NAMESPACE_BEGIN
namespace core {
constexpr int kMajor = 0;
constexpr int kMinor = 1;
constexpr int kPatch = 0;

inline int majorVersion()
{
    return kMajor;
}
inline int minorVersion()
{
    return kMinor;
}
inline int patchVersion()
{
    return kPatch;
}
}  // namespace core
NOVAINFRA_NAMESPACE_END  // namespace novainfra