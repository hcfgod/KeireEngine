#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/EditorRuntimeUiInput.h"
#include "KeireClient/Editor/EditorSmokePlayValidation.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/SceneViewportLayout.h"
#include "KeireClient/Editor/ViewportInputRouting.h"
#include "KeireInternal/Rendering/DynamicResolutionInternal.h"
#include "KeireInternal/Scenes/SceneRuntimeRenderingInternal.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    struct SceneCamera final
    {
        Keire::Entity Entity;
        Keire::Ref<Keire::CameraComponent> Camera;
        Keire::Ref<Keire::TransformComponent> Transform;
    };

    [[nodiscard]] std::optional<SceneCamera> SelectGameCamera(const Keire::Ref<Keire::Scene>& scene)
    {
        if (!scene || !scene->IsOpen())
        {
            return std::nullopt;
        }
        std::optional<SceneCamera> selected;
        bool selectedPrimary = false;
        for (const auto& entity : scene->Query<Keire::CameraComponent>())
        {
            const auto camera = entity.GetComponent<Keire::CameraComponent>();
            const auto transform = entity.GetComponent<Keire::TransformComponent>();
            if (!camera || !transform || !camera->Enabled() || !entity.ActiveInHierarchy())
            {
                continue;
            }
            if (!selected || (camera->Primary() && !selectedPrimary) ||
                (camera->Primary() == selectedPrimary && (camera->Priority() > selected->Camera->Priority() ||
                                                          (camera->Priority() == selected->Camera->Priority() &&
                                                           entity.Id().Value() < selected->Entity.Id().Value()))))
            {
                selected = SceneCamera{entity, camera, transform};
                selectedPrimary = camera->Primary();
            }
        }
        return selected;
    }

    [[nodiscard]] Keire::UiSize PrepareRenderSurface(const Keire::Ref<Keire::RenderView>& view,
                                                     const Keire::UiSize logicalSize, const float displayScale,
                                                     const float renderScale)
    {
        if (!view || !view->Surface())
        {
            return {};
        }
        const float width = std::max(logicalSize.Width, 1.0F);
        const float height = std::max(logicalSize.Height, 1.0F);
        const auto [pixelWidth, pixelHeight] =
            Keire::Internal::ScaledRenderSurfaceExtent(width, height, displayScale, renderScale);
        view->Surface()->RequestSize(pixelWidth, pixelHeight);
        return {width, height};
    }

} // namespace

