#include "Core/Assert.h"

#include "Core/Log.h"

#include <cstdio>
#include <cstdlib>

namespace Core::Detail
{
    [[noreturn]] void AssertionFailure(const std::string_view expression, const std::source_location location,
                                       const std::string_view message) noexcept
    {
        std::fprintf(stderr, "Assertion failed: %.*s%s%.*s%s at %s:%u in %s\n", static_cast<int>(expression.size()),
                     expression.data(), message.empty() ? "" : " (", static_cast<int>(message.size()), message.data(),
                     message.empty() ? "" : ")", location.file_name(), location.line(), location.function_name());
        std::fflush(stderr);
        try
        {
            CORE_CRITICAL("Assertion failed: {}{}{}{} at {}:{} in {}", expression, message.empty() ? "" : " (", message,
                          message.empty() ? "" : ")", location.file_name(), location.line(), location.function_name());
            Log::Flush();
        }
        catch (...)
        {
        }
        std::abort();
    }
} // namespace Core::Detail
