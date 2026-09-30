#pragma once

#if defined(_WIN32) || defined(_WIN64)
#  define NOVAINFRA_EXPORT __declspec(dllexport)
#  define NOVAINFRA_IMPORT __declspec(dllimport)
#  define NOVAINFRA_HIDDEN
#else
#  define NOVAINFRA_EXPORT __attribute__((visibility("default")))
#  define NOVAINFRA_IMPORT __attribute__((visibility("default")))
#  define NOVAINFRA_HIDDEN __attribute__((visibility("hidden")))
#endif

#if defined(NOVAINFRA_STATIC_DEFINE)
#  define NOVAINFRA_LIB_PORT
#elif defined(NOVAINFRA_LIB_EXPORT)
#  define NOVAINFRA_LIB_PORT NOVAINFRA_EXPORT
#else
#  define NOVAINFRA_LIB_PORT NOVAINFRA_IMPORT
#endif
