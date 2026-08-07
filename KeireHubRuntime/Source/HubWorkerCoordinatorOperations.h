#pragma once

#include "KeireHubRuntime/HubWorkerCoordinator.h"
#include "KeireHubRuntime/HubWorkerProtocol.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace KeireHub::Detail
{
    [[nodiscard]] DownloadRequest CreateWorkerDownloadRequest(const CatalogPackageDownloadRequest& request);
    [[nodiscard]] HubStatus ValidateCatalogDownload(const CatalogPackageDownloadRequest& request);
    [[nodiscard]] HubWorkerRequest CreateEditorInstallWorkerRequest(const CatalogEditorInstallRequest& request);
    [[nodiscard]] HubWorkerRequest CreateEditorRepairWorkerRequest(const CatalogEditorRepairRequest& request);
    [[nodiscard]] HubWorkerRequest CreateEditorRemovalWorkerRequest(const CatalogEditorRemovalRequest& request);
    [[nodiscard]] std::vector<std::string> WorkerRequestPackageIds(const HubWorkerRequest& request);
    [[nodiscard]] std::uint64_t WorkerRequestDownloadBytes(const HubWorkerRequest& request) noexcept;
    [[nodiscard]] bool WorkerRequestMatchesTaskKind(const HubWorkerRequest& request, HubTaskKind kind) noexcept;
    [[nodiscard]] bool HasPendingWorkerResult(const std::shared_ptr<const std::vector<HubTask>>& tasks,
                                              const std::filesystem::path& operationRoot,
                                              std::uint64_t processId) noexcept;
    [[nodiscard]] HubCompletedEditorRemoval CreateCompletedEditorRemoval(std::string taskId,
                                                                         const HubWorkerEditorRemovalRequest& removal);
    [[nodiscard]] HubCompletedEditorInstall CreateCompletedEditorInstall(std::string taskId,
                                                                         const HubWorkerEditorInstallRequest& install);
} // namespace KeireHub::Detail
