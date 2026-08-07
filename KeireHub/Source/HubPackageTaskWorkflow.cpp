#include "KeireHub/HubPackageTaskWorkflow.h"

#include "KeireHub/HubEditorInstallWorkflow.h"
#include "KeireHub/HubRuntimeUiBridge.h"

#include "KeireHubRuntime/EditorInstallationManager.h"
#include "KeireHubRuntime/HubUpdateWorkflow.h"
#include "KeireHubRuntime/HubWorkerProtocol.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iterator>
#include <optional>
#include <ranges>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <cstdlib>
#else
#include <sys/random.h>
#endif

#ifndef KEIRE_HUB_WORKER_TARGET
#define KEIRE_HUB_WORKER_TARGET "KeireHubWorker"
#endif

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] std::filesystem::path ResolveWorkerExecutable(const std::filesystem::path& hubExecutable)
        {
#if defined(_WIN32)
            constexpr std::string_view Suffix = ".exe";
#else
            constexpr std::string_view Suffix = "";
#endif
            const auto filename = std::string(KEIRE_HUB_WORKER_TARGET) + std::string(Suffix);
            const auto executable = std::filesystem::absolute(hubExecutable).lexically_normal();
            const std::array candidates{executable.parent_path() / filename,
                                        executable.parent_path().parent_path() / KEIRE_HUB_WORKER_TARGET / filename};
            std::error_code error;
            const auto found = std::ranges::find_if(
                candidates,
                [&](const auto& candidate)
                {
                    error.clear();
                    const auto status = std::filesystem::symlink_status(candidate, error);
                    return !error && std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status);
                });
            return found == candidates.end() ? candidates.front() : *found;
        }

        [[nodiscard]] HubError WorkflowError(std::string message, const std::filesystem::path& affected = {})
        {
            return {.Code = HubErrorCode::WorkerInterrupted,
                    .Message = std::move(message),
                    .Retryable = true,
                    .AffectedItem = affected.empty() ? std::string{} : affected.filename().string()};
        }

        [[nodiscard]] HubResult<std::string> SecureRandomHex(const std::size_t byteCount)
        {
            std::vector<std::uint8_t> bytes(byteCount);
#if defined(_WIN32)
            if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                                BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
            {
                return HubResult<std::string>::Failure(WorkflowError("Secure task identity generation failed."));
            }
#elif defined(__APPLE__)
            arc4random_buf(bytes.data(), bytes.size());
#else
            std::size_t offset = 0;
            while (offset < bytes.size())
            {
                const auto read = getrandom(bytes.data() + offset, bytes.size() - offset, 0);
                if (read < 0 && errno == EINTR)
                    continue;
                if (read <= 0)
                {
                    return HubResult<std::string>::Failure(WorkflowError("Secure task identity generation failed."));
                }
                offset += static_cast<std::size_t>(read);
            }
#endif
            constexpr std::string_view Hex = "0123456789abcdef";
            std::string result(bytes.size() * 2, '0');
            for (std::size_t index = 0; index < bytes.size(); ++index)
            {
                result[index * 2] = Hex[bytes[index] >> 4U];
                result[index * 2 + 1] = Hex[bytes[index] & 0x0fU];
            }
            return HubResult<std::string>::Success(std::move(result));
        }

        [[nodiscard]] std::uint64_t NowUnixSeconds() noexcept
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count());
        }

        [[nodiscard]] constexpr std::string_view HostPlatform() noexcept
        {
#if defined(_WIN32)
            return "windows";
#elif defined(__APPLE__)
            return "macos";
#else
            return "linux";
#endif
        }

        [[nodiscard]] constexpr std::string_view HostArchitecture() noexcept
        {
#if defined(_M_ARM64) || defined(__aarch64__)
            return "arm64";
#else
            return "x86_64";
#endif
        }

        [[nodiscard]] HubResult<EditorInstallation>
        RegisterCompletedEditorInstall(EditorInstallationRegistry& registry, const HubCompletedEditorInstall& completed,
                                       const std::uint64_t verifiedUnixSeconds)
        {
            if (!completed.RepairsExisting)
            {
                return RegisterManagedEditorPackage(registry, completed.Root, completed.InstallationId,
                                                    std::string(HostPlatform()), std::string(HostArchitecture()),
                                                    verifiedUnixSeconds);
            }
            if (completed.ManifestFingerprint.empty() || completed.PackageTreeIdentity.empty() ||
                completed.PackageReceiptSha256.empty() || completed.MarkerNonce.empty())
            {
                return HubResult<EditorInstallation>::Failure(
                    {.Code = HubErrorCode::WorkerProtocolInvalid,
                     .Message = "The completed editor repair proof is incomplete.",
                     .AffectedItem = completed.InstallationId});
            }
            auto prepared = PrepareManagedEditorPackage({.PackageRoot = completed.Root,
                                                         .InstallationRoot = completed.Root,
                                                         .InstallationId = completed.InstallationId,
                                                         .MarkerNonce = completed.MarkerNonce,
                                                         .HostPlatform = std::string(HostPlatform()),
                                                         .HostArchitecture = std::string(HostArchitecture()),
                                                         .VerifiedUnixSeconds = verifiedUnixSeconds,
                                                         .RequirePackageReceipt = true,
                                                         .PreserveExistingMarker = true});
            if (!prepared)
                return prepared;
            const auto& installation = prepared.Value();
            if (installation.ManifestFingerprint != completed.ManifestFingerprint ||
                installation.PackageTreeIdentity != completed.PackageTreeIdentity ||
                installation.PackageReceiptSha256 != completed.PackageReceiptSha256 ||
                installation.InstalledPackages.empty() ||
                installation.InstalledPackages.front().Id != completed.PackageId)
            {
                return HubResult<EditorInstallation>::Failure(
                    {.Code = HubErrorCode::UnsafeInstallRoot,
                     .Message = "The repaired editor changed before it could be registered.",
                     .AffectedItem = completed.InstallationId});
            }
            if (const auto status = registry.Upsert(installation); !status)
                return HubResult<EditorInstallation>::Failure(status.Error());
            return prepared;
        }
    } // namespace

    HubResult<std::unique_ptr<HubPackageTaskWorkflow>>
    HubPackageTaskWorkflow::Create(HubController& controller, const std::filesystem::path& hubExecutable,
                                   const HubSettings& settings)
    {
        try
        {
            const auto worker = ResolveWorkerExecutable(hubExecutable);
            std::error_code error;
            if (!std::filesystem::is_regular_file(worker, error) || error)
            {
                return HubResult<std::unique_ptr<HubPackageTaskWorkflow>>::Failure(
                    WorkflowError("The private Hub package worker is unavailable.", worker));
            }
            auto coordinator = HubWorkerCoordinator::Create(
                {.TaskStorePath = std::filesystem::absolute(controller.Tasks().Path()).lexically_normal(),
                 .OperationRoot =
                     std::filesystem::absolute(settings.TemporaryRoot / "PackageOperations").lexically_normal(),
                 .WorkerExecutable = worker,
                 .MaximumConcurrentDownloads = settings.ConcurrentDownloads},
                CreateNativeHubWorkerProcessHost());
            if (!coordinator)
                return HubResult<std::unique_ptr<HubPackageTaskWorkflow>>::Failure(coordinator.Error());
            return HubResult<std::unique_ptr<HubPackageTaskWorkflow>>::Success(
                std::unique_ptr<HubPackageTaskWorkflow>(new HubPackageTaskWorkflow(
                    controller.Settings(), controller.Installations(), std::move(coordinator).Value())));
        }
        catch (const std::exception& error)
        {
            auto failure = WorkflowError("The Hub package task center could not start.");
            failure.TechnicalDetails = error.what();
            return HubResult<std::unique_ptr<HubPackageTaskWorkflow>>::Failure(std::move(failure));
        }
    }

    HubPackageTaskWorkflow::HubPackageTaskWorkflow(HubSettingsStore& settings,
                                                   EditorInstallationRegistry& installations,
                                                   std::unique_ptr<HubWorkerCoordinator> coordinator)
        : m_Settings(settings), m_Installations(installations), m_Coordinator(std::move(coordinator))
    {
    }

    HubPackageTaskWorkflow::~HubPackageTaskWorkflow() { Stop(); }

    HubStatus HubPackageTaskWorkflow::QueuePackageDownload(CatalogPackageDownloadRequest request)
    {
        request.BandwidthLimitBytesPerSecond = m_Settings.Snapshot()->BandwidthLimitBytesPerSecond;
        return m_Coordinator->QueuePackageDownload(std::move(request));
    }

    HubResult<std::string> HubPackageTaskWorkflow::QueueHubUpdate(const HubUpdateCandidate& candidate,
                                                                  std::string serviceBaseUrl,
                                                                  const bool allowInsecureLoopbackDevelopment)
    {
        auto taskSuffix = SecureRandomHex(16);
        if (!taskSuffix)
            return HubResult<std::string>::Failure(taskSuffix.Error());
        const auto settings = m_Settings.Snapshot();
        auto request = CreateHubUpdateDownloadRequest(
            candidate,
            {.TaskId = HubUpdateTaskPrefix(candidate) + std::move(taskSuffix).Value(),
             .ServiceBaseUrl = std::move(serviceBaseUrl),
             .CacheRoot = settings->CacheRoot,
             .AllowInsecureLoopbackDevelopment = allowInsecureLoopbackDevelopment,
             .CustomProxyUrl = settings->NetworkProxyMode == ProxyMode::Custom ? std::optional(settings->CustomProxyUrl)
                                                                               : std::nullopt,
             .BandwidthLimitBytesPerSecond = settings->BandwidthLimitBytesPerSecond});
        if (!request)
            return HubResult<std::string>::Failure(request.Error());
        auto taskId = request.Value().TaskId;
        if (const auto status = m_Coordinator->QueueHubUpdate(std::move(request).Value()); !status)
            return HubResult<std::string>::Failure(status.Error());
        return HubResult<std::string>::Success(std::move(taskId));
    }

    HubStatus HubPackageTaskWorkflow::QueueEditorInstall(const EditorInstallPlan& plan,
                                                         const HubEditorInstallEndpointContext& endpoint)
    {
        const auto editorStep = std::ranges::find(
            plan.Steps, plan.EditorPackageId, [](const EditorInstallPackageStep& step) { return step.Manifest.Id; });
        if (plan.Steps.empty() || plan.Steps.size() > MaximumHubWorkerInstallPackageSteps ||
            editorStep == plan.Steps.end() || editorStep->Manifest.Kind != PackageKind::Editor ||
            plan.Destination.empty() || plan.Destination.parent_path().empty())
        {
            return HubStatus::Failure({.Code = HubErrorCode::PackageManifestInvalid,
                                       .Message = "This editor installation plan is invalid.",
                                       .AffectedItem = plan.EditorPackageId});
        }
        const auto settings = m_Settings.Snapshot();
        auto serviceBaseUrl = endpoint.ServiceBaseUrl;
        while (serviceBaseUrl.ends_with('/'))
            serviceBaseUrl.pop_back();
        if (serviceBaseUrl.empty())
        {
            return HubStatus::Failure({.Code = HubErrorCode::DistributionConfigurationInvalid,
                                       .Message = "No trusted editor distribution service is configured.",
                                       .Retryable = true,
                                       .AffectedItem = plan.EditorPackageId});
        }
        auto taskSuffix = SecureRandomHex(16);
        if (!taskSuffix)
            return HubStatus::Failure(taskSuffix.Error());
        auto markerNonce = SecureRandomHex(32);
        if (!markerNonce)
            return HubStatus::Failure(markerNonce.Error());

        const auto taskId = "editor-install-" + taskSuffix.Value();
        std::vector<std::string> requestedPackageIds;
        for (const auto& step : plan.Steps)
            if (step.ExplicitlySelected)
                requestedPackageIds.push_back(step.Manifest.Id);
        const auto makeDownload = [&](const PackageManifest& package)
        {
            return CatalogPackageDownloadRequest{
                .TaskId = taskId,
                .Package = package,
                .PackageUrl = serviceBaseUrl + "/v1/packages/" + package.ArtifactSha256,
                .CacheRoot = settings->CacheRoot,
                .AllowInsecureLoopbackDevelopment = endpoint.AllowInsecureLoopbackDevelopment,
                .CustomProxyUrl = settings->NetworkProxyMode == ProxyMode::Custom
                                      ? std::optional(settings->CustomProxyUrl)
                                      : std::nullopt,
                .BandwidthLimitBytesPerSecond = settings->BandwidthLimitBytesPerSecond};
        };
        CatalogEditorInstallRequest request{.Download = makeDownload(plan.Steps.front().Manifest),
                                            .EditorPackage = editorStep->Manifest,
                                            .RequestedPackageIds = std::move(requestedPackageIds),
                                            .AllowedInstallRoot = plan.Destination.parent_path(),
                                            .Destination = plan.Destination,
                                            .InstallationId = "managed-editor-" + taskSuffix.Value(),
                                            .MarkerNonce = std::move(markerNonce).Value(),
                                            .HostPlatform = std::string(HostPlatform()),
                                            .HostArchitecture = std::string(HostArchitecture()),
                                            .VerifiedUnixSeconds = NowUnixSeconds()};
        request.AdditionalDownloads.reserve(plan.Steps.size() - 1);
        for (auto step = std::next(plan.Steps.begin()); step != plan.Steps.end(); ++step)
            request.AdditionalDownloads.push_back(makeDownload(step->Manifest));
        return m_Coordinator->QueueEditorInstall(std::move(request));
    }

    HubStatus HubPackageTaskWorkflow::QueueEditorRepair(const EditorRepairPlan& plan,
                                                        const HubEditorInstallEndpointContext& endpoint)
    {
        const auto& install = plan.Install;
        const auto editorStep =
            std::ranges::find(install.Steps, install.EditorPackageId,
                              [](const EditorInstallPackageStep& step) { return step.Manifest.Id; });
        if (install.Steps.empty() || install.Steps.size() > MaximumHubWorkerInstallPackageSteps ||
            editorStep == install.Steps.end() || editorStep->Manifest.Kind != PackageKind::Editor ||
            install.Destination.empty() || install.Destination.parent_path().empty() ||
            plan.ManifestFingerprint.empty() || plan.PackageTreeIdentity.empty() || plan.PackageReceiptSha256.empty() ||
            plan.MarkerNonce.empty() || plan.EditorEntrypoint.empty())
        {
            return HubStatus::Failure({.Code = HubErrorCode::PackageManifestInvalid,
                                       .Message = "This managed editor repair plan is invalid.",
                                       .AffectedItem = install.InstallationId});
        }
        const auto settings = m_Settings.Snapshot();
        auto serviceBaseUrl = endpoint.ServiceBaseUrl;
        while (serviceBaseUrl.ends_with('/'))
            serviceBaseUrl.pop_back();
        if (serviceBaseUrl.empty())
        {
            return HubStatus::Failure({.Code = HubErrorCode::DistributionConfigurationInvalid,
                                       .Message = "No trusted editor distribution service is configured.",
                                       .Retryable = true,
                                       .AffectedItem = install.InstallationId});
        }
        auto taskSuffix = SecureRandomHex(16);
        if (!taskSuffix)
            return HubStatus::Failure(taskSuffix.Error());

        const auto taskId = "editor-repair-" + taskSuffix.Value();
        std::vector<std::string> requestedPackageIds;
        for (const auto& step : install.Steps)
            if (step.ExplicitlySelected)
                requestedPackageIds.push_back(step.Manifest.Id);
        const auto makeDownload = [&](const PackageManifest& package)
        {
            return CatalogPackageDownloadRequest{
                .TaskId = taskId,
                .Package = package,
                .PackageUrl = serviceBaseUrl + "/v1/packages/" + package.ArtifactSha256,
                .CacheRoot = settings->CacheRoot,
                .AllowInsecureLoopbackDevelopment = endpoint.AllowInsecureLoopbackDevelopment,
                .CustomProxyUrl = settings->NetworkProxyMode == ProxyMode::Custom
                                      ? std::optional(settings->CustomProxyUrl)
                                      : std::nullopt,
                .BandwidthLimitBytesPerSecond = settings->BandwidthLimitBytesPerSecond};
        };
        CatalogEditorRepairRequest request{.Install = {.Download = makeDownload(install.Steps.front().Manifest),
                                                       .EditorPackage = editorStep->Manifest,
                                                       .RequestedPackageIds = std::move(requestedPackageIds),
                                                       .AllowedInstallRoot = install.Destination.parent_path(),
                                                       .Destination = install.Destination,
                                                       .InstallationId = install.InstallationId,
                                                       .MarkerNonce = plan.MarkerNonce,
                                                       .HostPlatform = std::string(HostPlatform()),
                                                       .HostArchitecture = std::string(HostArchitecture()),
                                                       .VerifiedUnixSeconds = NowUnixSeconds()},
                                           .ManifestFingerprint = plan.ManifestFingerprint,
                                           .PackageTreeIdentity = plan.PackageTreeIdentity,
                                           .PackageReceiptSha256 = plan.PackageReceiptSha256,
                                           .EditorEntrypoint = plan.EditorEntrypoint};
        request.Install.AdditionalDownloads.reserve(install.Steps.size() - 1);
        for (auto step = std::next(install.Steps.begin()); step != install.Steps.end(); ++step)
            request.Install.AdditionalDownloads.push_back(makeDownload(step->Manifest));
        return m_Coordinator->QueueEditorRepair(std::move(request));
    }

    HubStatus HubPackageTaskWorkflow::QueueEditorRemoval(const EditorManagedOperationPlan& plan)
    {
        if (plan.Operation != EditorManagedOperation::Remove || plan.Root.empty() || plan.Root.parent_path().empty())
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The managed editor removal authorization is invalid.",
                                       .AffectedItem = plan.InstallationId});
        }
        auto taskSuffix = SecureRandomHex(16);
        if (!taskSuffix)
            return HubStatus::Failure(taskSuffix.Error());
        return m_Coordinator->QueueEditorRemoval({.TaskId = "editor-remove-" + std::move(taskSuffix).Value(),
                                                  .AllowedInstallRoot = plan.Root.parent_path(),
                                                  .Root = plan.Root,
                                                  .InstallationId = plan.InstallationId,
                                                  .ManifestFingerprint = plan.ManifestFingerprint,
                                                  .PackageTreeIdentity = plan.PackageTreeIdentity,
                                                  .PackageReceiptSha256 = plan.PackageReceiptSha256,
                                                  .MarkerNonce = plan.MarkerNonce});
    }

    HubResult<bool> HubPackageTaskWorkflow::ReconcileCompletedEditorInstalls()
    {
        const auto snapshot = m_Coordinator->Snapshot();
        if (snapshot->Revision == m_ReconciledRevision || !snapshot->CompletedEditorInstalls)
            return HubResult<bool>::Success(false);
        bool changed = false;
        for (const auto& completed : *snapshot->CompletedEditorInstalls)
        {
            if (m_RegisteredInstallTasks.contains(completed.TaskId))
                continue;
            auto registered = RegisterCompletedEditorInstall(m_Installations, completed, NowUnixSeconds());
            if (!registered)
            {
                if (m_ReportedInstallRegistrationFailures.insert(completed.TaskId).second)
                    return HubResult<bool>::Failure(registered.Error());
                return HubResult<bool>::Success(false);
            }
            m_ReportedInstallRegistrationFailures.erase(completed.TaskId);
            m_RegisteredInstallTasks.insert(completed.TaskId);
            changed = true;
        }
        if (snapshot->CompletedEditorRemovals)
        {
            for (const auto& completed : *snapshot->CompletedEditorRemovals)
            {
                if (m_ReconciledRemovalTasks.contains(completed.TaskId))
                    continue;
                const auto removed = m_Installations.RemoveDeletedManagedRegistration(completed.Proof);
                if (!removed && removed.Error().Code != HubErrorCode::NotFound)
                {
                    if (m_ReportedInstallRegistrationFailures.insert(completed.TaskId).second)
                        return HubResult<bool>::Failure(removed.Error());
                    return HubResult<bool>::Success(false);
                }
                m_ReportedInstallRegistrationFailures.erase(completed.TaskId);
                m_ReconciledRemovalTasks.insert(completed.TaskId);
                changed |= static_cast<bool>(removed);
            }
        }
        m_ReconciledRevision = snapshot->Revision;
        return HubResult<bool>::Success(changed);
    }

    HubStatus HubPackageTaskWorkflow::Execute(const HubUiCommand& command)
    {
        switch (command.Type)
        {
        case HubUiCommandType::PauseTask:
            return m_Coordinator->Pause(command.ItemId);
        case HubUiCommandType::ResumeTask:
            return m_Coordinator->Resume(command.ItemId);
        case HubUiCommandType::CancelTask:
            return m_Coordinator->Cancel(command.ItemId);
        case HubUiCommandType::RetryTask:
            return m_Coordinator->Retry(command.ItemId);
        default:
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The command is not a package task action.",
                                       .AffectedItem = command.ItemId});
        }
    }

    void HubPackageTaskWorkflow::ApplySnapshot(HubProductSnapshot& product) const
    {
        const auto snapshot = m_Coordinator->Snapshot();
        ApplyHubTaskSnapshot(snapshot->Tasks ? std::span<const HubTask>(*snapshot->Tasks) : std::span<const HubTask>{},
                             product);
        if (!snapshot->Tasks)
            return;
        for (auto& editor : product.Editors)
        {
            editor.HasActiveTask = std::ranges::any_of(*snapshot->Tasks,
                                                       [&](const HubTask& task)
                                                       {
                                                           return !IsTerminal(task.State) &&
                                                                  task.TargetInstallationId &&
                                                                  *task.TargetInstallationId == editor.Id;
                                                       });
        }
    }

    std::shared_ptr<const HubWorkerCoordinatorSnapshot> HubPackageTaskWorkflow::Snapshot() const noexcept
    {
        return m_Coordinator->Snapshot();
    }

    void HubPackageTaskWorkflow::Stop() noexcept
    {
        if (m_Coordinator)
            m_Coordinator->Stop();
    }
} // namespace KeireHub
