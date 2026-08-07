#pragma once

#include "KeireHubRuntime/DistributionCatalog.h"
#include "KeireHubRuntime/EditorInstallationRegistry.h"
#include "KeireHubRuntime/PackageResolver.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace KeireHub
{
    struct EditorInstallCatalogSpecification final
    {
        std::string HostPlatform;
        std::string HostArchitecture;
        bool EnablePreReleaseChannel = false;
        bool EnableNightlyChannel = false;
    };

    struct AvailableEditorComponent final
    {
        std::string PackageId;
        std::string Version;
        PackageKind Kind = PackageKind::BuildSupport;
        std::string DisplayName;
        std::uint64_t DownloadSizeBytes = 0;
        std::uint64_t InstalledSizeBytes = 0;
        bool RequiredByEditor = false;
        std::vector<std::string> RequiredByPackageIds;
    };

    struct AvailableEditorVersion final
    {
        std::string PackageId;
        std::string Version;
        std::string DisplayName;
        std::string Channel;
        std::string Platform;
        std::string Architecture;
        std::uint64_t DownloadSizeBytes = 0;
        std::uint64_t InstalledSizeBytes = 0;
        std::vector<std::string> InstalledInstallationIds;
        std::vector<AvailableEditorComponent> Components;
        std::optional<HubError> AvailabilityError;
    };

    struct EditorInstallCatalogSnapshot final
    {
        std::vector<std::string> PopulatedChannels;
        std::vector<AvailableEditorVersion> AvailableEditors;
        std::shared_ptr<const std::vector<EditorInstallation>> InstalledEditors;
    };

    struct EditorComponentSelection final
    {
        std::string PackageId;
        std::string Version;
    };

    struct EditorInstallPreviewRequest final
    {
        std::string InstallationId;
        std::filesystem::path Destination;
        std::string EditorPackageId;
        std::string EditorVersion;
        std::vector<EditorComponentSelection> Components;
        std::optional<std::uint64_t> AvailableDiskBytes;
    };

    struct EditorInstallPackageStep final
    {
        PackageManifest Manifest;
        std::string CatalogKeyId;
        std::uint64_t CatalogSequence = 0;
        bool ExplicitlySelected = false;
        std::vector<std::string> RequiredByPackageIds;
    };

    struct EditorInstallPlan final
    {
        std::string InstallationId;
        std::filesystem::path Destination;
        std::string EditorPackageId;
        std::string EditorVersion;
        std::string Channel;
        std::uint64_t DownloadSizeBytes = 0;
        std::uint64_t RequiredDiskBytes = 0;
        std::vector<EditorComponentSelection> SelectedComponents;
        std::vector<EditorInstallPackageStep> Steps;
    };

    struct EditorRepairPreviewRequest final
    {
        std::string InstallationId;
        std::filesystem::path Destination;
        std::string ManifestFingerprint;
        std::string PackageTreeIdentity;
        std::string PackageReceiptSha256;
        std::string MarkerNonce;
        std::optional<std::uint64_t> AvailableDiskBytes;
    };

    struct EditorRepairPlan final
    {
        EditorInstallPlan Install;
        std::string ManifestFingerprint;
        std::string PackageTreeIdentity;
        std::string PackageReceiptSha256;
        std::string MarkerNonce;
        std::filesystem::path EditorEntrypoint;
    };

    class EditorInstallCatalog final
    {
      public:
        EditorInstallCatalog(EditorInstallationRegistry& registry, EditorInstallCatalogSpecification specification);

        [[nodiscard]] HubStatus Refresh(std::shared_ptr<const DistributionCatalogSnapshot> distribution);
        [[nodiscard]] HubResult<EditorInstallPlan> PreviewInstall(const EditorInstallPreviewRequest& request) const;
        [[nodiscard]] HubResult<EditorRepairPlan> PreviewRepair(const EditorRepairPreviewRequest& request) const;
        [[nodiscard]] std::shared_ptr<const EditorInstallCatalogSnapshot> Snapshot() const noexcept;

      private:
        struct IndexedPackage final
        {
            PackageManifest Manifest;
            std::string CatalogKeyId;
            std::uint64_t CatalogSequence = 0;
        };

        [[nodiscard]] HubResult<EditorInstallPlan> Preview(const EditorInstallPreviewRequest& request,
                                                           bool registeredRepair) const;

        EditorInstallationRegistry& m_Registry;
        EditorInstallCatalogSpecification m_Specification;
        std::vector<IndexedPackage> m_Packages;
        std::shared_ptr<const EditorInstallCatalogSnapshot> m_Snapshot;
    };
} // namespace KeireHub
