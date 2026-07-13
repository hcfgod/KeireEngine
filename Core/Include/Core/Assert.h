#ifndef CROSS_PLATFORM_CORE_CLIENT_TEMPLATE_CORE_ASSERT_H
#define CROSS_PLATFORM_CORE_CLIENT_TEMPLATE_CORE_ASSERT_H

#include "Core/Api.h"

#include <source_location>
#include <string_view>

namespace Core::Detail
{
    [[noreturn]] CORE_API void AssertionFailure(std::string_view expression, std::source_location location,
                                            std::string_view message = {}) noexcept;
}

#if defined(CORE_ASSERTIONS_ENABLED)
#define CORE_ASSERT(condition, ...)                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            ::Core::Detail::AssertionFailure(#condition, std::source_location::current() __VA_OPT__(, ) __VA_ARGS__);  \
        }                                                                                                              \
    } while (false)
#else
#define CORE_ASSERT(condition, ...) (void)0
#endif

#endif
