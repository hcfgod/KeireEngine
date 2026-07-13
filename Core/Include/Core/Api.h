#ifndef CROSS_PLATFORM_CORE_CLIENT_TEMPLATE_CORE_API_H
#define CROSS_PLATFORM_CORE_CLIENT_TEMPLATE_CORE_API_H

#if defined(CORE_STATIC)
#define CORE_API
#elif defined(_WIN32)
#if defined(CORE_BUILDING_LIBRARY)
#define CORE_API __declspec(dllexport)
#else
#define CORE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define CORE_API __attribute__((visibility("default")))
#else
#define CORE_API
#endif

#endif
