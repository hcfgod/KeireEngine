#pragma once

#include "KeireInternal/Build/PlayerSupport.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Keire::Detail
{
    struct PlayerSupportPackageResult
    {
        PlayerSupportManifest Manifest;
        std::uint64_t ArchiveSize = 0;
        std::string ArchiveSha256;
    };

    struct PlayerSupportInstallCallbacks
    {
        std::function<bool()> Cancelled;
        std::function<void(float, std::string_view)> Progress;
    };

    [[nodiscard]] PlayerSupportPackageResult CreatePlayerSupportPackage(PlayerSupportManifest manifest,
                                                                        const std::filesystem::path& payloadRoot,
                                                                        const std::filesystem::path& output,
                                                                        int compressionLevel = 9);
    [[nodiscard]] PlayerSupportManifest ReadPlayerSupportPackageManifest(const std::filesystem::path& package);
    [[nodiscard]] PlayerSupportPackageResult
    InstallPlayerSupportPackage(const std::filesystem::path& package, std::string_view expectedModuleFingerprint = {},
                                const PlayerSupportInstallCallbacks& callbacks = {},
                                const std::filesystem::path& storageRoot = {});
    [[nodiscard]] bool ValidateInstalledPlayerSupport(const std::filesystem::path& installation,
                                                      std::string& diagnostic) noexcept;
    [[nodiscard]] std::vector<PlayerSupportPackageResult>
    InstalledPlayerSupport(const std::filesystem::path& storageRoot = {});
    void RemovePlayerSupport(std::string_view engineVersion, std::string_view packId,
                             const std::filesystem::path& storageRoot = {});
} // namespace Keire::Detail
