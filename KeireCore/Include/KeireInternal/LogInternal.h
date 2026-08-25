#pragma once

#include "Keire/Log.h"

#include <cstdint>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace Keire::Detail
{
    struct RetainedLogRecord final
    {
        std::uint64_t Sequence = 0;
        LogChannel Channel = LogChannel::Core;
        LogLevel Level = LogLevel::Info;
        std::string Message;
    };

    class LogInternalAccess final
    {
      public:
        [[nodiscard]] static bool
        WriteAndFlushIfOpen(LogChannel channel, LogLevel level, std::string_view message,
                            std::source_location location = std::source_location::current()) noexcept;
        [[nodiscard]] static std::vector<RetainedLogRecord> ReadRecordsSince(std::uint64_t sequence);
    };
} // namespace Keire::Detail
