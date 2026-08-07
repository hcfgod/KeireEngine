#include "KeireHubRuntime/DownloadManager.h"
#include "KeireHubRuntime/EditorInstallationManager.h"
#include "KeireHubRuntime/EditorInstallationRegistry.h"
#include "KeireHubRuntime/HubWorkerProtocol.h"
#include "KeireHubRuntime/ManagedEditorRemoval.h"
#include "KeireHubRuntime/NativeHttpTransport.h"
#include "KeireHubRuntime/PackageArchive.h"
#include "KeireHubRuntime/PackageAssembly.h"
#include "KeireHubRuntime/PackagePublish.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace
{
    struct CommandLine final
    {
        std::filesystem::path Request;
        std::filesystem::path Status;
        std::filesystem::path Result;
        std::filesystem::path Control;
    };

    struct AcquiredPackage final
    {
        KeireHub::PackageManifest Manifest;
        std::filesystem::path CachePath;
    };

    [[nodiscard]] std::filesystem::path NativeIoPath(const std::filesystem::path& path)
    {
#if defined(_WIN32)
        auto value = std::filesystem::absolute(path).lexically_normal().native();
        if (value.starts_with(LR"(\\?\)"))
            return value;
        if (value.starts_with(LR"(\\)"))
            return LR"(\\?\UNC\)" + value.substr(2);
        return LR"(\\?\)" + value;
#else
        return path;
#endif
    }

    [[nodiscard]] bool HasUnsafeLinkAncestor(const std::filesystem::path& path)
    {
        auto current = path.lexically_normal();
        while (!current.empty())
        {
#if defined(_WIN32)
            const auto attributes = GetFileAttributesW(NativeIoPath(current).c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                return true;
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                const auto failure = GetLastError();
                if (failure != ERROR_FILE_NOT_FOUND && failure != ERROR_PATH_NOT_FOUND)
                    return true;
            }
#else
            std::error_code error;
            const auto status = std::filesystem::symlink_status(current, error);
            if (!error && std::filesystem::is_symlink(status))
                return true;
            if (error && error != std::errc::no_such_file_or_directory)
                return true;
#endif
            if (current == current.root_path())
                break;
            const auto parent = current.parent_path();
            if (parent == current)
                break;
            current = parent;
        }
        return false;
    }

    [[nodiscard]] bool IsSafeDirectoryBoundary(const std::filesystem::path& path)
    {
        if (path.empty() || !path.is_absolute() || HasUnsafeLinkAncestor(path))
            return false;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(NativeIoPath(path), error);
        return !error && status.type() == std::filesystem::file_type::directory;
    }

    class OwnedStagingDirectories final
    {
      public:
        explicit OwnedStagingDirectories(std::filesystem::path allowedParent)
            : m_AllowedParent(std::move(allowedParent).lexically_normal())
        {
        }

        OwnedStagingDirectories(const OwnedStagingDirectories&) = delete;
        OwnedStagingDirectories& operator=(const OwnedStagingDirectories&) = delete;

        ~OwnedStagingDirectories()
        {
            if (!IsSafeDirectoryBoundary(m_AllowedParent))
                return;
            for (const auto& path : m_Paths)
            {
                if (HasUnsafeLinkAncestor(path))
                    continue;
                std::error_code ignored;
                const auto status = std::filesystem::symlink_status(NativeIoPath(path), ignored);
                if (!ignored && status.type() == std::filesystem::file_type::directory)
                    std::filesystem::remove_all(NativeIoPath(path), ignored);
            }
        }

        [[nodiscard]] KeireHub::HubStatus ResetAndOwn(const std::filesystem::path& path)
        {
            const auto normalized = path.lexically_normal();
            if (normalized.parent_path() != m_AllowedParent || normalized.filename().empty() ||
                !IsSafeDirectoryBoundary(m_AllowedParent) || HasUnsafeLinkAncestor(normalized))
            {
                return KeireHub::HubStatus::Failure(
                    {.Code = KeireHub::HubErrorCode::UnsafeInstallRoot,
                     .Message = "A worker staging path escaped its installation boundary.",
                     .AffectedItem = normalized.filename().string(),
                     .TechnicalDetails = {},
                     .LogReference = {}});
            }
            std::error_code error;
            const auto status = std::filesystem::symlink_status(NativeIoPath(normalized), error);
            if (error && error != std::errc::no_such_file_or_directory)
            {
                return KeireHub::HubStatus::Failure({.Code = KeireHub::HubErrorCode::IoRead,
                                                     .Message = "A worker staging path could not be inspected.",
                                                     .Retryable = true,
                                                     .AffectedItem = normalized.filename().string(),
                                                     .TechnicalDetails = error.message(),
                                                     .LogReference = {}});
            }
            if (!error && status.type() != std::filesystem::file_type::not_found)
            {
                if (std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status))
                {
                    return KeireHub::HubStatus::Failure(
                        {.Code = KeireHub::HubErrorCode::UnsafeInstallRoot,
                         .Message = "A worker staging path is occupied by an unsafe object.",
                         .AffectedItem = normalized.filename().string(),
                         .TechnicalDetails = {},
                         .LogReference = {}});
                }
                std::filesystem::remove_all(NativeIoPath(normalized), error);
                if (error)
                {
                    return KeireHub::HubStatus::Failure(
                        {.Code = KeireHub::HubErrorCode::IoWrite,
                         .Message = "A stale worker staging directory could not be removed.",
                         .Retryable = true,
                         .AffectedItem = normalized.filename().string(),
                         .TechnicalDetails = error.message(),
                         .LogReference = {}});
                }
            }
            m_Paths.push_back(normalized);
            return KeireHub::HubStatus::Success();
        }

        void Release(const std::filesystem::path& path)
        {
            const auto normalized = path.lexically_normal();
            std::erase(m_Paths, normalized);
        }

      private:
        std::filesystem::path m_AllowedParent;
        std::vector<std::filesystem::path> m_Paths;
    };

    class WorkerReporter final
    {
      public:
        WorkerReporter(const CommandLine& commandLine, const KeireHub::HubWorkerRequest& request,
                       const std::uint64_t processId)
            : m_CommandLine(commandLine), m_Request(request), m_ProcessId(processId)
        {
        }

        void Publish(const KeireHub::HubTaskState state, const KeireHub::HubTaskProgress& progress)
        {
            const auto status =
                KeireHub::WriteHubWorkerStatus(m_CommandLine.Status, {.TaskId = m_Request.TaskId,
                                                                      .State = state,
                                                                      .Progress = progress,
                                                                      .WorkerProcessId = m_ProcessId,
                                                                      .UpdatedUnixSeconds = NowUnixSeconds()});
            if (!status && !m_Failure)
                m_Failure = status.Error();
        }

        [[nodiscard]] KeireHub::DownloadControl Control()
        {
            if (m_Failure)
                return KeireHub::DownloadControl::Cancel;
            auto control = KeireHub::ReadHubWorkerControl(m_CommandLine.Control);
            if (!control)
            {
                m_Failure = control.Error();
                return KeireHub::DownloadControl::Cancel;
            }
            return control.Value();
        }

        [[nodiscard]] const std::optional<KeireHub::HubError>& Failure() const noexcept { return m_Failure; }

      private:
        [[nodiscard]] static std::uint64_t NowUnixSeconds() noexcept
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count());
        }

        const CommandLine& m_CommandLine;
        const KeireHub::HubWorkerRequest& m_Request;
        std::uint64_t m_ProcessId = 0;
        std::optional<KeireHub::HubError> m_Failure;
    };

    [[nodiscard]] CommandLine Parse(const int argc, char* const* argv)
    {
        CommandLine result;
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view option = argv[index];
            const auto requirePath = [&]()
            {
                if (++index >= argc)
                    throw std::invalid_argument(std::string(option) + " requires a path.");
                const auto* first = reinterpret_cast<const char8_t*>(argv[index]);
                return std::filesystem::path(std::u8string(first, first + std::char_traits<char>::length(argv[index])));
            };
            if (option == "--request")
                result.Request = requirePath();
            else if (option == "--status")
                result.Status = requirePath();
            else if (option == "--result")
                result.Result = requirePath();
            else if (option == "--control")
                result.Control = requirePath();
            else
                throw std::invalid_argument("Unknown Hub worker option: " + std::string(option));
        }
        if (result.Request.empty() || result.Status.empty() || result.Result.empty() || result.Control.empty())
            throw std::invalid_argument("Hub worker requires request, status, result, and control paths.");
        return result;
    }

    [[nodiscard]] bool ShareOperationRoot(const CommandLine& commandLine)
    {
        std::error_code error;
        const auto root = std::filesystem::weakly_canonical(commandLine.Request.parent_path(), error);
        if (error || root.empty())
            return false;
        std::set<std::filesystem::path> names;
        for (const auto& path : {commandLine.Request, commandLine.Status, commandLine.Result, commandLine.Control})
        {
            const auto parent = std::filesystem::weakly_canonical(path.parent_path(), error);
            if (error || parent != root || path.filename().empty() || !names.insert(path.filename()).second)
                return false;
            const bool exists = std::filesystem::exists(path, error);
            if (error)
                return false;
            if (exists && !std::filesystem::is_regular_file(std::filesystem::symlink_status(path, error)))
                return false;
            if (error)
                return false;
        }
        return std::filesystem::is_regular_file(std::filesystem::symlink_status(commandLine.Request, error)) && !error;
    }

    [[nodiscard]] std::uint64_t ProcessId() noexcept
    {
#if defined(_WIN32)
        return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
        return static_cast<std::uint64_t>(getpid());
#endif
    }

    [[nodiscard]] std::uint64_t TotalDownloadBytes(const KeireHub::HubWorkerRequest& request) noexcept
    {
        if (request.EditorRemoval)
            return 0;
        if (!request.EditorInstall)
            return request.Download.SizeBytes;
        std::uint64_t result = 0;
        for (const auto& step : request.EditorInstall->PackageSteps)
            result += step.Download.SizeBytes;
        return result;
    }

    [[nodiscard]] KeireHub::HubError WorkerFailure(const KeireHub::HubErrorCode code, std::string message,
                                                   std::string item = {}, std::string details = {},
                                                   const bool retryable = false)
    {
        return {.Code = code,
                .Message = std::move(message),
                .Retryable = retryable,
                .AffectedItem = std::move(item),
                .TechnicalDetails = std::move(details),
                .LogReference = {}};
    }

    [[nodiscard]] bool IsRegularDirectoryWithoutLinks(const std::filesystem::path& path)
    {
        if (HasUnsafeLinkAncestor(path))
            return false;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(NativeIoPath(path), error);
        return !error && status.type() == std::filesystem::file_type::directory;
    }

    [[nodiscard]] bool ExistsWithoutError(const std::filesystem::path& path)
    {
        std::error_code error;
        const bool exists = std::filesystem::exists(NativeIoPath(path), error);
        return exists && !error;
    }

    [[nodiscard]] bool OwnsPublishRecoveryState(const KeireHub::PackagePublishPaths& paths,
                                                const std::string& operationId)
    {
        const auto lockStaging = paths.AllowedParent / (".keire-publish-lock-" + operationId);
        if (ExistsWithoutError(lockStaging))
            return true;
        if (!ExistsWithoutError(paths.LockRoot))
            return false;
        const auto journal = KeireHub::LoadPackagePublishJournal(paths.AllowedParent, paths.Journal);
        return journal && journal.Value().OperationId == operationId;
    }

    [[nodiscard]] KeireHub::HubResult<KeireHub::DownloadResult> Acquire(const KeireHub::DownloadRequest& request,
                                                                        const KeireHub::DownloadCallbacks& callbacks)
    {
        KeireHub::DownloadManager manager;
        if (request.Url.starts_with("file://"))
        {
            KeireHub::FileDownloadTransport transport;
            return manager.Acquire(request, transport, callbacks);
        }
        auto transport = KeireHub::NativeHttpTransport::Create(
            {.CustomProxyUrl = request.CustomProxyUrl,
             .AllowInsecureLoopbackDevelopment = request.AllowInsecureLoopbackDevelopment});
        if (!transport)
            return KeireHub::HubResult<KeireHub::DownloadResult>::Failure(transport.Error());
        return manager.Acquire(request, transport.Value(), callbacks);
    }

    [[nodiscard]] int WriteFailure(const CommandLine& commandLine, const KeireHub::HubWorkerRequest& request,
                                   WorkerReporter& reporter, KeireHub::HubError failure,
                                   const KeireHub::HubTaskProgress& progress)
    {
        reporter.Publish(KeireHub::HubTaskState::Failed, progress);
        if (reporter.Failure())
            failure = *reporter.Failure();
        const auto status =
            KeireHub::WriteHubWorkerResult(commandLine.Result, {.TaskId = request.TaskId,
                                                                .Outcome = KeireHub::DownloadOutcome::Failed,
                                                                .CachePath = {},
                                                                .InstalledRoot = {},
                                                                .InstallationId = {},
                                                                .Failure = std::move(failure)});
        return status ? 1 : 2;
    }

    [[nodiscard]] int WriteStopped(const CommandLine& commandLine, const KeireHub::HubWorkerRequest& request,
                                   WorkerReporter& reporter, const KeireHub::DownloadOutcome outcome,
                                   KeireHub::HubTaskProgress progress, const std::filesystem::path& cachePath = {})
    {
        const auto state = outcome == KeireHub::DownloadOutcome::Paused ? KeireHub::HubTaskState::Paused
                                                                        : KeireHub::HubTaskState::Cancelled;
        progress.Phase = outcome == KeireHub::DownloadOutcome::Paused ? "Paused" : "Cancelled";
        reporter.Publish(state, progress);
        const auto status = KeireHub::WriteHubWorkerResult(commandLine.Result, {.TaskId = request.TaskId,
                                                                                .Outcome = outcome,
                                                                                .CachePath = cachePath,
                                                                                .InstalledRoot = {},
                                                                                .InstallationId = {},
                                                                                .Failure = {}});
        return status ? 0 : 2;
    }

    [[nodiscard]] std::vector<KeireHub::PackageManifest>
    PublicationManifests(const KeireHub::HubWorkerEditorInstallRequest& install)
    {
        std::vector<KeireHub::PackageManifest> result;
        result.reserve(install.PackageSteps.size());
        result.push_back(install.Package);
        for (const auto& step : install.PackageSteps)
            if (step.Package.Id != install.Package.Id)
                result.push_back(step.Package);
        return result;
    }

    [[nodiscard]] KeireHub::HubResult<KeireHub::PackageManifest>
    FinalizedPublicationManifest(const KeireHub::HubWorkerEditorInstallRequest& install,
                                 const std::filesystem::path& root)
    {
        auto marker = KeireHub::EditorInstallationRegistry::ReadManagedMarker(root);
        if (!marker || marker.Value().InstallationId != install.InstallationId ||
            marker.Value().Nonce != install.MarkerNonce)
        {
            return KeireHub::HubResult<KeireHub::PackageManifest>::Failure(WorkerFailure(
                KeireHub::HubErrorCode::UnsafeInstallRoot,
                "The existing managed editor marker does not match this operation.", install.InstallationId));
        }
        auto prepared = KeireHub::PrepareManagedEditorPackage({.PackageRoot = root,
                                                               .InstallationRoot = install.Destination,
                                                               .InstallationId = install.InstallationId,
                                                               .MarkerNonce = install.MarkerNonce,
                                                               .HostPlatform = install.HostPlatform,
                                                               .HostArchitecture = install.HostArchitecture,
                                                               .VerifiedUnixSeconds = install.VerifiedUnixSeconds,
                                                               .RequirePackageReceipt = true});
        if (!prepared)
            return KeireHub::HubResult<KeireHub::PackageManifest>::Failure(prepared.Error());
        const auto manifests = PublicationManifests(install);
        auto base = KeireHub::CreatePackagePublicationManifest(manifests);
        if (!base)
            return KeireHub::HubResult<KeireHub::PackageManifest>::Failure(base.Error());
        auto withReceipt = KeireHub::FinalizePackageAssemblyReceipt(root, base.Value(), manifests);
        if (!withReceipt)
            return KeireHub::HubResult<KeireHub::PackageManifest>::Failure(withReceipt.Error());
        return KeireHub::FinalizePackageAssemblyMarker(root, withReceipt.Value());
    }

    [[nodiscard]] KeireHub::HubStatus AuthorizeRepairRoot(const KeireHub::HubWorkerEditorInstallRequest& install,
                                                          const std::filesystem::path& root,
                                                          const bool requireExactReceipt, const bool probeEditorProcess)
    {
        if (install.Mode != KeireHub::HubWorkerEditorInstallMode::Repair || !install.RepairAuthorization ||
            !IsRegularDirectoryWithoutLinks(root))
        {
            return KeireHub::HubStatus::Failure(WorkerFailure(
                KeireHub::HubErrorCode::UnsafeInstallRoot,
                "The managed editor repair destination is unavailable or unsafe.", install.InstallationId));
        }
        const auto marker = KeireHub::EditorInstallationRegistry::ReadManagedMarker(root);
        const auto& authorization = *install.RepairAuthorization;
        if (!marker || marker.Value().InstallationId != install.InstallationId ||
            marker.Value().ManifestFingerprint != authorization.ManifestFingerprint ||
            marker.Value().ReceiptSha256 != authorization.PackageReceiptSha256 ||
            marker.Value().Nonce != install.MarkerNonce)
        {
            return KeireHub::HubStatus::Failure(
                WorkerFailure(KeireHub::HubErrorCode::UnsafeInstallRoot,
                              "The managed editor repair authorization no longer matches the installed editor.",
                              install.InstallationId));
        }
        if (probeEditorProcess)
        {
            const auto activity = KeireHub::ProbeEditorEntrypointProcessActivity(root / authorization.EditorEntrypoint);
            if (activity != KeireHub::EditorEntrypointActivity::NotRunning)
            {
                return KeireHub::HubStatus::Failure(
                    WorkerFailure(KeireHub::HubErrorCode::EditorRunning,
                                  activity == KeireHub::EditorEntrypointActivity::Running
                                      ? "Close the editor before repairing this installation."
                                      : "The Hub could not confirm that the editor is closed, so repair was stopped.",
                                  install.InstallationId, {}, true));
            }
        }
        const auto receipt = KeireHub::ReadPackageInstallReceipt(root);
        if ((requireExactReceipt && !receipt) ||
            (receipt && (receipt.Value().DocumentSha256 != authorization.PackageReceiptSha256 ||
                         receipt.Value().AggregateIdentitySha256 != authorization.PackageTreeIdentity)))
        {
            return KeireHub::HubStatus::Failure(WorkerFailure(
                KeireHub::HubErrorCode::UnsafeInstallRoot,
                "The managed editor repair receipt no longer matches its authorization.", install.InstallationId));
        }
        return KeireHub::HubStatus::Success();
    }

    [[nodiscard]] KeireHub::HubStatus AuthorizeRepairPublication(const KeireHub::HubWorkerEditorInstallRequest& install,
                                                                 const KeireHub::PackagePublishJournal& journal)
    {
        if (install.Mode != KeireHub::HubWorkerEditorInstallMode::Repair || !journal.ReplacesExisting)
        {
            return KeireHub::HubStatus::Failure(WorkerFailure(
                KeireHub::HubErrorCode::UnsafeInstallRoot,
                "The managed editor repair publication is not an authorized replacement.", install.InstallationId));
        }
        if (IsRegularDirectoryWithoutLinks(journal.Paths.StagingRoot))
            if (const auto staging = AuthorizeRepairRoot(install, journal.Paths.StagingRoot, true, false); !staging)
                return staging;
        for (const auto& root : {journal.Paths.Destination, journal.Paths.BackupRoot})
        {
            if (!ExistsWithoutError(root))
                continue;
            if (const auto existing = AuthorizeRepairRoot(install, root, false, true); !existing)
                return existing;
        }
        return KeireHub::HubStatus::Success();
    }

    [[nodiscard]] KeireHub::PackagePublishOptions
    EditorPublishOptions(const KeireHub::HubWorkerEditorInstallRequest& install)
    {
        KeireHub::PackagePublishOptions result{.DestinationPolicy =
                                                   install.Mode == KeireHub::HubWorkerEditorInstallMode::Repair
                                                       ? KeireHub::PackagePublishDestinationPolicy::RequireExisting
                                                       : KeireHub::PackagePublishDestinationPolicy::RequireAbsent};
        if (install.Mode == KeireHub::HubWorkerEditorInstallMode::Repair)
        {
            result.AuthorizeMutation = [&install](const KeireHub::PackagePublishJournal& journal)
            { return AuthorizeRepairPublication(install, journal); };
        }
        return result;
    }

    [[nodiscard]] KeireHub::HubResult<bool>
    RecoverOrRecognizePublishedInstall(const KeireHub::HubWorkerRequest& request,
                                       const KeireHub::PackagePublishPaths& paths,
                                       const KeireHub::PackagePublishOptions& publishOptions)
    {
        const auto& install = *request.EditorInstall;
        const auto lockStaging = install.AllowedInstallRoot / (".keire-publish-lock-" + request.TaskId);
        const bool ownsRecoveryState = OwnsPublishRecoveryState(paths, request.TaskId);
        if (ExistsWithoutError(paths.LockRoot) && !ownsRecoveryState)
        {
            return KeireHub::HubResult<bool>::Failure(WorkerFailure(
                KeireHub::HubErrorCode::InstallationBusy,
                "Another editor installation is being published to this location.", request.TaskId, {}, true));
        }
        const bool hasRecoveryState = ownsRecoveryState || ExistsWithoutError(lockStaging);
        const auto manifestRoot = IsRegularDirectoryWithoutLinks(paths.StagingRoot)   ? paths.StagingRoot
                                  : IsRegularDirectoryWithoutLinks(paths.Destination) ? paths.Destination
                                                                                      : std::filesystem::path{};
        if (hasRecoveryState)
        {
            if (manifestRoot.empty())
            {
                return KeireHub::HubResult<bool>::Failure(WorkerFailure(
                    KeireHub::HubErrorCode::WorkerInterrupted,
                    "An interrupted editor publication has no recoverable package tree.", request.TaskId, {}, true));
            }
            auto manifest = FinalizedPublicationManifest(install, manifestRoot);
            if (!manifest)
                return KeireHub::HubResult<bool>::Failure(manifest.Error());
            const auto recovered = KeireHub::RecoverPackagePublish(install.AllowedInstallRoot, paths.Journal,
                                                                   manifest.Value(), request.TaskId, publishOptions);
            if (recovered)
                return KeireHub::HubResult<bool>::Success(true);
            if (recovered.Error().Code != KeireHub::HubErrorCode::WorkerInterrupted || !recovered.Error().Retryable)
                return KeireHub::HubResult<bool>::Failure(recovered.Error());
        }

        if (!IsRegularDirectoryWithoutLinks(paths.Destination))
            return KeireHub::HubResult<bool>::Success(false);
        if (install.Mode == KeireHub::HubWorkerEditorInstallMode::Repair)
        {
            if (const auto authorized = AuthorizeRepairRoot(install, install.Destination, false, true); !authorized)
                return KeireHub::HubResult<bool>::Failure(authorized.Error());
            auto manifest = FinalizedPublicationManifest(install, paths.Destination);
            if (manifest && KeireHub::ValidatePackageTree(paths.Destination, manifest.Value()))
                return KeireHub::HubResult<bool>::Success(true);
            return KeireHub::HubResult<bool>::Success(false);
        }
        auto manifest = FinalizedPublicationManifest(install, paths.Destination);
        if (!manifest)
        {
            return KeireHub::HubResult<bool>::Failure(WorkerFailure(KeireHub::HubErrorCode::DestinationConflict,
                                                                    "The editor destination is already occupied.",
                                                                    install.InstallationId));
        }
        if (const auto status = KeireHub::ValidatePackageTree(paths.Destination, manifest.Value()); !status)
        {
            return KeireHub::HubResult<bool>::Failure(WorkerFailure(
                KeireHub::HubErrorCode::DestinationConflict,
                "The editor destination contains a different or damaged installation.", install.InstallationId));
        }
        return KeireHub::HubResult<bool>::Success(true);
    }

    [[nodiscard]] int CompleteInstall(const CommandLine& commandLine, const KeireHub::HubWorkerRequest& request,
                                      WorkerReporter& reporter, const std::filesystem::path& primaryCachePath)
    {
        const auto& install = *request.EditorInstall;
        const auto totalBytes = TotalDownloadBytes(request);
        reporter.Publish(KeireHub::HubTaskState::Completed, {.BytesTransferred = totalBytes,
                                                             .TotalBytes = totalBytes,
                                                             .CurrentPackage = install.Package.Id,
                                                             .RemainingComponents = 0,
                                                             .Phase = "Completed"});
        const auto result =
            KeireHub::WriteHubWorkerResult(commandLine.Result, {.TaskId = request.TaskId,
                                                                .Outcome = KeireHub::DownloadOutcome::Completed,
                                                                .CachePath = primaryCachePath,
                                                                .InstalledRoot = install.Destination,
                                                                .InstallationId = install.InstallationId,
                                                                .Failure = {}});
        return result ? 0 : 2;
    }

    [[nodiscard]] std::string_view RemovalPhase(const KeireHub::ManagedEditorRemovalPhase phase) noexcept
    {
        switch (phase)
        {
        case KeireHub::ManagedEditorRemovalPhase::Prepared:
            return "Removal authorized";
        case KeireHub::ManagedEditorRemovalPhase::RootRenamed:
            return "Installation hidden";
        case KeireHub::ManagedEditorRemovalPhase::Purging:
            return "Removing files";
        case KeireHub::ManagedEditorRemovalPhase::RemovingAnchors:
            return "Finalizing removal";
        }
        return "Removing";
    }

    [[nodiscard]] int RunEditorRemoval(const CommandLine& commandLine, const KeireHub::HubWorkerRequest& request,
                                       WorkerReporter& reporter)
    {
        const auto& removal = *request.EditorRemoval;
        const KeireHub::EditorManagedOperationPlan plan{.Operation = KeireHub::EditorManagedOperation::Remove,
                                                        .InstallationId = removal.InstallationId,
                                                        .Root = removal.Root,
                                                        .ManifestFingerprint = removal.ManifestFingerprint,
                                                        .PackageTreeIdentity = removal.PackageTreeIdentity,
                                                        .PackageReceiptSha256 = removal.PackageReceiptSha256,
                                                        .MarkerNonce = removal.MarkerNonce,
                                                        .CurrentHealth = KeireHub::InstallationHealth::Healthy};
        reporter.Publish(KeireHub::HubTaskState::Installing,
                         {.CurrentPackage = removal.InstallationId, .Phase = "Authorizing removal"});
        auto removed = KeireHub::RemoveManagedEditorInstallation(
            plan, request.TaskId,
            {.CancelBeforeCommit = [&]()
             { return reporter.Failure().has_value() || reporter.Control() == KeireHub::DownloadControl::Cancel; },
             .ContinueAfterPhase =
                 [&](const KeireHub::ManagedEditorRemovalPhase phase)
             {
                 reporter.Publish(KeireHub::HubTaskState::Installing, {.CurrentPackage = removal.InstallationId,
                                                                       .Phase = std::string(RemovalPhase(phase))});
                 return !reporter.Failure();
             }});
        if (!removed)
        {
            return WriteFailure(commandLine, request, reporter, removed.Error(),
                                {.CurrentPackage = removal.InstallationId, .Phase = "Removal failed"});
        }
        if (removed.Value().CancelledBeforeCommit)
        {
            return WriteStopped(commandLine, request, reporter, KeireHub::DownloadOutcome::Cancelled,
                                {.CurrentPackage = removal.InstallationId});
        }
        if (!removed.Value().Completed)
        {
            return WriteFailure(commandLine, request, reporter,
                                WorkerFailure(KeireHub::HubErrorCode::WorkerInterrupted,
                                              "The editor removal stopped before reaching a clean state.",
                                              removal.InstallationId, {}, true),
                                {.CurrentPackage = removal.InstallationId, .Phase = "Removal interrupted"});
        }
        reporter.Publish(KeireHub::HubTaskState::Completed,
                         {.CurrentPackage = removal.InstallationId, .Phase = "Completed"});
        const auto result =
            KeireHub::WriteHubWorkerResult(commandLine.Result, {.TaskId = request.TaskId,
                                                                .Outcome = KeireHub::DownloadOutcome::Completed,
                                                                .RemovedRoot = removal.Root,
                                                                .InstallationId = removal.InstallationId});
        return result ? 0 : 2;
    }

    [[nodiscard]] int RunDownload(const CommandLine& commandLine, const KeireHub::HubWorkerRequest& request,
                                  WorkerReporter& reporter)
    {
        reporter.Publish(KeireHub::HubTaskState::Downloading, {.TotalBytes = request.Download.SizeBytes,
                                                               .CurrentPackage = request.Download.PackageId,
                                                               .Phase = "Starting"});
        const KeireHub::DownloadCallbacks callbacks{.Control = [&]() { return reporter.Control(); },
                                                    .Progress =
                                                        [&](const KeireHub::DownloadProgress& progress)
                                                    {
                                                        reporter.Publish(progress.Phase == "Verifying"
                                                                             ? KeireHub::HubTaskState::Verifying
                                                                             : KeireHub::HubTaskState::Downloading,
                                                                         {.BytesTransferred = progress.BytesTransferred,
                                                                          .TotalBytes = progress.TotalBytes,
                                                                          .BytesPerSecond = progress.BytesPerSecond,
                                                                          .Attempt = progress.Attempt,
                                                                          .CurrentPackage = request.Download.PackageId,
                                                                          .Phase = progress.Phase});
                                                    },
                                                    .WaitBeforeRetry = {},
                                                    .MonotonicNow = {},
                                                    .WaitForThrottle = {}};
        auto acquired = Acquire(request.Download, callbacks);
        if (reporter.Failure())
            acquired = KeireHub::HubResult<KeireHub::DownloadResult>::Failure(*reporter.Failure());
        if (!acquired)
        {
            return WriteFailure(commandLine, request, reporter, acquired.Error(),
                                {.TotalBytes = request.Download.SizeBytes,
                                 .CurrentPackage = request.Download.PackageId,
                                 .Phase = "Failed"});
        }
        if (acquired.Value().Outcome != KeireHub::DownloadOutcome::Completed)
        {
            return WriteStopped(commandLine, request, reporter, acquired.Value().Outcome,
                                {.BytesTransferred = acquired.Value().BytesTransferred,
                                 .TotalBytes = request.Download.SizeBytes,
                                 .CurrentPackage = request.Download.PackageId,
                                 .RemainingComponents = 0,
                                 .Phase = {}},
                                acquired.Value().CachePath);
        }
        reporter.Publish(KeireHub::HubTaskState::Completed, {.BytesTransferred = request.Download.SizeBytes,
                                                             .TotalBytes = request.Download.SizeBytes,
                                                             .CurrentPackage = request.Download.PackageId,
                                                             .Phase = "Completed"});
        const auto result = KeireHub::WriteHubWorkerResult(commandLine.Result, {.TaskId = request.TaskId,
                                                                                .Outcome = acquired.Value().Outcome,
                                                                                .CachePath = acquired.Value().CachePath,
                                                                                .InstalledRoot = {},
                                                                                .InstallationId = {},
                                                                                .Failure = {}});
        return result ? 0 : 2;
    }

    [[nodiscard]] int RunEditorInstall(const CommandLine& commandLine, const KeireHub::HubWorkerRequest& request,
                                       WorkerReporter& reporter)
    {
        const auto& install = *request.EditorInstall;
        const auto totalBytes = TotalDownloadBytes(request);
        const auto packageCount = install.PackageSteps.size();
        reporter.Publish(KeireHub::HubTaskState::Downloading,
                         {.TotalBytes = totalBytes,
                          .CurrentPackage = install.PackageSteps.front().Package.Id,
                          .RemainingComponents = static_cast<std::uint32_t>(packageCount - 1U),
                          .Phase = "Starting"});

        if (!IsSafeDirectoryBoundary(install.AllowedInstallRoot))
        {
            return WriteFailure(commandLine, request, reporter,
                                WorkerFailure(KeireHub::HubErrorCode::UnsafeInstallRoot,
                                              "The editor installation root is unavailable or unsafe.",
                                              install.InstallationId),
                                {.TotalBytes = totalBytes, .CurrentPackage = install.Package.Id, .Phase = "Failed"});
        }

        auto publishPlan =
            KeireHub::PlanPackagePublish(install.AllowedInstallRoot, install.Destination, request.TaskId);
        if (!publishPlan)
        {
            return WriteFailure(commandLine, request, reporter, publishPlan.Error(),
                                {.TotalBytes = totalBytes, .CurrentPackage = install.Package.Id, .Phase = "Failed"});
        }
        const auto publishOptions = EditorPublishOptions(install);

        auto recovered = RecoverOrRecognizePublishedInstall(request, publishPlan.Value(), publishOptions);
        if (!recovered)
        {
            return WriteFailure(commandLine, request, reporter, recovered.Error(),
                                {.TotalBytes = totalBytes, .CurrentPackage = install.Package.Id, .Phase = "Failed"});
        }
        if (recovered.Value())
            return CompleteInstall(commandLine, request, reporter,
                                   KeireHub::DownloadManager::CachePath(request.Download));

        std::vector<AcquiredPackage> acquiredPackages;
        acquiredPackages.reserve(packageCount);
        std::uint64_t completedDownloadBytes = 0;
        for (std::size_t index = 0; index < packageCount; ++index)
        {
            const auto& step = install.PackageSteps[index];
            const auto remaining = static_cast<std::uint32_t>(packageCount - index - 1U);
            const KeireHub::DownloadCallbacks callbacks{
                .Control = [&]() { return reporter.Control(); },
                .Progress =
                    [&](const KeireHub::DownloadProgress& progress)
                {
                    const auto aggregateBytes = completedDownloadBytes + progress.BytesTransferred;
                    reporter.Publish(KeireHub::HubTaskState::Downloading, {.BytesTransferred = aggregateBytes,
                                                                           .TotalBytes = totalBytes,
                                                                           .BytesPerSecond = progress.BytesPerSecond,
                                                                           .Attempt = progress.Attempt,
                                                                           .CurrentPackage = step.Package.Id,
                                                                           .RemainingComponents = remaining,
                                                                           .Phase = progress.Phase});
                },
                .WaitBeforeRetry = {},
                .MonotonicNow = {},
                .WaitForThrottle = {}};
            auto acquired = Acquire(step.Download, callbacks);
            if (reporter.Failure())
                acquired = KeireHub::HubResult<KeireHub::DownloadResult>::Failure(*reporter.Failure());
            if (!acquired)
            {
                return WriteFailure(commandLine, request, reporter, acquired.Error(),
                                    {.BytesTransferred = completedDownloadBytes,
                                     .TotalBytes = totalBytes,
                                     .CurrentPackage = step.Package.Id,
                                     .RemainingComponents = remaining,
                                     .Phase = "Failed"});
            }
            if (acquired.Value().Outcome != KeireHub::DownloadOutcome::Completed)
            {
                return WriteStopped(commandLine, request, reporter, acquired.Value().Outcome,
                                    {.BytesTransferred = completedDownloadBytes + acquired.Value().BytesTransferred,
                                     .TotalBytes = totalBytes,
                                     .CurrentPackage = step.Package.Id,
                                     .RemainingComponents = remaining,
                                     .Phase = {}},
                                    acquired.Value().CachePath);
            }
            completedDownloadBytes += step.Download.SizeBytes;
            acquiredPackages.push_back({.Manifest = step.Package, .CachePath = acquired.Value().CachePath});
        }

        reporter.Publish(KeireHub::HubTaskState::Verifying, {.BytesTransferred = totalBytes,
                                                             .TotalBytes = totalBytes,
                                                             .CurrentPackage = install.PackageSteps.back().Package.Id,
                                                             .RemainingComponents = 0,
                                                             .Phase = "Verified"});
        if (reporter.Failure())
        {
            return WriteFailure(commandLine, request, reporter, *reporter.Failure(),
                                {.BytesTransferred = totalBytes,
                                 .TotalBytes = totalBytes,
                                 .CurrentPackage = install.Package.Id,
                                 .Phase = "Failed"});
        }
        const auto beforeExtraction = reporter.Control();
        if (reporter.Failure())
        {
            return WriteFailure(commandLine, request, reporter, *reporter.Failure(),
                                {.BytesTransferred = totalBytes,
                                 .TotalBytes = totalBytes,
                                 .CurrentPackage = install.Package.Id,
                                 .Phase = "Failed"});
        }
        // Pause is a transfer-only operation. Once every archive is verified, the worker either completes the
        // non-published staging work or honors cancellation before the atomic publication boundary.
        if (beforeExtraction == KeireHub::DownloadControl::Cancel)
        {
            return WriteStopped(commandLine, request, reporter, KeireHub::DownloadOutcome::Cancelled,
                                {.BytesTransferred = totalBytes,
                                 .TotalBytes = totalBytes,
                                 .CurrentPackage = install.Package.Id,
                                 .RemainingComponents = 0,
                                 .Phase = {}},
                                acquiredPackages.front().CachePath);
        }

        OwnedStagingDirectories sourceCleanup(install.AllowedInstallRoot);
        std::vector<KeireHub::PackageAssemblySource> sources;
        sources.reserve(packageCount);
        std::optional<KeireHub::DownloadControl> extractionStop;
        for (std::size_t index = 0; index < packageCount; ++index)
        {
            const auto& package = acquiredPackages[index];
            const auto staging =
                install.AllowedInstallRoot / (".keire-stage-" + request.TaskId + "-source-" + std::to_string(index));
            if (auto status = sourceCleanup.ResetAndOwn(staging); !status)
            {
                return WriteFailure(commandLine, request, reporter, status.Error(),
                                    {.BytesTransferred = totalBytes,
                                     .TotalBytes = totalBytes,
                                     .CurrentPackage = package.Manifest.Id,
                                     .Phase = "Failed"});
            }
            const auto remaining = static_cast<std::uint32_t>(packageCount - index - 1U);
            reporter.Publish(KeireHub::HubTaskState::Extracting, {.BytesTransferred = totalBytes,
                                                                  .TotalBytes = totalBytes,
                                                                  .CurrentPackage = package.Manifest.Id,
                                                                  .RemainingComponents = remaining,
                                                                  .Phase = "Extracting"});
            const KeireHub::PackageArchiveCallbacks callbacks{
                .Cancelled =
                    [&]()
                {
                    const auto control = reporter.Control();
                    if (control == KeireHub::DownloadControl::Cancel)
                        extractionStop = control;
                    return reporter.Failure().has_value() || extractionStop.has_value();
                },
                .Progress =
                    [&](const KeireHub::PackageArchiveProgress&)
                {
                    reporter.Publish(KeireHub::HubTaskState::Extracting, {.BytesTransferred = totalBytes,
                                                                          .TotalBytes = totalBytes,
                                                                          .CurrentPackage = package.Manifest.Id,
                                                                          .RemainingComponents = remaining,
                                                                          .Phase = "Extracting"});
                }};
            auto extracted = KeireHub::ExtractPackageArchiveToStaging(
                package.CachePath, staging,
                {.SignedCatalogManifest = &package.Manifest, .AllowedStagingParent = install.AllowedInstallRoot},
                callbacks);
            if (reporter.Failure())
            {
                return WriteFailure(commandLine, request, reporter, *reporter.Failure(),
                                    {.BytesTransferred = totalBytes,
                                     .TotalBytes = totalBytes,
                                     .CurrentPackage = package.Manifest.Id,
                                     .Phase = "Failed"});
            }
            if (extractionStop)
            {
                return WriteStopped(commandLine, request, reporter, KeireHub::DownloadOutcome::Cancelled,
                                    {.BytesTransferred = totalBytes,
                                     .TotalBytes = totalBytes,
                                     .CurrentPackage = package.Manifest.Id,
                                     .RemainingComponents = remaining,
                                     .Phase = {}},
                                    acquiredPackages.front().CachePath);
            }
            if (!extracted)
            {
                return WriteFailure(commandLine, request, reporter, extracted.Error(),
                                    {.BytesTransferred = totalBytes,
                                     .TotalBytes = totalBytes,
                                     .CurrentPackage = package.Manifest.Id,
                                     .RemainingComponents = remaining,
                                     .Phase = "Failed"});
            }
            auto extraction = std::move(extracted).Value();
            sources.push_back(
                {.Root = std::move(extraction.StagingRoot), .Manifest = std::move(extraction.Metadata.Manifest)});
        }

        const auto editorSource =
            std::ranges::find(sources, install.Package.Id,
                              [](const KeireHub::PackageAssemblySource& source) { return source.Manifest.Id; });
        if (editorSource == sources.end())
        {
            return WriteFailure(commandLine, request, reporter,
                                WorkerFailure(KeireHub::HubErrorCode::WorkerProtocolInvalid,
                                              "The editor package is missing from the extracted package set.",
                                              install.Package.Id),
                                {.BytesTransferred = totalBytes,
                                 .TotalBytes = totalBytes,
                                 .CurrentPackage = install.Package.Id,
                                 .Phase = "Failed"});
        }
        std::rotate(sources.begin(), editorSource, std::next(editorSource));

        OwnedStagingDirectories assemblyCleanup(install.AllowedInstallRoot);
        if (auto status = assemblyCleanup.ResetAndOwn(publishPlan.Value().StagingRoot); !status)
        {
            return WriteFailure(commandLine, request, reporter, status.Error(),
                                {.BytesTransferred = totalBytes,
                                 .TotalBytes = totalBytes,
                                 .CurrentPackage = install.Package.Id,
                                 .Phase = "Failed"});
        }
        std::optional<KeireHub::DownloadControl> assemblyStop;
        reporter.Publish(KeireHub::HubTaskState::Installing, {.BytesTransferred = totalBytes,
                                                              .TotalBytes = totalBytes,
                                                              .CurrentPackage = install.Package.Id,
                                                              .RemainingComponents = 0,
                                                              .Phase = "Assembling"});
        auto assembly = KeireHub::AssemblePackageTreesToStaging(
            {.Sources = sources,
             .AllowedStagingParent = install.AllowedInstallRoot,
             .StagingRoot = publishPlan.Value().StagingRoot,
             .Callbacks = {.Cancelled =
                               [&]()
                           {
                               const auto control = reporter.Control();
                               if (control == KeireHub::DownloadControl::Cancel)
                                   assemblyStop = control;
                               return reporter.Failure().has_value() || assemblyStop.has_value();
                           },
                           .Progress =
                               [&](const KeireHub::PackageArchiveProgress&)
                           {
                               reporter.Publish(KeireHub::HubTaskState::Installing,
                                                {.BytesTransferred = totalBytes,
                                                 .TotalBytes = totalBytes,
                                                 .CurrentPackage = install.Package.Id,
                                                 .RemainingComponents = 0,
                                                 .Phase = "Assembling"});
                           }}});
        if (reporter.Failure())
        {
            return WriteFailure(commandLine, request, reporter, *reporter.Failure(),
                                {.BytesTransferred = totalBytes,
                                 .TotalBytes = totalBytes,
                                 .CurrentPackage = install.Package.Id,
                                 .Phase = "Failed"});
        }
        if (assemblyStop)
        {
            return WriteStopped(commandLine, request, reporter, KeireHub::DownloadOutcome::Cancelled,
                                {.BytesTransferred = totalBytes,
                                 .TotalBytes = totalBytes,
                                 .CurrentPackage = install.Package.Id,
                                 .RemainingComponents = 0,
                                 .Phase = {}},
                                acquiredPackages.front().CachePath);
        }
        if (!assembly)
        {
            return WriteFailure(commandLine, request, reporter, assembly.Error(),
                                {.BytesTransferred = totalBytes,
                                 .TotalBytes = totalBytes,
                                 .CurrentPackage = install.Package.Id,
                                 .Phase = "Failed"});
        }

        reporter.Publish(KeireHub::HubTaskState::Configuring, {.BytesTransferred = totalBytes,
                                                               .TotalBytes = totalBytes,
                                                               .CurrentPackage = install.Package.Id,
                                                               .RemainingComponents = 0,
                                                               .Phase = "Configuring"});
        auto prepared = KeireHub::PrepareManagedEditorPackage({.PackageRoot = publishPlan.Value().StagingRoot,
                                                               .InstallationRoot = install.Destination,
                                                               .InstallationId = install.InstallationId,
                                                               .MarkerNonce = install.MarkerNonce,
                                                               .HostPlatform = install.HostPlatform,
                                                               .HostArchitecture = install.HostArchitecture,
                                                               .VerifiedUnixSeconds = install.VerifiedUnixSeconds,
                                                               .RequirePackageReceipt = true});
        if (!prepared)
        {
            return WriteFailure(commandLine, request, reporter, prepared.Error(),
                                {.BytesTransferred = totalBytes,
                                 .TotalBytes = totalBytes,
                                 .CurrentPackage = install.Package.Id,
                                 .Phase = "Failed"});
        }
        auto publicationManifest = KeireHub::FinalizePackageAssemblyMarker(publishPlan.Value().StagingRoot,
                                                                           assembly.Value().PublicationManifest);
        if (!publicationManifest)
        {
            return WriteFailure(commandLine, request, reporter, publicationManifest.Error(),
                                {.BytesTransferred = totalBytes,
                                 .TotalBytes = totalBytes,
                                 .CurrentPackage = install.Package.Id,
                                 .Phase = "Failed"});
        }
        if (install.Mode == KeireHub::HubWorkerEditorInstallMode::Repair)
        {
            const auto stagedAuthorization = AuthorizeRepairRoot(install, publishPlan.Value().StagingRoot, true, false);
            if (!stagedAuthorization)
            {
                return WriteFailure(commandLine, request, reporter, stagedAuthorization.Error(),
                                    {.BytesTransferred = totalBytes,
                                     .TotalBytes = totalBytes,
                                     .CurrentPackage = install.Package.Id,
                                     .Phase = "Failed"});
            }
        }

        const auto beforePublish = reporter.Control();
        if (reporter.Failure())
        {
            return WriteFailure(commandLine, request, reporter, *reporter.Failure(),
                                {.BytesTransferred = totalBytes,
                                 .TotalBytes = totalBytes,
                                 .CurrentPackage = install.Package.Id,
                                 .Phase = "Failed"});
        }
        if (beforePublish == KeireHub::DownloadControl::Cancel)
        {
            return WriteStopped(commandLine, request, reporter, KeireHub::DownloadOutcome::Cancelled,
                                {.BytesTransferred = totalBytes,
                                 .TotalBytes = totalBytes,
                                 .CurrentPackage = install.Package.Id,
                                 .RemainingComponents = 0,
                                 .Phase = {}},
                                acquiredPackages.front().CachePath);
        }

        reporter.Publish(KeireHub::HubTaskState::Configuring, {.BytesTransferred = totalBytes,
                                                               .TotalBytes = totalBytes,
                                                               .CurrentPackage = install.Package.Id,
                                                               .RemainingComponents = 0,
                                                               .Phase = "Publishing"});
        auto published = KeireHub::PublishStagedPackage(publishPlan.Value(), publicationManifest.Value(),
                                                        request.TaskId, publishOptions);
        if (!published)
        {
            const auto recoveredPublish =
                KeireHub::RecoverPackagePublish(install.AllowedInstallRoot, publishPlan.Value().Journal,
                                                publicationManifest.Value(), request.TaskId, publishOptions);
            if (!recoveredPublish)
            {
                if (OwnsPublishRecoveryState(publishPlan.Value(), request.TaskId))
                    assemblyCleanup.Release(publishPlan.Value().StagingRoot);
                return WriteFailure(commandLine, request, reporter, published.Error(),
                                    {.BytesTransferred = totalBytes,
                                     .TotalBytes = totalBytes,
                                     .CurrentPackage = install.Package.Id,
                                     .Phase = "Failed"});
            }
        }
        return CompleteInstall(commandLine, request, reporter, acquiredPackages.front().CachePath);
    }

    [[nodiscard]] int Run(const CommandLine& commandLine)
    {
        if (!ShareOperationRoot(commandLine))
            throw std::invalid_argument("Hub worker protocol paths must share one operation directory.");
        auto request = KeireHub::ReadHubWorkerRequest(commandLine.Request);
        if (!request)
            throw std::runtime_error(request.Error().Message);
        WorkerReporter reporter(commandLine, request.Value(), ProcessId());
        if (request.Value().EditorInstall)
            return RunEditorInstall(commandLine, request.Value(), reporter);
        if (request.Value().EditorRemoval)
            return RunEditorRemoval(commandLine, request.Value(), reporter);
        return RunDownload(commandLine, request.Value(), reporter);
    }
} // namespace

int main(const int argc, char* const* argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "KeireHubWorker --request <path> --status <path> --result <path> --control <path>\n";
        return 0;
    }
    try
    {
        return Run(Parse(argc, argv));
    }
    catch (const std::exception& error)
    {
        std::cerr << "Kéire Hub worker failed: " << error.what() << '\n';
        return 2;
    }
}
