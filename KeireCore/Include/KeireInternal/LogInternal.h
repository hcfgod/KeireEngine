#pragma once

#include "Keire/Log.h"

#include <cstdint>
#include <string>
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
        [[nodiscard]] static std::vector<RetainedLogRecord> ReadRecordsSince(std::uint64_t sequence);
    };
} // namespace Keire::Detail
