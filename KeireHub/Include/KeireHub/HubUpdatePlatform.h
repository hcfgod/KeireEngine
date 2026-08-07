#pragma once

#include "KeireHubRuntime/HubUpdateManager.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace KeireHub
{
    [[nodiscard]] bool NativeHubUpdateHandoffAvailable() noexcept;
    [[nodiscard]] bool NativeHubUpdateRequiresPlatformSignature() noexcept;
    [[nodiscard]] std::string NativeHubUpdateHandoffUnavailableMessage();
    [[nodiscard]] std::uint64_t HubCurrentProcessId() noexcept;
    [[nodiscard]] HubStatus VerifyNativeHubInstallerSignature(const std::filesystem::path& installer);
    [[nodiscard]] HubStatus LaunchNativeHubInstaller(const HubUpdateLaunch& launch);
} // namespace KeireHub
