#pragma once

#include "Keire/Build/PlayerBuild.h"
#include "KeireInternal/Build/PlayerSupportPackage.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Keire::Detail
{
    struct PlayerSupportCatalogEntry
    {
        std::string Id;
        PlayerPlatform Platform = PlayerPlatform::Windows;
        PlayerArchitecture Architecture = PlayerArchitecture::X86_64;
        std::string File;
        std::string Url;
        std::uint64_t Size = 0;
        std::string Sha256;

        auto operator<=>(const PlayerSupportCatalogEntry&) const = default;
    };

    struct PlayerSupportCatalog
    {
        std::string EngineVersion;
        std::vector<PlayerSupportCatalogEntry> Packages;
    };

    [[nodiscard]] std::string DefaultPlayerSupportCatalogUrl(std::string_view repositorySlug,
                                                             std::string_view engineVersion);
    [[nodiscard]] PlayerSupportCatalog LoadPlayerSupportCatalog(const std::filesystem::path& path,
                                                                std::string_view sourceUrl,
                                                                std::string_view expectedEngineVersion);
    [[nodiscard]] PlayerSupportCatalog FetchPlayerSupportCatalog(std::string_view repositorySlug,
                                                                 std::string_view engineVersion,
                                                                 const std::filesystem::path& destination,
                                                                 const PlayerSupportInstallCallbacks& callbacks = {});
    void DownloadPlayerSupportPackage(const PlayerSupportCatalogEntry& entry, const std::filesystem::path& destination,
                                      const PlayerSupportInstallCallbacks& callbacks = {});

    void DownloadHttpsFileNative(std::string_view url, const std::filesystem::path& destination,
                                 std::uint64_t maximumBytes, const PlayerSupportInstallCallbacks& callbacks);
} // namespace Keire::Detail
