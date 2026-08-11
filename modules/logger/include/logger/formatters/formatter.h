#pragma once

#include <logger/types.h>
#include <logger/details/log_msg.h>

#include <core/export.h>
#include <core/namespace.h>

#include <memory>
#include <string>

NOVA_NAMESPACE_BEGIN
namespace log::formatters {

class NOVA_LIB_PORT IFormatter
{
public:
    virtual ~IFormatter() = default;
    virtual void format(const details::LogMsg &msg, std::string &dst) = 0;
    virtual std::unique_ptr<IFormatter> clone() const = 0;
};

}  // namespace log::formatters
NOVA_NAMESPACE_END
