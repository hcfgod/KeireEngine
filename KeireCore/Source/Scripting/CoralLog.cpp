#include "KeireInternal/Scripting/CoralLog.h"

#include "Keire/Log.h"

#include <cstdio>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Keire::Detail
{
    void PreserveManagedHostLibrary()
    {
#if defined(_WIN32)
        // CoreCLR remains loaded after Coral closes its host context. Unloading hostfxr resets its
        // initialization state while hostpolicy still owns the runtime, breaking the next host.
        // Pinning is idempotent and lets Windows release the library with the process.
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"hostfxr.dll", &module))
            throw std::runtime_error("Could not preserve the managed host library for safe runtime reopening.");
#endif
    }

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
