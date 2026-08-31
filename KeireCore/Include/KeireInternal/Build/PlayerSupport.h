#pragma once

#include "Keire/Build/PlayerBuild.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Keire::Detail
{
    inline constexpr std::uint32_t PlayerSupportManifestSchemaVersion = 2;
    inline constexpr std::uint32_t PlayerBuildAbiVersion = 1;

    struct PlayerSupportVariant
    {
        PlayerBuildConfiguration Configuration = PlayerBuildConfiguration::Development;
        std::filesystem::path Root;
        std::filesystem::path Executable;
        std::filesystem::path Bundle;
        std::vector<std::filesystem::path> Symbols;
    };

    struct PlayerSupportFile
    {
        std::filesystem::path Path;
        std::uint64_t Size = 0;
        std::string Sha256;
        std::uint32_t Mode = 0644;

        [[nodiscard]] bool operator==(const PlayerSupportFile&) const = default;
    };

    struct PlayerBrandingSlot
    {
        std::filesystem::path Path;
        std::string Kind;
        std::uint64_t Offset = 0;
        std::uint64_t Size = 0;

        [[nodiscard]] bool operator==(const PlayerBrandingSlot&) const = default;
    };

    struct PlayerSupportManifest
    {
        std::uint32_t SchemaVersion = PlayerSupportManifestSchemaVersion;
        std::uint32_t PlayerAbi = PlayerBuildAbiVersion;
        std::string Id;
        std::string EngineVersion;
        PlayerPlatform Platform = PlayerPlatform::Windows;
        PlayerArchitecture Architecture = PlayerArchitecture::X86_64;
        std::string ModuleFingerprint;
        std::vector<std::string> SourceModules;
        std::vector<PlayerSupportVariant> Variants;
        std::vector<PlayerSupportFile> Files;
        std::vector<PlayerBrandingSlot> BrandingSlots;
    };

    struct ResolvedPlayerSupport
    {
        PlayerSupportManifest Manifest;
        PlayerSupportVariant Variant;
        std::filesystem::path InstallationRoot;
        bool DevelopmentFallback = false;
    };

    [[nodiscard]] std::filesystem::path PlayerSupportStorageRoot();
    [[nodiscard]] PlayerSupportManifest LoadPlayerSupportManifest(const std::filesystem::path& path);
    [[nodiscard]] PlayerSupportManifest DecodePlayerSupportManifest(std::string_view text);
    [[nodiscard]] std::string EncodePlayerSupportManifest(const PlayerSupportManifest& manifest);
    void ValidatePlayerSupportManifest(const PlayerSupportManifest& manifest);
    [[nodiscard]] ResolvedPlayerSupport ResolvePlayerSupport(const std::filesystem::path& executable,
                                                             PlayerPlatform platform, PlayerArchitecture architecture,
                                                             PlayerBuildConfiguration configuration,
                                                             const std::string& moduleFingerprint);
} // namespace Keire::Detail