void EditorWorkspaceLayer::DrawGame(Keire::UiFrame& ui)
{
    m_GameViewportRect = {};
    const auto inputSession = m_SceneDocument->PlaySession();
    const auto window = Owner().MainWindow();
    // The unattended smoke fixture keeps input active even when the OS refuses foreground focus. Apply that
    // policy here as well as after drawing the viewport, so a transient false state cannot cancel a held click.
    SetGameViewportInputActive(KeireEditor::GameViewportOwnsRuntimeInput(
                                   inputSession && inputSession->State() == Keire::ScenePlayState::Playing,
                                   window && window->Focused(), false, false, m_GameViewportCaptureSuspended,
                                   m_PlayChanges != nullptr) ||
                               static_cast<bool>(m_SmokePlayValidation));
    if (auto gamePanel = ui.BeginPanel(m_Game); gamePanel)
    {
        constexpr std::array aspectLabels{std::string_view("Free"), std::string_view("16:9"), std::string_view("16:10"),
                                          std::string_view("4:3")};
        // Keep the selector compact instead of consuming the dock's default two-thirds item width.
        const float selectorWidth = ui.MeasureText("16:10").Width + ui.MeasureText("MMMM").Width;
        const float labelWidth = ui.MeasureText("Preview M").Width;
        ui.SetNextItemWidth(std::clamp(ui.ContentAvailable().Width - labelWidth, 1.0F, selectorWidth));
        if (auto aspect = ui.BeginCombo("Preview", aspectLabels[static_cast<std::size_t>(m_GameAspect)]); aspect)
        {
            for (std::size_t index = 0; index < aspectLabels.size(); ++index)
            {
                if (ui.Selectable(aspectLabels[index], m_GameAspect == static_cast<int>(index)))
                {
                    m_GameAspect = static_cast<int>(index);
                }
            }
        }
        if (ui.Checkbox("Maximize On Play", m_MaximizeGameOnPlay, {.ContinueRow = true}))
        {
            PersistEditorSessionPreferences();
        }
        (void)ui.Checkbox("FPS", m_ShowPerformanceOverlay, {.ContinueRow = true});
        if (m_GameRenderView && m_GameRenderView->Surface())
        {
            const auto surface = m_GameRenderView->Surface();
            bool visibilityBounds = surface->OcclusionDebugView() == Keire::GpuOcclusionDebugView::VisibilityBounds;
            if (ui.Checkbox("GPU Bounds", visibilityBounds, {.ContinueRow = true}))
            {
                surface->SetOcclusionDebugView(visibilityBounds ? Keire::GpuOcclusionDebugView::VisibilityBounds
                                                                : Keire::GpuOcclusionDebugView::None,
                                               0U);
            }
            ui.SetTooltip("Game-camera GPU bounds: green survived conservative visibility tests; red was safely "
                          "occlusion-rejected. CPU-frustum-rejected objects are not submitted or outlined.",
                          {.Delayed = true});
        }
        const auto playSession = m_SceneDocument->PlaySession();
        const bool playActive = playSession && playSession->State() != Keire::ScenePlayState::Stopped;
        const auto runtimeSelection = playActive ? Keire::Internal::SelectRuntimeRenderSession(m_PlayRuntimeWorld)
                                                 : Keire::Internal::RuntimeRenderSessionSelection{};
        const auto editingScene = m_SceneDocument->EditingScene();
        const auto scene =
            playActive
                ? (runtimeSelection.Session ? runtimeSelection.Session->RuntimeScene() : Keire::Ref<Keire::Scene>{})
                : (editingScene && editingScene->IsOpen() ? editingScene : Keire::Ref<Keire::Scene>{});
        if (!scene)
        {
            SetGameViewportInputActive(false);
            KeireEditor::CancelRuntimeUiPointer(m_PlayRuntimeWorld, m_GameRuntimeUiPointer);
            DrawEmptyState(ui, "GAME", "No scene is loaded.", "Open a scene to preview its active primary camera.");
            return;
        }
        if (!m_GameRenderView)
        {
            SetGameViewportInputActive(false);
            KeireEditor::CancelRuntimeUiPointer(m_PlayRuntimeWorld, m_GameRuntimeUiPointer);
            DrawEmptyState(ui, "GAME", "The renderer is disabled.",
                           "Enable rendered or headless rendering in the application specification.");
            return;
        }

        std::optional<SceneCamera> selected;
        if (runtimeSelection.Camera)
        {
            selected = SceneCamera{runtimeSelection.Camera->Entity, runtimeSelection.Camera->Camera,
                                   runtimeSelection.Camera->Transform};
        }
        else if (!playActive)
        {
            selected = SelectGameCamera(scene);
        }
        if (!selected && !playActive)
        {
            SetGameViewportInputActive(false);
            KeireEditor::CancelRuntimeUiPointer(m_PlayRuntimeWorld, m_GameRuntimeUiPointer);
            DrawEmptyState(ui, "GAME", "No active camera.",
                           "Add or enable a Camera on an active entity. Use Primary to choose between cameras.");
            return;
        }

        ui.TextColoredWrapped(m_Theme.MutedText, selected ? "Game camera: " + selected->Entity.Name() +
                                                                " | GPU visibility is camera-local" +
                                                                (playActive ? " (Play)" : " (Edit)")
                                                          : "Game camera: none | clear and runtime UI only (Play)");

        const auto available = ui.ContentAvailable();
        constexpr std::array aspectRatios{0.0F, 16.0F / 9.0F, 16.0F / 10.0F, 4.0F / 3.0F};
        const float requestedAspect = aspectRatios[static_cast<std::size_t>(m_GameAspect)];
        const auto drawableSize = KeireEditor::CalculateGameViewportPreviewSize(available, requestedAspect);
        if (!drawableSize)
        {
            SetGameViewportInputActive(false);
            KeireEditor::CancelRuntimeUiPointer(m_PlayRuntimeWorld, m_GameRuntimeUiPointer);
            m_GameLogicalViewportSize = {};
            m_GameViewportRect = {};
            return;
        }
        const auto previewSize = *drawableSize;
        const float aspect = previewSize.Width / std::max(previewSize.Height, 1.0F);
        Keire::RenderCamera camera;
        if (selected)
        {
            camera = Keire::Internal::BuildPresentedSceneCamera(*selected->Camera, *selected->Transform, aspect);
        }
        m_GameRenderView->SetCamera(camera);

        std::vector<Keire::Ref<Keire::ScenePresentationRuntime>> presentations;
        if (playActive)
        {
            if (m_PlayRuntimeWorld)
            {
                m_PlayRuntimeWorld->SetPresentationViewport(previewSize.Width, previewSize.Height);
                for (const auto& session : m_PlayRuntimeWorld->Sessions())
                {
                    if (const auto presentation =
                            session ? session->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{})
                    {
                        presentations.push_back(presentation);
                    }
                }
            }
        }
        else
        {
            if (!m_GameEditPresentation)
            {
                if (const auto assets = SceneViewportAssetSystem())
                {
                    m_GameEditPresentation =
                        Keire::CreateRef<Keire::ScenePresentationRuntime>(assets, Keire::Ref<Keire::AudioSystem>{});
                }
            }
            if (m_GameEditPresentation)
            {
                m_GameEditPresentation->Synchronize(scene, previewSize.Width, previewSize.Height, false);
                m_GameEditPresentation->AdvanceUi(
                    static_cast<float>(std::max(Owner().GetTime().UnscaledDeltaTime().Seconds(), 0.0)));
                presentations.push_back(m_GameEditPresentation);
            }
        }
        std::vector<Keire::RuntimeUiRenderSubmission> uiSubmissions;
        if (KeireEditor::CompositesRuntimeGameUi(KeireEditor::EditorViewportTarget::Game))
        {
            for (const auto& presentation : presentations)
            {
                auto additions = presentation->UiRenderSubmissions(m_GameRenderView);
                uiSubmissions.insert(uiSubmissions.end(), std::make_move_iterator(additions.begin()),
                                     std::make_move_iterator(additions.end()));
            }
        }
        auto environment = SceneViewportSettings();
        environment.SkyVisible =
            selected && environment.SkyVisible && selected->Camera->ClearMode() == Keire::CameraClearMode::Skybox;
        const auto featureSelection =
            Keire::ResolveRenderFeatureSelection(environment, Owner().Renderer()->FeatureCapabilities());
        const float effectiveRenderScale = Keire::Internal::NativePixelScaleForRuntimeUi(
            m_GameDynamicResolution.Update(environment, featureSelection, Owner().Renderer()->Statistics()),
            uiSubmissions, true);
        const auto size = PrepareRenderSurface(m_GameRenderView, previewSize, Owner().MainWindow()->DisplayScale(),
                                               effectiveRenderScale);
        m_GameLogicalViewportSize = size;
        m_GameRenderView->Surface()->RequestSampleCount(Keire::ResolveRenderSurfaceSampleCount(featureSelection));
        const auto& materialTime = Owner().GetTime();
        std::optional<Keire::SceneRenderRequest> renderRequest =
            playActive ? Keire::Internal::CaptureRuntimeSceneRenderRequest(
                             m_PlayRuntimeWorld, runtimeSelection.Session, m_GameRenderView, environment,
                             m_ManagedMaterialParameters.Snapshot(), selected.has_value())
                       : std::optional<Keire::SceneRenderRequest>{
                             Keire::SceneRenderRequest{scene, m_GameRenderView, false, environment}};
        if (renderRequest)
        {
            if (!playActive)
            {
                renderRequest->EvaluatedEnvironment = SceneViewportEnvironmentLighting();
            }
            renderRequest->MaterialTimeSeconds = static_cast<float>(materialTime.TimeSinceStartup().Seconds());
            renderRequest->PlanetEnvironment = SceneViewportPlanetEnvironment(camera);
            renderRequest->MaterialDeltaSeconds = static_cast<float>(materialTime.DeltaTime().Seconds());
            renderRequest->FrameIndex = materialTime.FrameCount();
            Owner().Renderer()->Submit(std::move(*renderRequest));
        }
        ui.SetCursorScreenPosition(KeireEditor::CenterGameViewportPreview(ui.CursorScreenPosition(), available, size));
        ui.Image(m_GameRenderView->Surface(), size);
        const auto imageState = ui.LastItemState();
        const auto imageRect = ui.LastItemRect();
        m_GameViewportRect = imageRect;

        if (playActive && m_GameViewportCaptureSuspended && imageState.Hovered && ui.PointerState().LeftPressed &&
            !ui.KeyDown(Keire::UiKey::Escape))
        {
            m_GameViewportCaptureSuspended = false;
        }
        const auto mainWindow = Owner().MainWindow();
        const bool productionInputActive = KeireEditor::GameViewportOwnsRuntimeInput(
            playSession && playSession->State() == Keire::ScenePlayState::Playing, mainWindow && mainWindow->Focused(),
            ui.WindowFocused(), m_ManagedCursorLocked, m_GameViewportCaptureSuspended, m_PlayChanges != nullptr);
        // The window manager may refuse to focus a process launched by an unattended validation harness. Keep the
        // production focus contract intact while allowing --smoke-play to exercise the real SDL/ImGui pointer path in
        // packaged builds, where renderer fault-injection hooks remain disabled.
        SetGameViewportInputActive(productionInputActive || static_cast<bool>(m_SmokePlayValidation));
        for (auto& submission : uiSubmissions)
        {
            if (submission.Target == Keire::RuntimeUiRenderTarget::ScreenOverlay)
            {
                submission.Target = Keire::RuntimeUiRenderTarget::CameraOverlay;
            }
            Owner().Renderer()->SubmitRuntimeUiTarget(std::move(submission));
        }
        const auto runtimeUiRouting =
            KeireEditor::ResolveRuntimeGameUiRouting(playActive, !presentations.empty(), m_GameViewportInputActive);
        const bool uiBuilderPickIntercepted = TryPickUiBuilderLiveElement(presentations, imageRect, ui.PointerState());
        if (runtimeUiRouting.Pointer)
        {
            if (!uiBuilderPickIntercepted)
            {
                KeireEditor::RouteRuntimeUiPointer(ui, presentations, imageRect, m_GameRuntimeUiPointer);
            }
            if (runtimeUiRouting.Keyboard)
            {
                KeireEditor::RouteRuntimeUiKeyboard(ui,
                                                    KeireEditor::SelectRuntimeUiKeyboardPresentation(
                                                        Keire::Internal::ActiveRuntimePresentation(m_PlayRuntimeWorld)),
                                                    Owner().Windows(), mainWindow);
            }
        }
        else if (playActive)
        {
            KeireEditor::CancelRuntimeUiPointer(m_PlayRuntimeWorld, m_GameRuntimeUiPointer);
        }
        if (m_SmokePlayValidation)
        {
            m_SmokePlayValidation->ObserveGameView(
                Owner(), m_PlayRuntimeWorld, m_GameRenderView->Surface(), imageRect, presentations, ui.PointerState(),
                m_GameRuntimeUiPointer.HoveredPresentation, m_GameRuntimeUiPointer.PointerCaptures.front(),
                m_GameViewportInputActive, ui.WindowFocused());
        }
        const auto occlusion = m_GameRenderView && m_GameRenderView->Surface()
                                   ? std::optional(m_GameRenderView->Surface()->OcclusionDiagnostics())
                                   : std::nullopt;
        (void)DrawPerformanceOverlay(ui, imageRect, "GAME CAMERA", occlusion);
        return;
    }
    KeireEditor::CancelRuntimeUiPointer(m_PlayRuntimeWorld, m_GameRuntimeUiPointer);
}
