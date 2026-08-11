#include <logger/registry.h>

int main()
{
    auto &registry = nova::log::Registry::instance();
    return registry.defaultLogger() ? 0 : 1;
}
