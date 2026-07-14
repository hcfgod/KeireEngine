#pragma once

#if defined(KEIRE_STATIC)
#define KEIRE_API
#elif defined(_WIN32)
#if defined(KEIRE_BUILDING_LIBRARY)
#define KEIRE_API __declspec(dllexport)
#else
#define KEIRE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define KEIRE_API __attribute__((visibility("default")))
#else
#define KEIRE_API
#endif
