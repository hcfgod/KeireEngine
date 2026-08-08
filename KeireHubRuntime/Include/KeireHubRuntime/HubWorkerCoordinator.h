#pragma once

#include "KeireHubRuntime/DownloadManager.h"
#include "KeireHubRuntime/EditorInstallationRegistry.h"
#include "KeireHubRuntime/HubTaskStore.h"
#include "KeireHubRuntime/PackageResolver.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace KeireHub
{
    struct HubWorkerLaunch final
    {
        std::filesystem::path Executable;
        std::filesystem::path WorkingDirectory;
        std::vector<std::string> Arguments;
    };

    class HubWorkerProcessHost
    {
      public:
        virtual ~HubWorkerProcessHost() = default;

        [[nodiscard]] virtual HubStatus LaunchDetached(const HubWorkerLaunch& launch) = 0;
        [[nodiscard]] virtual bool IsProcessAlive(std::uint64_t processId) const noexcept = 0;
    };

    [[nodiscard]] std::unique_ptr<HubWorkerProcessHost> CreateNativeHubWorkerProcessHost();

    struct CatalogPackageDownloadRequest final
    {
        std::string TaskId;
        PackageManifest Package;
        std::string PackageUrl;
        std::filesystem::path CacheRoot;
        DownloadRetryPolicy Retry;
        bool AllowInsecureLoopbackDevelopment = false;
        std::optional<std::string> CustomProxyUrl;
        std::uint64_t BandwidthLimitBytesPerSecond = 0;
    };

    struct CatalogEditorInstallRequest final
    {
        CatalogPackageDownloadRequest Download;
        std::vector<CatalogPackageDownloadRequest> AdditionalDownloads;
        PackageManifest EditorPackage;
        std::vector<std::string> RequestedPackageIds;
        std::filesystem::path AllowedInstallRoot;
        std::filesystem::path Destination;
        std::string InstallationId;
        std::string MarkerNonce;
        std::string HostPlatform;
        std::string HostArchitecture;
        std::uint64_t VerifiedUnixSeconds = 0;
    };

    struct CatalogEditorRepairRequest final
    {
        CatalogEditorInstallRequest Install;
        std::string ManifestFingerprint;
        std::string PackageTreeIdentity;
        std::string PackageReceiptSha256;
        std::filesystem::path EditorEntrypoint;
    };

    struct CatalogEditorRemovalRequest final
    {
        std::string TaskId;
        std::filesystem::path AllowedInstallRoot;
        std::filesystem::path Root;
        std::string InstallationId;
        std::string ManifestFingerprint;
        std::string PackageTreeIdentity;
        std::string PackageReceiptSha256;
        std::string MarkerNonce;
    };

    struct HubWorkerCoordinatorSpecification final
    {
        std::filesystem::path TaskStorePath;
        std::filesystem::path OperationRoot;
        std::filesystem::path WorkerExecutable;
        std::uint32_t MaximumConcurrentDownloads = 2;
        std::size_t MaximumPendingCommands = 256;
        std::chrono::milliseconds PollInterval{100};
        std::chrono::milliseconds WorkerStartupTimeout{15'000};
    };

    struct HubWorkerCoordinatorClocks final
    {
        std::function<std::uint64_t()> UnixSeconds;
        std::function<std::chrono::steady_clock::time_point()> Monotonic;
    };

    enum class HubWorkerCoordinatorState
    {
        Starting,
        Ready,
        Failed,
        Stopped
    };

    struct HubVerifiedPackageDownload final
    {
        std::string TaskId;
        std::string PackageId;
        std::string Sha256;
        std::uint64_t SizeBytes = 0;
        std::filesystem::path CachePath;
    };

    struct HubCompletedEditorInstall final
    {
        std::string TaskId;
        std::string InstallationId;
        std::string PackageId;
        std::filesystem::path Root;
        bool RepairsExisting = false;
        std::string ManifestFingerprint;
        std::string PackageTreeIdentity;
        std::string PackageReceiptSha256;
        std::string MarkerNonce;
    };

    struct HubCompletedEditorRemoval final
    {
        std::string TaskId;
        ManagedInstallRemovalProof Proof;
    };

    struct HubWorkerCoordinatorSnapshot final
    {
        HubWorkerCoordinatorState State = HubWorkerCoordinatorState::Starting;
        std::uint64_t Revision = 0;
        std::shared_ptr<const std::vector<HubTask>> Tasks;
        // Completion proves the worker verified this content-addressed path. Consumers must still tolerate cache
        // eviction between observing the snapshot and opening the file.
        std::shared_ptr<const std::vector<HubVerifiedPackageDownload>> VerifiedDownloads;
        std::shared_ptr<const std::vector<HubCompletedEditorInstall>> CompletedEditorInstalls;
        std::shared_ptr<const std::vector<HubCompletedEditorRemoval>> CompletedEditorRemovals;
        std::optional<HubError> LastFailure;
    };

    class HubWorkerCoordinator final
    {
      public:
        [[nodiscard]] static HubResult<std::unique_ptr<HubWorkerCoordinator>>
        Create(HubWorkerCoordinatorSpecification specification, std::unique_ptr<HubWorkerProcessHost> processHost,
               HubWorkerCoordinatorClocks clocks = {});

        ~HubWorkerCoordinator();

        HubWorkerCoordinator(const HubWorkerCoordinator&) = delete;
        HubWorkerCoordinator& operator=(const HubWorkerCoordinator&) = delete;
        HubWorkerCoordinator(HubWorkerCoordinator&&) = delete;
        HubWorkerCoordinator& operator=(HubWorkerCoordinator&&) = delete;

        // Commands are validated and placed on a bounded queue; persistence and process work occurs on the
        // coordinator thread. Asynchronous failures are published through LastFailure.
        [[nodiscard]] HubStatus QueuePackageDownload(CatalogPackageDownloadRequest request);
        [[nodiscard]] HubStatus QueueHubUpdate(CatalogPackageDownloadRequest request);
        [[nodiscard]] HubStatus QueueEditorInstall(CatalogEditorInstallRequest request);
        [[nodiscard]] HubStatus QueueEditorRepair(CatalogEditorRepairRequest request);
        [[nodiscard]] HubStatus QueueEditorRemoval(const CatalogEditorRemovalRequest& request);
        [[nodiscard]] HubStatus Pause(const std::string& taskId);
        [[nodiscard]] HubStatus Resume(const std::string& taskId);
        [[nodiscard]] HubStatus Cancel(const std::string& taskId);
        [[nodiscard]] HubStatus Retry(const std::string& taskId);
        [[nodiscard]] HubStatus Dismiss(const std::string& taskId);
        [[nodiscard]] HubStatus ClearFinished();

        [[nodiscard]] std::shared_ptr<const HubWorkerCoordinatorSnapshot> Snapshot() const noexcept;
        // Detached workers are intentionally left running; their journals and PIDs are reconciled at the next start.
        void Stop() noexcept;

      private:
        class Impl;

        HubWorkerCoordinator(HubWorkerCoordinatorSpecification specification,
                             std::unique_ptr<HubWorkerProcessHost> processHost, HubWorkerCoordinatorClocks clocks);

        std::unique_ptr<Impl> m_Impl;
    };
} // namespace KeireHub
