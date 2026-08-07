#pragma once

#include "KeireHub/HubDistributionWorkflow.h"
#include "KeireHub/HubProductUi.h"

#include "KeireHubRuntime/EditorInstallCatalog.h"
#include "KeireHubRuntime/EditorInstallationManager.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace KeireHub
{
    class HubPackageTaskWorkflow;

    struct HubEditorInstallEndpointContext final
    {
        std::string ServiceBaseUrl;
        bool AllowInsecureLoopbackDevelopment = false;
    };

    [[nodiscard]] std::vector<HubAvailableEditorUiRecord>
    BuildHubAvailableEditorUiRecords(const EditorInstallCatalogSnapshot& snapshot);

    [[nodiscard]] HubEditorInstallPreviewUiRecord BuildHubEditorInstallPreviewUiRecord(const EditorInstallPlan& plan);

    class HubEditorInstallWorkflow final
    {
      public:
        HubEditorInstallWorkflow(EditorInstallationRegistry& registry, std::string hostPlatform,
                                 std::string hostArchitecture);

        HubEditorInstallWorkflow(const HubEditorInstallWorkflow&) = delete;
        HubEditorInstallWorkflow& operator=(const HubEditorInstallWorkflow&) = delete;
        HubEditorInstallWorkflow(HubEditorInstallWorkflow&&) = delete;
        HubEditorInstallWorkflow& operator=(HubEditorInstallWorkflow&&) = delete;

        [[nodiscard]] HubStatus Refresh(const HubDistributionWorkflowSnapshot& distribution,
                                        const HubSettings& settings);
        void ReloadRegistrations() noexcept { m_HasRefreshKey = false; }
        [[nodiscard]] HubResult<EditorInstallPlan> PreviewInstall(const HubEditorInstallUiRequest& request);
        [[nodiscard]] HubResult<EditorRepairPlan> PreviewRepair(const EditorManagedOperationPlan& authorization);
        void ClearPreview() noexcept;
        void ApplySnapshot(HubProductSnapshot& product) const;

        [[nodiscard]] const HubEditorInstallEndpointContext& EndpointContext() const noexcept { return m_Endpoint; }

      private:
        [[nodiscard]] std::string NextInstallationId();

        EditorInstallationRegistry& m_Registry;
        std::string m_HostPlatform;
        std::string m_HostArchitecture;
        std::unique_ptr<EditorInstallCatalog> m_Catalog;
        std::shared_ptr<const DistributionCatalogSnapshot> m_LastDistribution;
        std::shared_ptr<const std::vector<EditorInstallation>> m_LastInstallations;
        std::vector<std::string> m_PopulatedChannels;
        std::shared_ptr<const std::vector<HubAvailableEditorUiRecord>> m_AvailableEditors;
        std::shared_ptr<const HubEditorInstallPreviewUiRecord> m_Preview;
        std::optional<HubError> m_CatalogFailure;
        std::optional<HubError> m_PreviewFailure;
        std::optional<HubErrorCode> m_LastDistributionFailureCode;
        std::string m_LastDistributionFailureMessage;
        HubEditorInstallEndpointContext m_Endpoint;
        bool m_EnablePreRelease = false;
        bool m_EnableNightly = false;
        bool m_Refreshing = false;
        bool m_HasRefreshKey = false;
        std::uint64_t m_NextInstallationSuffix = 1;
    };

    [[nodiscard]] HubResult<std::string> ExecuteHubEditorInstallCommand(const HubUiCommand& command,
                                                                        HubEditorInstallWorkflow& installs,
                                                                        HubPackageTaskWorkflow* packageTasks);
} // namespace KeireHub
