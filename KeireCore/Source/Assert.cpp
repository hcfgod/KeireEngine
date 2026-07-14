#include "Keire/Assert.h"

#include "Keire/Log.h"

#include <cstdio>
#include <cstdlib>

namespace Keire::Detail
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
        KEIRE_CORE_CRITICAL("Assertion failed: {}{}{}{} at {}:{} in {}", expression, message.empty() ? "" : " (",
                            message, message.empty() ? "" : ")", location.file_name(), location.line(),
                            location.function_name());
        Log::Flush();
    }
    catch (...)
    {
    }
#if defined(_WIN32) && defined(_MSC_VER)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    std::abort();
}
} // namespace Keire::Detail
