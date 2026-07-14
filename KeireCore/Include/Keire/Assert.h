#pragma once

#include "Keire/Api.h"

#include <source_location>
#include <string_view>

namespace Keire::Detail
{
    [[noreturn]] KEIRE_API void AssertionFailure(std::string_view expression, std::source_location location,
                                             std::string_view message = {}) noexcept;
}

#if defined(KEIRE_ASSERTIONS_ENABLED)
#define KEIRE_ASSERT(condition, ...)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            ::Keire::Detail::AssertionFailure(#condition, std::source_location::current() __VA_OPT__(, ) __VA_ARGS__); \
        }                                                                                                              \
    } while (false)
#else
#define KEIRE_ASSERT(condition, ...) (void)0

#endif
