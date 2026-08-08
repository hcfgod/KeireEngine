#pragma once

#include "KeireHub/HubProductUi.h"

#include "KeireHubRuntime/EditorInstallationManager.h"
#include "KeireHubRuntime/HubController.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace KeireHub
{
    struct HubEditorManagementSpecification final
    {
        using RunningProbe = std::function<bool(const EditorInstallation&)>;

        std::string HostPlatform;
        std::string HostArchitecture;
        RunningProbe ProbeRunning;
    };

    struct HubEditorManagementWorkItem final
    {
        EditorInstallation Installation;
        EditorInstallationActivity Activity;
    };

    struct HubEditorManagementServices final
    {
        using RefreshService = std::function<HubResult<std::vector<EditorInstallationHealthSnapshot>>(
            std::vector<HubEditorManagementWorkItem>, std::string, std::string)>;
        using VerifyService = std::function<HubResult<EditorInstallationHealthSnapshot>(HubEditorManagementWorkItem,
                                                                                        std::string, std::string)>;
        using AuthorizationService = std::function<HubResult<EditorManagedOperationPlan>(
            HubEditorManagementWorkItem, std::filesystem::path, EditorManagedOperation, std::string, std::string)>;

        RefreshService Refresh;
        VerifyService Verify;
        AuthorizationService Authorize;
    };

    enum class HubEditorManagementOperation
    {
        None,
        Refresh,
        Verify,
        AuthorizeRepair,
        AuthorizeRemoval
    };

    enum class HubEditorManagementState
    {
        Idle,
        Running,
        Completed,
        Failed
    };

    struct HubEditorManagementOperationSnapshot final
    {
        std::uint64_t OperationId = 0;
        HubEditorManagementOperation Operation = HubEditorManagementOperation::None;
        HubEditorManagementState State = HubEditorManagementState::Idle;
        std::string InstallationId;
        std::optional<HubError> Failure;

        [[nodiscard]] bool IsRunning() const noexcept { return State == HubEditorManagementState::Running; }
        [[nodiscard]] bool IsTerminal() const noexcept
        {
            return State == HubEditorManagementState::Completed || State == HubEditorManagementState::Failed;
        }
    };

    struct HubEditorManagementCompletion final
    {
        std::uint64_t OperationId = 0;
        HubEditorManagementOperation Operation = HubEditorManagementOperation::None;
        std::string InstallationId;
        std::optional<InstallationHealth> VerifiedHealth;
        std::optional<EditorManagedOperationPlan> Authorization;
        std::optional<HubError> Failure;
    };

    [[nodiscard]] std::vector<HubEditorUiRecord>
    BuildHubEditorUiRecords(std::span<const EditorInstallationHealthSnapshot> installations,
                            std::span<const HubRecentProject> projects);

    class HubEditorManagementWorkflow final
    {
      public:
        HubEditorManagementWorkflow(HubController& controller, HubEditorManagementSpecification specification,
                                    HubEditorManagementServices services = {});
        ~HubEditorManagementWorkflow();

        HubEditorManagementWorkflow(const HubEditorManagementWorkflow&) = delete;
        HubEditorManagementWorkflow& operator=(const HubEditorManagementWorkflow&) = delete;
        HubEditorManagementWorkflow(HubEditorManagementWorkflow&&) = delete;
        HubEditorManagementWorkflow& operator=(HubEditorManagementWorkflow&&) = delete;

        [[nodiscard]] HubStatus Refresh();
        void ReloadRegistrations();
        [[nodiscard]] HubResult<bool> Poll();
        void ApplySnapshot(HubProductSnapshot& product);
        void ApplyOperationSnapshot(HubProductSnapshot& product) const;
        [[nodiscard]] HubStatus Execute(const HubUiCommand& command);
        [[nodiscard]] std::optional<HubEditorManagementCompletion> TakeCompletion();

        [[nodiscard]] std::shared_ptr<const std::vector<EditorInstallationHealthSnapshot>> Snapshot() const noexcept;
        [[nodiscard]] std::shared_ptr<const HubEditorManagementOperationSnapshot> OperationSnapshot() const;

      private:
        struct WorkerResult final
        {
            std::optional<std::vector<EditorInstallationHealthSnapshot>> Installations;
            std::optional<EditorInstallationHealthSnapshot> Verification;
            std::optional<EditorManagedOperationPlan> Authorization;
            std::optional<HubError> Failure;
        };

        [[nodiscard]] HubStatus RequireOwnerThread(std::string_view action) const;
        [[nodiscard]] HubStatus StartTargetOperation(const HubUiCommand& command,
                                                     HubEditorManagementOperation operation);
        [[nodiscard]] HubStatus ValidateCommandTarget(const HubUiCommand& command) const;
        [[nodiscard]] HubStatus ValidateCurrentTarget() const;
        [[nodiscard]] EditorInstallationActivity Activity(const EditorInstallation& installation) const;
        void PublishOperation(HubEditorManagementOperationSnapshot snapshot);
        void PublishFailure(HubError error);
        void QueueLicenseRefresh(std::shared_ptr<const std::vector<EditorInstallation>> installations);
        void StartLicenseRefresh();
        void PollLicenses(HubProductSnapshot& product);
        void JoinWorkers() noexcept;

        HubController& m_Controller;
        std::string m_HostPlatform;
        std::string m_HostArchitecture;
        HubEditorManagementSpecification::RunningProbe m_ProbeRunning;
        HubEditorManagementServices m_Services;
        std::thread::id m_OwnerThread;
        std::shared_ptr<const std::vector<EditorInstallationHealthSnapshot>> m_Snapshot;
        std::shared_ptr<const std::vector<EditorInstallation>> m_ActiveRegistrations;
        std::optional<EditorInstallation> m_ActiveTarget;
        std::future<WorkerResult> m_WorkFuture;
        std::optional<HubEditorManagementCompletion> m_Completion;
        std::uint64_t m_NextOperationId = 1;
        mutable std::mutex m_OperationMutex;
        std::shared_ptr<const HubEditorManagementOperationSnapshot> m_OperationSnapshot;
        std::shared_ptr<const std::vector<EditorInstallation>> m_ObservedInstallations;
        std::shared_ptr<const std::vector<EditorInstallation>> m_PendingLicenseInstallations;
        std::future<HubResult<std::vector<HubLicenseUiRecord>>> m_LicenseFuture;
        std::vector<HubLicenseUiRecord> m_Licenses;
        std::optional<HubError> m_LicenseFailure;
        std::uint64_t m_LicenseRevision = 0;
        std::uint64_t m_AppliedLicenseRevision = 0;
    };
} // namespace KeireHub
