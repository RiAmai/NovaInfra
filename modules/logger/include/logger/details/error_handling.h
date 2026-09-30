#pragma once

#include <logger/types.h>

#include <string>

#define NOVAINFRA_LOG_DETAILS_CATCH(location)                                                                \
    NOVAINFRA_CATCH_STD_BODY({                                                                               \
        if (!location.file.empty()) {                                                                        \
            std::string str = std::string(ex.what()) + "[" + std::string(location.file) + "(" +              \
                              std::to_string(location.line) + ")]";                                          \
            errHandler(str);                                                                                 \
        } else {                                                                                             \
            errHandler(ex.what());                                                                           \
        }                                                                                                    \
    })                                                                                                       \
    NOVAINFRA_CATCH_ALL_BODY({                                                                               \
        errHandler("Rethrowing unknown exception in logger");                                                \
        NOVAINFRA_RETHROW_CURRENT();                                                                         \
    })
