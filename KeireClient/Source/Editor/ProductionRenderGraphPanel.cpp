#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/EditorPanels.h"
#include "KeireClient/Editor/GpuOcclusionDiagnostics.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireInternal/Diagnostics/GraphicsCaptureInternal.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>

void EditorWorkspaceLayer::DrawRenderGraph(Keire::UiFrame& ui)
{
    if (auto panel = ui.BeginPanel(m_RenderGraph); panel)
    {
        const auto renderer = Owner().Renderer();
        if (!renderer || !renderer->IsOpen())
        {
            DrawEmptyState(ui, "Render graph unavailable", "Enable the renderer to inspect its compiled graph.",
                           "The panel captures immutable data from the renderer's compiled frame graph.");
            return;
        }

        const auto snapshot = renderer->CaptureFrameGraph();
        const auto statistics = renderer->Statistics();
        const auto sceneSurface = m_SceneViewportPanel ? m_SceneViewportPanel->OcclusionDiagnostics()
                                                       : std::optional<Keire::GpuOcclusionSurfaceDiagnostics>{};
        const auto gameSurface = m_GameRenderView && m_GameRenderView->Surface()
                                     ? std::optional(m_GameRenderView->Surface()->OcclusionDiagnostics())
                                     : std::nullopt;
        const auto playSession =
            m_SceneDocument ? m_SceneDocument->PlaySession() : Keire::Ref<Keire::SceneRuntimeSession>{};
        const bool playActive = playSession && playSession->State() != Keire::ScenePlayState::Stopped;
        const auto occlusionSurface =
            KeireEditor::SelectGpuOcclusionPanelSurface(playActive, gameSurface, sceneSurface);
        const auto occlusion = KeireEditor::BuildGpuOcclusionPanelDiagnostics(renderer->Capabilities(), statistics,
                                                                              occlusionSurface.Diagnostics);
        const auto occlusionStatus = std::string(occlusionSurface.Label) + ": " + occlusion.Status;
        ui.TextColored(occlusion.Warning                                                     ? m_Theme.Warning
                       : occlusion.State == KeireEditor::GpuOcclusionDiagnosticState::Active ? m_Theme.Success
                                                                                             : m_Theme.MutedText,
                       occlusionStatus);
        ui.Text(occlusion.Pyramid + " | " + occlusion.Readback);
        ui.Text("Passes: " + std::to_string(snapshot.Passes.size()) +
                " | Resources: " + std::to_string(snapshot.Resources.size()));
        ui.Text("Transient: " + std::to_string(snapshot.ActiveTransientBytes) +
                " bytes | Unaliased: " + std::to_string(snapshot.TheoreticalUnaliasedBytes) +
                " | Saved: " + std::to_string(snapshot.SavedAliasingBytes));
        ui.Text("Fence-retired: " + std::to_string(snapshot.FenceRetiredBytes) + " bytes");

        const auto capture = Keire::Internal::QueryGraphicsCaptureStatus();
        const auto captureProvider = capture.Provider == Keire::Internal::GraphicsCaptureProvider::RenderDoc
                                         ? std::string_view("RenderDoc")
                                         : std::string_view("none");
        const auto captureState = [&capture]() -> std::string_view
        {
            switch (capture.State)
            {
            case Keire::Internal::GraphicsCaptureState::Unavailable:
                return "unavailable; inject RenderDoc before launching the editor";
            case Keire::Internal::GraphicsCaptureState::Ready:
                return "ready for a one-frame GPU capture";
            case Keire::Internal::GraphicsCaptureState::Capturing:
                return "capture active";
            }
            return "state unavailable";
        }();
        ui.TextColored(capture.State == Keire::Internal::GraphicsCaptureState::Ready       ? m_Theme.Success
                       : capture.State == Keire::Internal::GraphicsCaptureState::Capturing ? m_Theme.Warning
                                                                                           : m_Theme.MutedText,
                       "GPU capture provider: " + std::string(captureProvider) + " / " + std::string(captureState));
        if (auto disabled = ui.BeginDisabled(capture.State != Keire::Internal::GraphicsCaptureState::Ready); disabled)
        {
            if (ui.Button("Capture Next GPU Frame"))
            {
                switch (Keire::Internal::QueueGraphicsCaptureNextFrame())
                {
                case Keire::Internal::GraphicsCaptureRequestResult::Unavailable:
                    m_RenderGraphStatus = "GPU capture unavailable; inject RenderDoc before launching the editor";
                    break;
                case Keire::Internal::GraphicsCaptureRequestResult::CaptureAlreadyActive:
                    m_RenderGraphStatus = "A GPU capture is already active";
                    break;
                case Keire::Internal::GraphicsCaptureRequestResult::Queued:
                    m_RenderGraphStatus = "RenderDoc capture queued for the next submitted GPU frame";
                    break;
                }
            }
        }

        if (ui.Button("Export JSON"))
        {
            try
            {
                const auto root = Owner().GetProject() ? Owner().GetProject()->Root() : std::filesystem::path(".");
                Keire::ExportFrameGraphJson(snapshot, root / "Library" / "Diagnostics" / "render-graph.json");
                m_RenderGraphStatus = "Exported Library/Diagnostics/render-graph.json";
            }
            catch (const std::exception& error)
            {
                ReportError("Render Graph", error.what());
            }
        }
        ui.SameLine();
        if (ui.Button("Export DOT"))
        {
            try
            {
                const auto root = Owner().GetProject() ? Owner().GetProject()->Root() : std::filesystem::path(".");
                Keire::ExportFrameGraphDot(snapshot, root / "Library" / "Diagnostics" / "render-graph.dot");
                m_RenderGraphStatus = "Exported Library/Diagnostics/render-graph.dot";
            }
            catch (const std::exception& error)
            {
                ReportError("Render Graph", error.what());
            }
        }

        if (!m_RenderGraphStatus.empty())
            ui.Text(m_RenderGraphStatus);

        ui.Separator();
        ui.Text("DETERMINISTIC PASS ORDER");
        for (const auto& pass : snapshot.Passes)
        {
            ui.Text(std::to_string(pass.Order) + ". " + pass.Name +
                    "  [transitions: " + std::to_string(pass.Transitions.size()) + "]");
        }

        ui.Separator();
        ui.Text("RESOURCE LIFETIMES AND ALIAS SLOTS");
        const auto passCount = std::max<std::size_t>(1, snapshot.Passes.size());
        for (const auto& resource : snapshot.Resources)
        {
            const auto label =
                resource.Name +
                (resource.Imported ? " [imported]"
                                   : " [transient slot " + std::to_string(resource.PhysicalAliasSlot) + "]") +
                "  " + std::to_string(resource.EstimatedBytes) + " bytes";
            ui.Text(label);
            const auto span = resource.Used ? resource.LastPass - resource.FirstPass + 1U : 0U;
            const auto fraction = static_cast<float>(span) / static_cast<float>(passCount);
            const auto interval = resource.Used
                                      ? std::to_string(resource.FirstPass) + ".." + std::to_string(resource.LastPass)
                                      : std::string("unused");
            ui.ProgressBar(fraction, {0.0F, 12.0F}, interval);
        }
    }
}
