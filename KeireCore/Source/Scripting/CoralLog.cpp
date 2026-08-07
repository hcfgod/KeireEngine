#include "KeireInternal/Scripting/CoralLog.h"

#include "Keire/Log.h"

#include <cstdio>
#include <utility>

namespace Keire::Detail
{
    Coral::HostSettings CreateCoralHostSettings(std::string coralDirectory)
    {
        Coral::HostSettings settings;
        settings.CoralDirectory = std::move(coralDirectory);
        settings.MessageCallback = [](const std::string_view message, const Coral::MessageLevel level) noexcept
        {
            auto logLevel = LogLevel::Info;
            switch (level)
            {
            case Coral::MessageLevel::Trace:
                logLevel = LogLevel::Trace;
                break;
            case Coral::MessageLevel::Warning:
                logLevel = LogLevel::Warn;
                break;
            case Coral::MessageLevel::Error:
                logLevel = LogLevel::Error;
                break;
            case Coral::MessageLevel::Info:
            case Coral::MessageLevel::All:
                break;
            }
            try
            {
                Log::GetCoreLogger().Write(logLevel, LogMessage("[Coral] {}", message));
            }
            catch (...)
            {
                std::fprintf(stderr, "[Coral] %.*s\n", static_cast<int>(message.size()), message.data());
            }
        };
        return settings;
    }
} // namespace Keire::Detail
