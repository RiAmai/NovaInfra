#include <logger/registry.h>

int main()
{
    auto &registry = novainfra::log::Registry::instance();
    return registry.defaultLogger() ? 0 : 1;
}
