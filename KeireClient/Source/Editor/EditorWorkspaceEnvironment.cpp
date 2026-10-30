#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/EditorPanels.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClientInternal/Editor/EnvironmentPreviewClock.h"
#include "KeireInternal/Environment/SceneRuntimeEnvironmentAccess.h"
#include "KeireInternal/Rendering/PlanetEnvironmentCapture.h"
#include "KeireInternal/Scenes/SceneRuntimeRenderingInternal.h"
#include "KeireInternal/Spatial/PlanetProjectFrameResolver.h"

#include <exception>
#include <string>

std::optional<Keire::PlanetEnvironmentView>
EditorWorkspaceLayer::SceneViewportPlanetEnvironment(const Keire::RenderCamera& camera)
{
    if (!m_ProjectSettingsDocument->Opened())
    {
        return std::nullopt;
    }
    if (m_PlayRuntimeWorld && m_SceneDocument->PlaySession())
    {
        return Keire::Internal::CaptureRuntimePlanetEnvironment(*m_PlayRuntimeWorld, camera);
    }
    if (!m_PlanetPreviewFrames)
    {
        m_PlanetPreviewFrames = std::make_unique<Keire::Detail::PlanetProjectFrameResolver>();
    }
    m_PlanetPreviewFrames->Configure(m_ProjectSettingsDocument->Planets());
    const auto localObserver = Keire::Math::TransformPoint(Keire::Math::Inverse(camera.View), {});
    const Keire::Detail::SectorPosition origin({1, 1}, {},
                                               {camera.WorldOrigin.X, camera.WorldOrigin.Y, camera.WorldOrigin.Z});
    const auto observer = origin.Translated({localObserver.X, localObserver.Y, localObserver.Z});
    m_EnvironmentPreview->SynchronizeAuthored(m_ProjectSettingsDocument->EnvironmentTime());
    const auto clock = m_EnvironmentPreview->State();
    const auto frame = m_PlanetPreviewFrames->Resolve(observer, m_EnvironmentPreview->SimulationTime(), clock,
                                                      m_EnvironmentPreview->Revision());
    return frame ? std::optional{Keire::Detail::ToPlanetEnvironmentView(*frame)} : std::nullopt;
}

std::vector<Keire::PlanetEnvironmentView>
EditorWorkspaceLayer::SceneViewportPlanetAtmospheres(const Keire::RenderCamera& camera)
{
    if (!m_ProjectSettingsDocument->Opened())
    {
        return {};
    }
    if (m_PlayRuntimeWorld && m_SceneDocument->PlaySession())
    {
        return Keire::Internal::CaptureRuntimePlanetAtmospheres(*m_PlayRuntimeWorld, camera);
    }
    if (!m_PlanetPreviewFrames)
    {
        m_PlanetPreviewFrames = std::make_unique<Keire::Detail::PlanetProjectFrameResolver>();
    }
    m_PlanetPreviewFrames->Configure(m_ProjectSettingsDocument->Planets());
    m_EnvironmentPreview->SynchronizeAuthored(m_ProjectSettingsDocument->EnvironmentTime());
    const auto localObserver = Keire::Math::TransformPoint(Keire::Math::Inverse(camera.View), {});
    const Keire::Detail::SectorPosition origin({1, 1}, {},
                                               {camera.WorldOrigin.X, camera.WorldOrigin.Y, camera.WorldOrigin.Z});
    const auto observer = origin.Translated({localObserver.X, localObserver.Y, localObserver.Z});
    const auto frames =
        m_PlanetPreviewFrames->ResolveAll(observer, m_EnvironmentPreview->SimulationTime(),
                                          m_EnvironmentPreview->State(), m_EnvironmentPreview->Revision());
    std::vector<Keire::PlanetEnvironmentView> result;
    result.reserve(frames.size());
    for (const auto& frame : frames)
    {
        result.push_back(Keire::Detail::ToPlanetEnvironmentView(frame));
    }
    return result;
}

std::optional<Keire::EnvironmentLightingSnapshot> EditorWorkspaceLayer::SceneViewportEnvironmentLighting()
{
    if (m_PlayRuntimeWorld && m_SceneDocument->PlaySession())
    {
        return Keire::Detail::SceneRuntimeEnvironmentAccess::CaptureLighting(*m_PlayRuntimeWorld);
    }
    if (!m_ProjectSettingsDocument->Opened())
    {
        return std::nullopt;
    }
    m_EnvironmentPreview->SynchronizeAuthored(m_ProjectSettingsDocument->EnvironmentTime());
    auto source = m_EnvironmentPreview->LightingSource();
    if (!source)
    {
        source = Keire::AssetId::Generate();
    }
    return m_EnvironmentPreview->CaptureLighting(m_ProjectSettingsDocument->EnvironmentLighting(), source);
}

KeireEditor::EnvironmentPreviewClock* EditorWorkspaceLayer::ProjectEnvironmentPreview()
{
    if (!m_ProjectSettingsDocument->Opened() || m_SceneDocument->PlaySession())
    {
        return nullptr;
    }
    m_EnvironmentPreview->SynchronizeAuthored(m_ProjectSettingsDocument->EnvironmentTime());
    return m_EnvironmentPreview.get();
}

void EditorWorkspaceLayer::UpdateEnvironmentPreview(const double elapsedSeconds)
{
    if (!m_ProjectSettingsDocument->Opened())
    {
        m_EnvironmentPreview->Unbind();
        return;
    }
    try
    {
        m_EnvironmentPreview->SynchronizeAuthored(m_ProjectSettingsDocument->EnvironmentTime());
        const bool visible = m_ProjectSettingsPanel->Registration().Visible() ||
                             m_ProjectSettingsPanel->EnvironmentRegistration().Visible() ||
                             m_SceneViewportPanel->Registration().Visible() || m_Game.Visible();
        m_EnvironmentPreview->Tick(elapsedSeconds, !m_SceneDocument->PlaySession(), visible);
    }
    catch (const std::exception& error)
    {
        if (m_EnvironmentPreview->Bound())
        {
            m_EnvironmentPreview->SetPlaying(false);
        }
        ReportError("Environment preview", std::string("Preview stopped: ") + error.what());
    }
}
