#pragma once

#include <core/namespace.h>

#include <mutex>
#include <type_traits>

NOVA_NAMESPACE_BEGIN
namespace log::details {

struct NullMutex {
    void lock() const {}
    void unlock() const {}
};

struct ConsoleMutex {
    using Mutex = std::mutex;

    static Mutex &mutex()
    {
        static Mutex sharedMutex;
        return sharedMutex;
    }
};

struct ConsoleNullMutex {
    using Mutex = NullMutex;

    static Mutex &mutex()
    {
        static Mutex sharedMutex;
        return sharedMutex;
    }
};

template <typename MutexOrPolicy, typename = void>
struct ConsoleMutexTraits {
    using Mutex = MutexOrPolicy;

    static Mutex &mutex()
    {
        static Mutex sharedMutex;
        return sharedMutex;
    }
};

template <typename MutexPolicy>
struct ConsoleMutexTraits<MutexPolicy, std::void_t<typename MutexPolicy::Mutex>> {
    using Mutex = typename MutexPolicy::Mutex;

    static Mutex &mutex() { return MutexPolicy::mutex(); }
};

}  // namespace log::details
NOVA_NAMESPACE_END
