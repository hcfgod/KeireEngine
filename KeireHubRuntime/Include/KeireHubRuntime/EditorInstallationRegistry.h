#pragma once

#include "KeireHubRuntime/HubError.h"
#include "KeireHubRuntime/PackageReceipt.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace KeireHub
{
    class HubController;

    enum class InstallationOwnership
    {
        Managed,
        External
    };

    enum class InstallationHealth
    {
        Unknown,
        Healthy,
        VerificationRequired,
        Damaged,
        Missing
    };

    struct ManagedInstallMarker final
    {
        std::string InstallationId;
        std::string ManifestFingerprint;
        std::string Nonce;
        std::string ReceiptSha256;
    };

    struct ManagedInstallRemovalProof final
    {
        std::string InstallationId;
        std::filesystem::path Root;
        std::string ManifestFingerprint;
        std::string PackageTreeIdentity;
        std::string PackageReceiptSha256;
        std::string MarkerNonce;
    };

    struct EditorInstallation final
    {
        std::string Id;
        std::string Version;
        std::string Channel;
        std::string Platform;
        std::string Architecture;
        std::filesystem::path Root;
        InstallationOwnership Ownership = InstallationOwnership::External;
        std::string ManifestFingerprint;
        std::string PackageTreeIdentity;
        std::string PackageReceiptSha256;
        std::string MarkerNonce;
        std::vector<InstalledPackageRecord> InstalledPackages;
        std::vector<std::filesystem::path> Entrypoints;
        std::filesystem::path EditorEntrypoint;
        std::filesystem::path AssetToolEntrypoint;
        std::string BundledDotnetSdk;
        std::uint32_t MinimumProjectSchema = 1;
        std::uint32_t MaximumProjectSchema = 1;
        std::uint64_t InstalledSizeBytes = 0;
        std::uint64_t LastVerifiedUnixSeconds = 0;
        InstallationHealth Health = InstallationHealth::Unknown;
    };

    struct EditorInstallationHealthUpdate final
    {
        std::string InstallationId;
        InstallationHealth Health = InstallationHealth::Unknown;
    };

    [[nodiscard]] std::filesystem::path ResolveEditorEntrypoint(const EditorInstallation& installation);
    [[nodiscard]] std::filesystem::path ResolveAssetToolEntrypoint(const EditorInstallation& installation);

    class EditorInstallationRegistry final
    {
      public:
        static constexpr std::uint32_t CurrentSchemaVersion = 1;
        static constexpr const char* MarkerFileName = ".keirehub-install.json";

        explicit EditorInstallationRegistry(std::filesystem::path registryPath);

        [[nodiscard]] HubStatus Load();
        [[nodiscard]] HubStatus Upsert(EditorInstallation installation);
        [[nodiscard]] HubStatus UpsertMany(std::span<const EditorInstallation> installations);
        [[nodiscard]] HubStatus UpdateHealth(std::span<const EditorInstallationHealthUpdate> updates);
        [[nodiscard]] HubStatus RemoveExternal(const std::string& installationId);
        [[nodiscard]] HubStatus RemoveManagedRegistration(const std::string& installationId,
                                                          const std::filesystem::path& expectedRoot);
        // Removes only a stale managed registration after proving that its exact registered root is already absent.
        // No filesystem content is deleted by this recovery operation.
        [[nodiscard]] HubStatus RemoveMissingManagedRegistration(const std::string& installationId,
                                                                 const std::filesystem::path& expectedRoot);
        // Used only after the worker has atomically hidden and purged an authorized managed root. The registry entry
        // is removed only when every persisted identity field still matches and no filesystem object remains there.
        [[nodiscard]] HubStatus RemoveDeletedManagedRegistration(const ManagedInstallRemovalProof& proof);
        [[nodiscard]] HubStatus CanMutateManagedInstall(const std::string& installationId,
                                                        const std::filesystem::path& expectedRoot) const;

        [[nodiscard]] std::shared_ptr<const std::vector<EditorInstallation>> Snapshot() const noexcept;
        [[nodiscard]] const std::filesystem::path& Path() const noexcept;

        [[nodiscard]] static HubStatus WriteManagedMarker(const std::filesystem::path& root,
                                                          const ManagedInstallMarker& marker);
        [[nodiscard]] static HubResult<ManagedInstallMarker> ReadManagedMarker(const std::filesystem::path& root);

      private:
        friend class HubController;

        [[nodiscard]] HubResult<std::vector<EditorInstallation>>
        PrepareUpsertMany(std::span<const EditorInstallation> installations) const;
        [[nodiscard]] HubStatus Commit(std::vector<EditorInstallation> installations);

        std::filesystem::path m_Path;
        std::shared_ptr<const std::vector<EditorInstallation>> m_Snapshot;
    };
} // namespace KeireHub
