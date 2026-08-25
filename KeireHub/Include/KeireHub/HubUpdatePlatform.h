#pragma once

#include "KeireHubRuntime/HubUpdateManager.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace KeireHub
{
    [[nodiscard]] bool NativeHubUpdateHandoffAvailable() noexcept;
    [[nodiscard]] HubUpdatePlatformSignaturePolicy NativeHubUpdatePlatformSignaturePolicy() noexcept;
    [[nodiscard]] std::string NativeHubUpdateHandoffUnavailableMessage();
    [[nodiscard]] std::uint64_t HubCurrentProcessId() noexcept;
    [[nodiscard]] HubResult<HubUpdatePlatformSignatureState>
    VerifyNativeHubInstallerSignature(const std::filesystem::path& installer);
    [[nodiscard]] HubStatus LaunchNativeHubInstaller(const HubUpdateLaunch& launch);
} // namespace KeireHub
