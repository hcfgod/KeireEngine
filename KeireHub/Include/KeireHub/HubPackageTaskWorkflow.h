#pragma once

#include "KeireHub/HubProductUi.h"

#include "KeireHubRuntime/HubController.h"
#include "KeireHubRuntime/HubWorkerCoordinator.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <string>

namespace KeireHub
{
    struct EditorInstallPlan;
    struct EditorRepairPlan;
    struct EditorManagedOperationPlan;
    struct HubEditorInstallEndpointContext;
    struct HubUpdateCandidate;

    class HubPackageTaskWorkflow final
    {
      public:
        [[nodiscard]] static HubResult<std::unique_ptr<HubPackageTaskWorkflow>>
        Create(HubController& controller, const std::filesystem::path& hubExecutable, const HubSettings& settings);

        ~HubPackageTaskWorkflow();

        HubPackageTaskWorkflow(const HubPackageTaskWorkflow&) = delete;
        HubPackageTaskWorkflow& operator=(const HubPackageTaskWorkflow&) = delete;
        HubPackageTaskWorkflow(HubPackageTaskWorkflow&&) = delete;
        HubPackageTaskWorkflow& operator=(HubPackageTaskWorkflow&&) = delete;

        [[nodiscard]] HubStatus QueuePackageDownload(CatalogPackageDownloadRequest request);
        [[nodiscard]] HubResult<std::string> QueueHubUpdate(const HubUpdateCandidate& candidate,
                                                            std::string serviceBaseUrl,
                                                            bool allowInsecureLoopbackDevelopment);
        [[nodiscard]] HubStatus QueueEditorInstall(const EditorInstallPlan& plan,
                                                   const HubEditorInstallEndpointContext& endpoint);
        [[nodiscard]] HubStatus QueueEditorRepair(const EditorRepairPlan& plan,
                                                  const HubEditorInstallEndpointContext& endpoint);
        [[nodiscard]] HubStatus QueueEditorRemoval(const EditorManagedOperationPlan& plan);
        [[nodiscard]] HubResult<bool> ReconcileCompletedEditorInstalls();
        [[nodiscard]] HubStatus Execute(const HubUiCommand& command);
        void ApplySnapshot(HubProductSnapshot& product) const;
        [[nodiscard]] std::shared_ptr<const HubWorkerCoordinatorSnapshot> Snapshot() const noexcept;
        void Stop() noexcept;

      private:
        HubPackageTaskWorkflow(HubSettingsStore& settings, EditorInstallationRegistry& installations,
                               std::unique_ptr<HubWorkerCoordinator> coordinator);

        HubSettingsStore& m_Settings;
        EditorInstallationRegistry& m_Installations;
        std::unique_ptr<HubWorkerCoordinator> m_Coordinator;
        std::set<std::string, std::less<>> m_RegisteredInstallTasks;
        std::set<std::string, std::less<>> m_ReconciledRemovalTasks;
        std::set<std::string, std::less<>> m_ReportedInstallRegistrationFailures;
        std::uint64_t m_ReconciledRevision = 0;
    };
} // namespace KeireHub
