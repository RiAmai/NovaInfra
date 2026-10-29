#pragma once

#include <logger/types.h>
#include <logger/detail/log_msg.h>

#include <core/export.h>
#include <core/namespace.h>

#include <memory>
#include <string>

NOVAINFRA_NAMESPACE_BEGIN
namespace log::formatters {

class NOVAINFRA_LIB_PORT IFormatter
{
public:
    virtual ~IFormatter() = default;
    virtual void format(const detail::LogMsg &msg, std::string &dst) = 0;
    virtual std::unique_ptr<IFormatter> clone() const = 0;
};

}  // namespace log::formatters
NOVAINFRA_NAMESPACE_END
