#pragma once

#include "Keire/Build/PlayerBuild.h"
#include "KeireInternal/Build/PlayerSupport.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace Keire::Detail
{
    inline constexpr std::uint32_t PlayerBuildDescriptorSchemaVersion = 1;

    struct PlayerPackageLayout
    {
        std::filesystem::path Root;
        std::filesystem::path Executable;
        std::filesystem::path Content;
        std::filesystem::path ManagedRuntime;
        std::filesystem::path Descriptor;
    };

    struct PackagedPlayerConfiguration
    {
        PlayerSettings Settings;
        PlayerPlatform Platform = PlayerPlatform::Windows;
        PlayerArchitecture Architecture = PlayerArchitecture::X86_64;
        PlayerBuildConfiguration Configuration = PlayerBuildConfiguration::Development;
        std::filesystem::path Content;
        std::filesystem::path ManagedRuntime;
    };

    struct PlayerBuildStatusDocument
    {
        std::string State;
        std::string Phase;
        float Progress = 0.0F;
        std::string Message;
        std::string ErrorCode;
        std::filesystem::path Output;
        std::filesystem::path Executable;
    };

    [[nodiscard]] PlayerPackageLayout AssemblePlayerPackage(const ResolvedPlayerSupport& support,
                                                            const PlayerSettings& settings,
                                                            const PlayerBuildProfile& profile,
                                                            const std::filesystem::path& stagingRoot,
                                                            std::span<const std::byte> iconSource = {});
    void RunPlayerSigningHook(const std::filesystem::path& projectRoot, const PlayerSettings& settings,
                              const PlayerBuildProfile& profile, const PlayerPackageLayout& layout);
    void PublishPlayerPackage(const std::filesystem::path& stagingRoot, const std::filesystem::path& destination);
    [[nodiscard]] std::optional<PackagedPlayerConfiguration>
    LoadPackagedPlayerConfiguration(const std::filesystem::path& executable);
    void WritePlayerBuildStatusDocument(const std::filesystem::path& path, const PlayerBuildStatusDocument& document);
    [[nodiscard]] PlayerBuildStatusDocument ReadPlayerBuildStatusDocument(const std::filesystem::path& path);
} // namespace Keire::Detail
