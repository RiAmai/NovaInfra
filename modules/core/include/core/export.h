#pragma once

#if defined(_WIN32) || defined(_WIN64)
#  define NOVA_EXPORT __declspec(dllexport)
#  define NOVA_IMPORT __declspec(dllimport)
#  define NOVA_HIDDEN
#else
#  define NOVA_EXPORT __attribute__((visibility("default")))
#  define NOVA_IMPORT __attribute__((visibility("default")))
#  define NOVA_HIDDEN __attribute__((visibility("hidden")))
#endif

#if defined(NOVA_STATIC_DEFINE)
#  define NOVA_LIB_PORT
#elif defined(NOVA_LIB_EXPORT)
#  define NOVA_LIB_PORT NOVA_EXPORT
#else
#  define NOVA_LIB_PORT NOVA_IMPORT
#endif
