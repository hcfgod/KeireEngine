#pragma once

#include "KeireHubRuntime/DownloadManager.h"
#include "KeireHubRuntime/HubTaskStore.h"
#include "KeireHubRuntime/PackageResolver.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace KeireHub
{
    inline constexpr std::size_t MaximumHubWorkerInstallPackageSteps = 64;

    enum class HubWorkerEditorInstallMode
    {
        Install,
        Repair
    };

    struct HubWorkerEditorRepairAuthorization final
    {
        std::string ManifestFingerprint;
        std::string PackageTreeIdentity;
        std::string PackageReceiptSha256;
        std::filesystem::path EditorEntrypoint;
    };

    struct HubWorkerInstallPackageRequest final
    {
        PackageManifest Package;
        DownloadRequest Download;
    };

    struct HubWorkerEditorInstallRequest final
    {
        // PackageSteps retain the resolver's deterministic topological order. Package identifies the one editor
        // package whose manifest and marker define the published installation.
        PackageManifest Package;
        HubWorkerEditorInstallMode Mode = HubWorkerEditorInstallMode::Install;
        std::optional<HubWorkerEditorRepairAuthorization> RepairAuthorization;
        std::vector<HubWorkerInstallPackageRequest> PackageSteps;
        std::vector<std::string> RequestedPackageIds;
        std::filesystem::path AllowedInstallRoot;
        std::filesystem::path Destination;
        std::string InstallationId;
        std::string MarkerNonce;
        std::string HostPlatform;
        std::string HostArchitecture;
        std::uint64_t VerifiedUnixSeconds = 0;
    };

    struct HubWorkerEditorRemovalRequest final
    {
        std::filesystem::path AllowedInstallRoot;
        std::filesystem::path Root;
        std::string InstallationId;
        std::string ManifestFingerprint;
        std::string PackageTreeIdentity;
        std::string PackageReceiptSha256;
        std::string MarkerNonce;
    };

    struct HubWorkerRequest final
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        std::string TaskId;
        DownloadRequest Download;
        std::optional<HubWorkerEditorInstallRequest> EditorInstall;
        std::optional<HubWorkerEditorRemovalRequest> EditorRemoval;
    };

    struct HubWorkerStatus final
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        std::string TaskId;
        HubTaskState State = HubTaskState::Downloading;
        HubTaskProgress Progress;
        std::uint64_t WorkerProcessId = 0;
        std::uint64_t UpdatedUnixSeconds = 0;
    };

    struct HubWorkerResult final
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        std::string TaskId;
        DownloadOutcome Outcome = DownloadOutcome::Completed;
        std::filesystem::path CachePath;
        std::filesystem::path InstalledRoot;
        std::filesystem::path RemovedRoot;
        std::string InstallationId;
        std::optional<HubError> Failure;
    };

    [[nodiscard]] HubStatus ValidateHubWorkerRequest(const HubWorkerRequest& request);
    [[nodiscard]] HubResult<HubWorkerRequest> ReadHubWorkerRequest(const std::filesystem::path& path);
    [[nodiscard]] HubStatus WriteHubWorkerRequest(const std::filesystem::path& path, const HubWorkerRequest& request);
    [[nodiscard]] HubResult<HubWorkerStatus> ReadHubWorkerStatus(const std::filesystem::path& path);
    [[nodiscard]] HubStatus WriteHubWorkerStatus(const std::filesystem::path& path, const HubWorkerStatus& status);
    [[nodiscard]] HubResult<HubWorkerResult> ReadHubWorkerResult(const std::filesystem::path& path);
    [[nodiscard]] HubStatus WriteHubWorkerResult(const std::filesystem::path& path, const HubWorkerResult& result);
    [[nodiscard]] HubResult<DownloadControl> ReadHubWorkerControl(const std::filesystem::path& path);
    [[nodiscard]] HubStatus WriteHubWorkerControl(const std::filesystem::path& path, DownloadControl control);
} // namespace KeireHub
