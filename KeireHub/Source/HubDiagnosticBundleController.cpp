#include "KeireHub/HubDiagnosticBundleController.h"

#include "KeireHub/HubProductUi.h"
#include "KeireHubRuntime/HubController.h"
#include "KeireInternal/Diagnostics/DiagnosticBundleProductInternal.h"

#include <algorithm>
#include <ranges>
#include <string>
#include <utility>

namespace KeireHub
{
    HubDiagnosticBundleSummary CreateHubDiagnosticBundleSummary(const HubProductSnapshot& snapshot,
                                                                const HubController* controller,
                                                                const bool recentUiFailure)
    {
        HubDiagnosticBundleSummary result{.RecentProjects = snapshot.RecentProjects,
                                          .PinnedProjects = snapshot.PinnedProjects};
        if (controller)
        {
            const auto installations = controller->Snapshot().Installations;
            if (installations)
            {
                for (const auto& installation : *installations)
                {
                    for (const auto& package : installation.InstalledPackages)
                        result.Packages.push_back({package.Id, package.Version.ToString()});
                }
            }
        }
        const auto failedTasks = std::ranges::count(snapshot.Tasks, true, &HubTaskUiRecord::Retryable);
        if (failedTasks != 0)
            result.Failures.push_back({"hub.package-task.failed", static_cast<std::uint64_t>(failedTasks)});
        if (recentUiFailure)
            result.Failures.push_back({"hub.ui.error-present", 1U});
        return result;
    }

    void HubDiagnosticBundleController::Open(HubDiagnosticBundleSummary summary,
                                             const Keire::Ref<Keire::RenderSystem>& renderer,
                                             const Keire::LogConfig& logging)
    {
        Keire::Internal::DiagnosticBundleProductSnapshot snapshot;
        snapshot.Product = "Kéire Hub";
        if (renderer)
        {
            snapshot.RendererAvailable = renderer->Mode() != Keire::RenderMode::Disabled;
            snapshot.RendererMode = renderer->Mode();
            snapshot.RendererIdentity = renderer->DeviceIdentity();
            snapshot.RendererCapabilities = renderer->Capabilities();
            snapshot.RendererStatistics = renderer->Statistics();
            snapshot.RendererTimelines = renderer->RecentFrameTimelines();
            snapshot.LastDeviceLoss = renderer->LastDeviceLoss();
        }
        snapshot.Project = {.Kind = "hub-project-catalog",
                            .RecentProjectCount = summary.RecentProjects,
                            .PinnedProjectCount = summary.PinnedProjects};
        snapshot.Packages = std::move(summary.Packages);
        snapshot.Failures = std::move(summary.Failures);
        if (snapshot.LastDeviceLoss)
        {
            snapshot.LastFailure = Keire::Internal::DiagnosticBundleLastFailureRecord{
                .Kind = "device-loss",
                .Code = "renderer.device-loss",
                .Frame = snapshot.LastDeviceLoss->Frame,
                .DeviceGeneration = snapshot.LastDeviceLoss->DeviceGeneration};
        }
        else if (!snapshot.Failures.empty())
        {
            snapshot.LastFailure = Keire::Internal::DiagnosticBundleLastFailureRecord{
                .Kind = snapshot.Failures.back().Kind, .Code = snapshot.Failures.back().Code};
        }
        snapshot.LogRoot = logging.LogDirectory;
        snapshot.LogFiles = {logging.CoreLogFile, logging.ClientLogFile};
        m_Dialog.Open(Keire::Internal::CreateProductDiagnosticBundleRequest(snapshot), "Keire-Hub-Diagnostics.zip");
    }

    void HubDiagnosticBundleController::Draw(Keire::UiFrame& ui, Keire::WindowSystem& windows,
                                             const Keire::WindowId parent)
    {
        m_Dialog.Draw(ui, windows, parent);
    }

    void HubDiagnosticBundleController::Shutdown() noexcept { m_Dialog.Shutdown(); }
} // namespace KeireHub
