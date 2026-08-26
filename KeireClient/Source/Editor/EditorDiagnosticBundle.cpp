#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/PackageManagerPanel.h"
#include "KeireInternal/Diagnostics/DiagnosticBundleProductInternal.h"
#include "KeireInternal/Diagnostics/DiagnosticBundleUiInternal.h"

#include <algorithm>
#include <map>
#include <string>

void EditorWorkspaceLayer::OpenDiagnosticBundle()
{
    if (!m_DiagnosticBundle)
        return;

    Keire::Internal::DiagnosticBundleProductSnapshot snapshot;
    snapshot.Product = "Kéire Editor";
    if (const auto renderer = Owner().Renderer())
    {
        snapshot.RendererAvailable = renderer->Mode() != Keire::RenderMode::Disabled;
        snapshot.RendererMode = renderer->Mode();
        snapshot.RendererIdentity = renderer->DeviceIdentity();
        snapshot.RendererCapabilities = renderer->Capabilities();
        snapshot.RendererStatistics = renderer->Statistics();
        snapshot.RendererTimelines = renderer->RecentFrameTimelines();
        snapshot.LastDeviceLoss = renderer->LastDeviceLoss();
    }
    if (const auto project = Owner().GetProject())
    {
        snapshot.Project = {.Kind = "active-editor-project",
                            .SchemaVersion = project->Descriptor().SchemaVersion,
                            .AssetCount = m_AssetRecords.size(),
                            .Writable = project->Writable(),
                            .StartupSceneConfigured = static_cast<bool>(project->Descriptor().StartupScene),
                            .DefaultInputConfigured = static_cast<bool>(project->Descriptor().DefaultInput)};
    }
    else
    {
        snapshot.Project.Kind = "no-active-project";
    }

    if (m_PackageManagerPanel)
    {
        for (const auto& package : m_PackageManagerPanel->InstalledPackages())
            snapshot.Packages.push_back({package.PackageId, package.Version});
    }

    std::map<std::string, std::uint64_t, std::less<>> failureCounts;
    if (snapshot.LastDeviceLoss)
    {
        snapshot.LastFailure = Keire::Internal::DiagnosticBundleLastFailureRecord{
            .Kind = "device-loss",
            .Code = "renderer.device-loss",
            .Frame = snapshot.LastDeviceLoss->Frame,
            .DeviceGeneration = snapshot.LastDeviceLoss->DeviceGeneration};
    }
    if (const auto reports = Owner().DiagnosticReports())
    {
        std::uint64_t lastFailureSequence = 0;
        for (const auto& report : reports->Snapshot())
        {
            if (report.Severity < Keire::DiagnosticSeverity::Error)
                continue;
            const auto code = report.Id.IsValid() ? std::string(report.Id.Value()) : "editor.diagnostic.unregistered";
            ++failureCounts[code];
            if (report.Sequence >= lastFailureSequence)
            {
                snapshot.LastFailure = Keire::Internal::DiagnosticBundleLastFailureRecord{
                    .Kind = "handled-exception", .Code = code};
                lastFailureSequence = report.Sequence;
            }
        }
    }
    if (!m_Error.empty())
        ++failureCounts["editor.ui.error-present"];
    for (auto& [code, count] : failureCounts)
        snapshot.Failures.push_back({std::move(code), count});

    const auto& logging = Owner().Specification().Logging;
    snapshot.LogRoot = logging.LogDirectory;
    snapshot.LogFiles = {logging.CoreLogFile, logging.ClientLogFile};
    m_DiagnosticBundle->Open(Keire::Internal::CreateProductDiagnosticBundleRequest(snapshot),
                             "Keire-Editor-Diagnostics.zip");
}

void EditorWorkspaceLayer::DrawDiagnosticBundle(Keire::UiFrame& ui)
{
    const auto windows = Owner().Windows();
    const auto mainWindow = Owner().MainWindow();
    if (m_DiagnosticBundle && windows && mainWindow)
        m_DiagnosticBundle->Draw(ui, *windows, mainWindow->Id());
}
