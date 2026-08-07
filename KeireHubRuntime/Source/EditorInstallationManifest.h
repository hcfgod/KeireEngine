#pragma once

#include "KeireHubRuntime/EditorInstallationManager.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub::Detail
{
    struct EditorPackageFile final
    {
        std::filesystem::path Path;
        std::uint64_t SizeBytes = 0;
        std::string Sha256;
    };

    struct EditorPackageManifest final
    {
        std::string PackageId;
        std::string Version;
        std::string Channel;
        std::string Platform;
        std::string Architecture;
        std::string Fingerprint;
        std::uint32_t MinimumProjectSchema = 1;
        std::uint32_t MaximumProjectSchema = 1;
        std::uint64_t InstalledSizeBytes = 0;
        std::uint64_t ManifestSizeBytes = 0;
        std::vector<std::filesystem::path> Entrypoints;
        std::filesystem::path EditorEntrypoint;
        std::filesystem::path AssetToolEntrypoint;
        std::string BundledDotnetSdk;
        std::vector<EditorPackageFile> Files;
    };

    [[nodiscard]] HubResult<EditorPackageManifest> ReadEditorPackageManifest(const std::filesystem::path& root);
    [[nodiscard]] std::string CanonicalEditorPlatform(std::string_view value);
    [[nodiscard]] std::string CanonicalEditorArchitecture(std::string_view value);
    [[nodiscard]] std::string NormalizedEditorPathKey(const std::filesystem::path& path);
} // namespace KeireHub::Detail
