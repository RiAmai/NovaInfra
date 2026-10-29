#pragma once

#include <memory>
#include "./macros.h"

/**
 * @brief CRTP
 *
 */
template <class T>
class Singleton
{
public:
    typedef std::shared_ptr<T> Ptr;
    static Ptr instance()
    {
        // 不要用std::make_shared，因为T构造函数是privte，只给了本地友元，没有给标准库友元
        static Ptr singleton = Ptr(new T);
        return singleton;
    }

protected:
    Singleton() = default;
    virtual ~Singleton() = default;

    NOVAINFRA_DISABLE_COPY(Singleton)
};
