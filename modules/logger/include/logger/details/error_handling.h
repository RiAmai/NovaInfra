#pragma once

#include <logger/types.h>

#include <string>

#define NOVA_LOG_DETAILS_CATCH(location)                                                                     \
      NOVA_CATCH_STD_BODY({                                                                                  \
          if (!location.file.empty()) {                                                                      \
              std::string str = std::string(ex.what()) + "[" + std::string(location.file) + "(" +            \
                                std::to_string(location.line) + ")]";                                        \
              errHandler(str);                                                                               \
          } else {                                                                                           \
              errHandler(ex.what());                                                                         \
          }                                                                                                  \
      })                                                                                                     \
      NOVA_CATCH_ALL_BODY({                                                                                  \
          errHandler("Rethrowing unknown exception in logger");                                              \
          NOVA_RETHROW_CURRENT();                                                                            \
      })
