#pragma once

#include "KeireHubRuntime/EditorInstallationRegistry.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub
{
    enum class EditorInstallationIssueCode
    {
        RootMissing,
        ManifestMissing,
        ManifestInvalid,
        InventoryInvalid,
        ManifestFingerprintMismatch,
        ReceiptMissing,
        ReceiptInvalid,
        ReceiptMismatch,
        RegistrationMismatch,
        HostIncompatible,
        MarkerMismatch,
        MissingFile,
        UnsafeFile,
        FileSizeMismatch,
        FileDigestMismatch,
        FileModeMismatch,
        MissingEntrypoint
    };

    struct EditorInstallationIssue final
    {
        EditorInstallationIssueCode Code = EditorInstallationIssueCode::ManifestInvalid;
        std::filesystem::path RelativePath;
        std::string Message;
    };

    struct EditorInstallationActivity final
    {
        bool Running = false;
        bool HasActiveTask = false;
    };

    enum class EditorEntrypointActivity
    {
        NotRunning,
        Running,
        Indeterminate
    };

    struct EditorProcessObservation final
    {
        EditorEntrypointActivity Activity = EditorEntrypointActivity::Indeterminate;
        std::uint64_t Identity = 0;
    };

    [[nodiscard]] EditorEntrypointActivity
    ProbeEditorEntrypointProcessActivity(const std::filesystem::path& executable) noexcept;
    [[nodiscard]] EditorProcessObservation ProbeEditorProcess(std::uint64_t processId,
                                                              const std::filesystem::path& executable) noexcept;
    [[nodiscard]] EditorEntrypointActivity ProbeEditorProcessActivity(std::uint64_t processId,
                                                                      const std::filesystem::path& executable) noexcept;

    struct EditorInstallationHealthSnapshot final
    {
        EditorInstallation Installation;
        InstallationHealth Health = InstallationHealth::Unknown;
        EditorInstallationActivity Activity;
        std::vector<EditorInstallationIssue> Issues;
        std::uint64_t VerifiedFileCount = 0;
        std::uint64_t VerifiedBytes = 0;
    };

    enum class EditorManagedOperation
    {
        Repair,
        Remove
    };

    struct EditorManagedOperationPlan final
    {
        EditorManagedOperation Operation = EditorManagedOperation::Repair;
        std::string InstallationId;
        std::filesystem::path Root;
        std::string ManifestFingerprint;
        std::string PackageTreeIdentity;
        std::string PackageReceiptSha256;
        std::string MarkerNonce;
        std::filesystem::path EditorEntrypoint;
        InstallationHealth CurrentHealth = InstallationHealth::Unknown;
        bool RequiresCompletePackage = false;
        std::vector<std::filesystem::path> FilesToRestore;
    };

    struct EditorInstallationManagerSpecification final
    {
        using ActivityProbe = std::function<EditorInstallationActivity(const EditorInstallation&)>;
        using EntrypointActivityProbe = std::function<EditorEntrypointActivity(const std::filesystem::path&)>;
        using VerificationProgress = std::function<void(std::uint64_t verifiedFiles, std::uint64_t totalFiles,
                                                        std::uint64_t verifiedBytes, std::uint64_t totalBytes)>;

        std::string HostPlatform;
        std::string HostArchitecture;
        ActivityProbe ProbeActivity;
        EntrypointActivityProbe ProbeEntrypointActivity;
        VerificationProgress ReportVerificationProgress;
    };

    struct ManagedEditorPackageRequest final
    {
        std::filesystem::path PackageRoot;
        std::filesystem::path InstallationRoot;
        std::string InstallationId;
        std::string MarkerNonce;
        std::string HostPlatform;
        std::string HostArchitecture;
        std::uint64_t VerifiedUnixSeconds = 0;
        bool RequirePackageReceipt = false;
        bool PreserveExistingMarker = false;
    };

    [[nodiscard]] HubResult<std::string> ComputeEditorPackageManifestFingerprint(std::string_view document);
    [[nodiscard]] HubResult<std::filesystem::path>
    ResolveExternalEditorPackageRoot(const std::filesystem::path& selected);
    [[nodiscard]] HubResult<EditorInstallation> PrepareManagedEditorPackage(const ManagedEditorPackageRequest& request);
    // These value-only operations are safe to run on a background worker when the caller captures the installation
    // and every activity input by value. The caller must reject stale results against the live registry and activity
    // state before publishing or queuing a mutating operation.
    [[nodiscard]] HubResult<EditorInstallationHealthSnapshot>
    InspectEditorInstallationSnapshot(const EditorInstallation& installation,
                                      const EditorInstallationManagerSpecification& specification);
    [[nodiscard]] HubResult<EditorInstallationHealthSnapshot>
    VerifyEditorInstallationSnapshot(const EditorInstallation& installation,
                                     const EditorInstallationManagerSpecification& specification);
    [[nodiscard]] HubResult<EditorManagedOperationPlan>
    PrepareManagedEditorOperationSnapshot(const EditorInstallation& installation,
                                          const std::filesystem::path& expectedRoot, EditorManagedOperation operation,
                                          const EditorInstallationManagerSpecification& specification);
    [[nodiscard]] HubStatus
    RevalidateManagedEditorOperationSnapshot(const EditorInstallation& installation,
                                             const EditorManagedOperationPlan& plan,
                                             const EditorInstallationManagerSpecification& specification);
    [[nodiscard]] HubResult<EditorInstallation>
    RegisterManagedEditorPackage(EditorInstallationRegistry& registry, const std::filesystem::path& root,
                                 std::string installationId, std::string hostPlatform, std::string hostArchitecture,
                                 std::uint64_t verifiedUnixSeconds);

    class EditorInstallationManager final
    {
      public:
        EditorInstallationManager(EditorInstallationRegistry& registry,
                                  EditorInstallationManagerSpecification specification);

        [[nodiscard]] HubStatus Refresh();
        [[nodiscard]] HubResult<EditorInstallationHealthSnapshot> Inspect(const std::string& installationId) const;
        [[nodiscard]] HubResult<EditorInstallationHealthSnapshot> Verify(const std::string& installationId) const;
        [[nodiscard]] HubStatus RemoveExternalRegistration(const std::string& installationId,
                                                           const std::filesystem::path& expectedRoot);
        [[nodiscard]] HubResult<EditorManagedOperationPlan>
        PrepareManagedRepair(const std::string& installationId, const std::filesystem::path& expectedRoot) const;
        [[nodiscard]] HubResult<EditorManagedOperationPlan>
        PrepareManagedRemoval(const std::string& installationId, const std::filesystem::path& expectedRoot) const;
        [[nodiscard]] HubStatus Revalidate(const EditorManagedOperationPlan& plan) const;

        [[nodiscard]] std::shared_ptr<const std::vector<EditorInstallationHealthSnapshot>> Snapshot() const noexcept;

      private:
        [[nodiscard]] std::optional<EditorInstallation> Find(const std::string& installationId) const;
        [[nodiscard]] EditorInstallationActivity ProbeActivity(const EditorInstallation& installation) const;
        [[nodiscard]] HubStatus GuardInactive(const EditorInstallation& installation) const;
        [[nodiscard]] HubStatus GuardInactive(const EditorInstallation& installation,
                                              EditorInstallationActivity activity) const;
        [[nodiscard]] HubResult<EditorManagedOperationPlan>
        PrepareManagedOperation(const std::string& installationId, const std::filesystem::path& expectedRoot,
                                EditorManagedOperation operation) const;

        EditorInstallationRegistry& m_Registry;
        EditorInstallationManagerSpecification m_Specification;
        std::shared_ptr<const std::vector<EditorInstallationHealthSnapshot>> m_Snapshot;
    };
} // namespace KeireHub
