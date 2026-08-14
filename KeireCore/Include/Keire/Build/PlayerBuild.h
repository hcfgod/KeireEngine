#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/Asset.h"
#include "Keire/Project/Project.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    inline constexpr std::uint32_t PlayerSettingsSchemaVersion = 1;
    inline constexpr std::uint32_t PlayerBuildProfilesSchemaVersion = 1;
    inline constexpr std::uint32_t PlayerBuildScenesSchemaVersion = 1;

    enum class PlayerPlatform : std::uint8_t
    {
        Windows,
        Linux,
        MacOS
    };

    enum class PlayerArchitecture : std::uint8_t
    {
        X86_64,
        Arm64
    };

    enum class PlayerBuildConfiguration : std::uint8_t
    {
        Development,
        Release,
        Dist
    };

    enum class PlayerSigningPolicy : std::uint8_t
    {
        Disabled,
        SignIfConfigured,
        Required
    };

    struct PlayerSettings
    {
        std::uint32_t SchemaVersion = PlayerSettingsSchemaVersion;
        std::string ProductName;
        std::string Version = "0.1.0";
        std::string ApplicationIdentifier;
        std::string WindowTitle;
        AssetId WindowsIcon;
        AssetId LinuxIcon;
        AssetId MacOSIcon;

        [[nodiscard]] bool operator==(const PlayerSettings&) const = default;
    };

    struct PlayerSigningSettings
    {
        PlayerSigningPolicy Policy = PlayerSigningPolicy::Disabled;
        std::filesystem::path Command;
        std::vector<std::string> Arguments;
        std::vector<std::string> RequiredEnvironment;
        std::uint32_t TimeoutSeconds = 600;

        [[nodiscard]] bool operator==(const PlayerSigningSettings&) const = default;
    };

    struct PlayerBuildProfile
    {
        AssetId Id;
        std::string Name;
        PlayerPlatform Platform = PlayerPlatform::Windows;
        PlayerArchitecture Architecture = PlayerArchitecture::X86_64;
        PlayerBuildConfiguration Configuration = PlayerBuildConfiguration::Development;
        bool IncludeSymbols = true;
        std::string OutputSlug;
        PlayerSigningSettings Signing;

        [[nodiscard]] bool operator==(const PlayerBuildProfile&) const = default;
    };

    struct PlayerBuildProfiles
    {
        std::uint32_t SchemaVersion = PlayerBuildProfilesSchemaVersion;
        AssetId ActiveProfile;
        std::vector<PlayerBuildProfile> Profiles;

        [[nodiscard]] bool operator==(const PlayerBuildProfiles&) const = default;
    };

    struct PlayerBuildScene
    {
        AssetId Scene;
        bool Enabled = true;

        [[nodiscard]] bool operator==(const PlayerBuildScene&) const = default;
    };

    struct PlayerBuildScenes
    {
        std::uint32_t SchemaVersion = PlayerBuildScenesSchemaVersion;
        std::vector<PlayerBuildScene> Scenes;

        [[nodiscard]] bool operator==(const PlayerBuildScenes&) const = default;
    };

    [[nodiscard]] KEIRE_API PlayerPlatform HostPlayerPlatform() noexcept;
    [[nodiscard]] KEIRE_API PlayerArchitecture HostPlayerArchitecture() noexcept;
    [[nodiscard]] KEIRE_API std::string_view ToString(PlayerPlatform platform) noexcept;
    [[nodiscard]] KEIRE_API std::string_view ToString(PlayerArchitecture architecture) noexcept;
    [[nodiscard]] KEIRE_API std::string_view ToString(PlayerBuildConfiguration configuration) noexcept;
    [[nodiscard]] KEIRE_API std::string_view ToString(PlayerSigningPolicy policy) noexcept;
    [[nodiscard]] KEIRE_API PlayerSettings DefaultPlayerSettings(const ProjectDescriptor& project);
    [[nodiscard]] KEIRE_API PlayerBuildProfiles DefaultPlayerBuildProfiles();
    [[nodiscard]] KEIRE_API PlayerBuildScenes DefaultPlayerBuildScenes(const ProjectDescriptor& project);
    KEIRE_API void ValidatePlayerSettings(const PlayerSettings& settings);
    KEIRE_API void ValidatePlayerBuildProfiles(const PlayerBuildProfiles& profiles);
    KEIRE_API void ValidatePlayerBuildScenes(const PlayerBuildScenes& scenes);
    [[nodiscard]] KEIRE_API PlayerSettings LoadPlayerSettings(const std::filesystem::path& projectRoot,
                                                              const ProjectDescriptor& project);
    KEIRE_API void SavePlayerSettings(const std::filesystem::path& projectRoot, const PlayerSettings& settings);
    [[nodiscard]] KEIRE_API PlayerBuildProfiles LoadPlayerBuildProfiles(const std::filesystem::path& projectRoot);
    KEIRE_API void SavePlayerBuildProfiles(const std::filesystem::path& projectRoot,
                                           const PlayerBuildProfiles& profiles);
    [[nodiscard]] KEIRE_API PlayerBuildScenes LoadPlayerBuildScenes(const std::filesystem::path& projectRoot,
                                                                    const ProjectDescriptor& project);
    KEIRE_API void SavePlayerBuildScenes(const std::filesystem::path& projectRoot, const PlayerBuildScenes& scenes);
    [[nodiscard]] KEIRE_API std::vector<AssetId> EnabledPlayerBuildScenes(const PlayerBuildScenes& scenes);
    [[nodiscard]] KEIRE_API AssetId PlayerBuildStartupScene(const PlayerBuildScenes& scenes);
    [[nodiscard]] KEIRE_API const PlayerBuildProfile& FindPlayerBuildProfile(const PlayerBuildProfiles& profiles,
                                                                             AssetId id);
    [[nodiscard]] KEIRE_API const PlayerBuildProfile& FindPlayerBuildProfile(const PlayerBuildProfiles& profiles,
                                                                             std::string_view name);
} // namespace Keire
