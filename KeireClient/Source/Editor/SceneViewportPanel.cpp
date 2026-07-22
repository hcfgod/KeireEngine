#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/SceneCameraController.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/SceneGizmoController.h"
#include "KeireClient/Editor/ScenePicker.h"
#include "KeireInternal/EditorCameraController.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
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
        if (!scene)
            return std::nullopt;
        std::optional<SceneCamera> selected;
        bool primary = false;
        for (const auto& entity : scene->Query<Keire::CameraComponent>())
        {
            const auto camera = entity.GetComponent<Keire::CameraComponent>();
            const auto transform = entity.GetComponent<Keire::TransformComponent>();
            if (!camera || !transform || !camera->Enabled() || !entity.ActiveInHierarchy())
                continue;
            if (!selected || (camera->Primary() && !primary) ||
                (camera->Primary() == primary && (camera->Priority() > selected->Camera->Priority() ||
                                                  (camera->Priority() == selected->Camera->Priority() &&
                                                   entity.Id().Value() < selected->Entity.Id().Value()))))
            {
                selected = SceneCamera{entity, camera, transform};
                primary = camera->Primary();
            }
        }
        return selected;
    }

    [[nodiscard]] Keire::UiSize PrepareRenderSurface(const Keire::Ref<Keire::RenderView>& view,
                                                     const Keire::UiSize logicalSize, const float displayScale)
    {
        if (!view || !view->Surface())
            return {};
        const float width = std::max(logicalSize.Width, 1.0F);
        const float height = std::max(logicalSize.Height, 1.0F);
        const auto pixelWidth =
            static_cast<std::uint32_t>(std::round(std::clamp(width * std::max(displayScale, 1.0F), 1.0F, 16384.0F)));
        const auto pixelHeight =
            static_cast<std::uint32_t>(std::round(std::clamp(height * std::max(displayScale, 1.0F), 1.0F, 16384.0F)));
        view->Surface()->RequestSize(pixelWidth, pixelHeight);
        return {width, height};
    }

    void DrawEmptyState(Keire::UiFrame& ui, const std::string_view heading, const std::string_view primary,
                        const std::string_view detail)
    {
        ui.TextColored({0.20F, 0.55F, 1.0F, 1.0F}, heading);
        ui.Separator();
        ui.Text(primary);
        ui.Text(detail);
    }
} // namespace

KeireEditor::SceneViewportPanel::SceneViewportPanel(ISceneViewportController& controller)
    : m_Controller(controller), m_Gizmos(std::make_unique<SceneGizmoController>()),
      m_Camera(std::make_unique<SceneCameraController>())
{
}

KeireEditor::SceneViewportPanel::~SceneViewportPanel() = default;

void KeireEditor::SceneViewportPanel::Initialize(const std::filesystem::path& projectRoot)
{
    m_ProjectRoot = projectRoot;
    if (const auto renderer = m_Controller.SceneViewportRenderer();
        renderer && renderer->Mode() != Keire::RenderMode::Disabled)
    {
        Keire::RenderSurfaceSpecification surface;
        surface.Name = "Scene View";
        surface.ClearColor = {0.075F, 0.085F, 0.105F, 1.0F};
        m_RenderView = renderer->CreateView(surface);
    }
    if (!projectRoot.empty())
    {
        m_Gizmos->Load(projectRoot);
        (void)m_Camera->Load(projectRoot / "Library/Editor/SceneCamera.state");
    }
}

void KeireEditor::SceneViewportPanel::Shutdown(const std::filesystem::path& projectRoot) noexcept
{
    if (!projectRoot.empty())
    {
        m_Gizmos->Save(projectRoot);
        (void)m_Camera->Save(projectRoot / "Library/Editor/SceneCamera.state");
    }
    if (m_Camera->Capturing())
    {
        try
        {
            m_Controller.SceneViewportWindows()->SetCursorMode(m_Controller.SceneViewportWindow(),
                                                               Keire::CursorMode::Normal);
        }
        catch (...)
        {
        }
        m_Camera->SetCapturing(false);
    }
    m_RenderView.Reset();
}
void KeireEditor::SceneViewportPanel::Draw(Keire::UiFrame& ui)
{
    auto panel = ui.BeginPanel(m_Registration);
    if (!panel)
        return;
    auto& document = m_Controller.SceneViewportDocument();
    const auto& theme = m_Controller.SceneViewportTheme();
    const auto database = m_Controller.SceneViewportAssetDatabase();
    const auto assetSystem = m_Controller.SceneViewportAssetSystem();
    const auto renderer = m_Controller.SceneViewportRenderer();
    const auto activeScene = document.ActiveScene();
    if (ui.WindowFocused())
        m_Controller.ActivateSceneViewportHistory();
    if (!document.EditingScene())
    {
        DrawEmptyState(ui, "SCENE", "No scene is loaded.",
                       "Create or double-click a .keirescene asset in the Project panel.");
        return;
    }
    if (document.RecoveryAvailable())
    {
        ui.TextColored(theme.Warning, "A recovery snapshot is available for this scene.");
        if (ui.Button("Restore Recovery"))
        {
            try
            {
                m_Controller.RestoreSceneViewportRecovery();
            }
            catch (const std::exception& error)
            {
                document.SetStatus(std::string("Scene recovery failed: ") + error.what());
                m_Controller.ReportSceneViewportError(document.Status());
            }
        }
        ui.SameLine();
        if (ui.Button("Discard Recovery"))
            m_Controller.DiscardSceneViewportRecovery();
    }
    if (!m_RenderView)
    {
        DrawEmptyState(ui, "SCENE", "The renderer is disabled.",
                       "Enable rendered or headless rendering in the application specification.");
        return;
    }

    const auto available = ui.ContentAvailable();
    const auto size = PrepareRenderSurface(m_RenderView, available, m_Controller.SceneViewportDisplayScale());
    const float aspect = size.Width / std::max(size.Height, 1.0F);
    Keire::RenderCamera camera;
    camera.View = m_Camera->ViewMatrix();
    camera.Projection = m_Camera->ProjectionMatrix(aspect);
    const auto renderScene = activeScene;
    if (const auto sceneCamera = SelectGameCamera(renderScene))
        camera.ClearColor = sceneCamera->Camera->ClearColor();
    else
        camera.ClearColor = {0.075F, 0.085F, 0.105F, 1.0F};
    m_RenderView->SetCamera(camera);

    if (renderScene)
        renderer->Submit({renderScene, m_RenderView, true, m_Controller.SceneViewportSettings()});
    ui.Image(m_RenderView->Surface(), size);
    const auto imageState = ui.LastItemState();
    const auto imageRect = ui.LastItemRect();
    m_ViewportRect = imageRect;
    m_LastCamera = camera;
    if (auto target = ui.BeginDragTarget(); target)
    {
        std::vector<std::byte> payload;
        if (ui.AcceptDragPayload("KEIRE_ASSETS", payload))
        {
            try
            {
                const auto assets = KeireEditor::AssetBrowserPanel::DecodeDragPayload(payload);
                for (const auto asset : assets)
                {
                    const auto record = database ? database->Find(asset) : std::nullopt;
                    if (!record)
                        throw std::runtime_error("A dropped asset no longer exists in the project database.");
                    if (record->Type == Keire::Texture2DAsset::StaticType() ||
                        record->Type == Keire::ShaderAsset::StaticType())
                    {
                        m_Controller.SetSceneViewportSelectedAsset(asset);
                        continue;
                    }
                    const auto hit =
                        KeireEditor::PickSceneEntity(activeScene, imageRect, ui.PointerState().Position, camera);
                    m_Controller.RouteSceneViewportAsset(record->Type, asset, hit);
                }
            }
            catch (const std::exception& error)
            {
                document.SetStatus(std::string("Scene asset drop failed: ") + error.what());
                m_Controller.ReportSceneViewportError(document.Status());
            }
        }
    }
    const auto toolbarRect = m_Gizmos->DrawOverlayToolbar(ui, imageRect);
    constexpr float overlaySize = 28.0F;
    constexpr float overlayGap = 3.0F;
    constexpr float overlayPadding = 8.0F;
    Keire::UiPosition orientationPosition{imageRect.Maximum.X - overlayPadding - overlaySize * 4.0F - overlayGap * 3.0F,
                                          imageRect.Minimum.Y + overlayPadding};
    const Keire::UiItemRect orientationRect{
        orientationPosition, {imageRect.Maximum.X - overlayPadding, orientationPosition.Y + overlaySize}};
    const auto orientationButton =
        [&](const std::string_view id, const Keire::UiIcon icon, const std::string_view tooltip)
    {
        const bool activated = ui.OverlayIconButton(
            id, icon, {.Position = orientationPosition, .Size = {overlaySize, overlaySize}, .Tooltip = tooltip});
        orientationPosition.X += overlaySize + overlayGap;
        return activated;
    };
    if (orientationButton("SceneProjection",
                          m_Camera->State().Projection == Keire::Detail::EditorCameraProjection::Perspective
                              ? Keire::UiIcon::Perspective
                              : Keire::UiIcon::Orthographic,
                          "Toggle perspective/orthographic projection"))
    {
        m_Camera->ToggleProjection();
        m_Camera->MarkDirty();
    }
    if (orientationButton("SceneAxisX", Keire::UiIcon::AxisX, "Look along the X axis"))
    {
        m_Camera->Snap(Keire::Detail::EditorCameraAxis::PositiveX);
        m_Camera->MarkDirty();
    }
    if (orientationButton("SceneAxisY", Keire::UiIcon::AxisY, "Look along the Y axis"))
    {
        m_Camera->Snap(Keire::Detail::EditorCameraAxis::PositiveY);
        m_Camera->MarkDirty();
    }
    if (orientationButton("SceneAxisZ", Keire::UiIcon::AxisZ, "Look along the Z axis"))
    {
        m_Camera->Snap(Keire::Detail::EditorCameraAxis::PositiveZ);
        m_Camera->MarkDirty();
    }
    const std::string viewportStatus = std::to_string(activeScene->ObjectCount()) + " objects  |  " +
                                       (document.PlaySession() ? "Play" : "Edit") +
                                       (document.EditingScene()->Dirty() ? "  |  Unsaved" : "");
    const Keire::UiPosition statusPosition{imageRect.Minimum.X + 12.0F, imageRect.Maximum.Y - 24.0F};
    ui.DrawFilledRectangle(
        {{statusPosition.X - 5.0F, statusPosition.Y - 3.0F},
         {statusPosition.X + static_cast<float>(viewportStatus.size()) * 7.0F + 5.0F, statusPosition.Y + 18.0F}},
        {0.03F, 0.04F, 0.06F, 0.72F}, 4.0F);
    ui.DrawOverlayText(statusPosition, theme.MutedText, viewportStatus);
    const bool pointerBlocked =
        toolbarRect.Contains(ui.PointerState().Position) || orientationRect.Contains(ui.PointerState().Position);
    if (renderScene)
    {
        const bool allowManipulation = !m_Controller.SceneViewportPlayReviewActive();
        const auto resolveMeshBounds = [assetSystem](const Keire::AssetId mesh) -> std::optional<Keire::MeshBounds>
        {
            const auto assets = assetSystem;
            if (!assets)
                return std::nullopt;
            const auto metadata = assets->TryGetMetadata(mesh);
            if (!metadata || !metadata->LocalBounds)
                return std::nullopt;
            const auto& bounds = *metadata->LocalBounds;
            return Keire::MeshBounds{{bounds.Minimum[0], bounds.Minimum[1], bounds.Minimum[2]},
                                     {bounds.Maximum[0], bounds.Maximum[1], bounds.Maximum[2]}};
        };
        const auto pointer = ui.PointerState();
        std::vector<Keire::AssetId> selectionBeforePointer;
        if (imageState.Hovered && pointer.LeftPressed)
        {
            const auto selected = document.Selections();
            selectionBeforePointer.assign(selected.begin(), selected.end());
        }
        const auto selections = document.Selections();
        const auto gizmo = m_Gizmos->UpdateAndDraw(
            ui, renderScene, Keire::EntityId(document.Selection()), camera, imageRect, allowManipulation,
            pointerBlocked, [this](const std::string_view name) { m_Controller.RecordSceneViewportUndo(name); },
            resolveMeshBounds, selections);
        if (gizmo.SelectionActivated)
            m_Controller.SelectSceneViewportEntity(gizmo.Selection.Value(), ui.ControlDown());
        if (imageState.Hovered && !pointerBlocked && pointer.LeftPressed)
        {
            if (gizmo.PointerConsumed)
            {
                m_BoxSelecting = false;
                m_BoxSelectionBase.clear();
            }
            else
            {
                m_BoxSelecting = true;
                m_BoxSelectionStart = pointer.Position;
                m_BoxSelectionAdditive = ui.ControlDown();
                m_BoxSelectionBase = std::move(selectionBeforePointer);
            }
        }

        if (m_BoxSelecting)
        {
            const float deltaX = pointer.Position.X - m_BoxSelectionStart.X;
            const float deltaY = pointer.Position.Y - m_BoxSelectionStart.Y;
            const bool marquee = deltaX * deltaX + deltaY * deltaY >= 16.0F;
            Keire::UiItemRect selection{{std::min(m_BoxSelectionStart.X, pointer.Position.X),
                                         std::min(m_BoxSelectionStart.Y, pointer.Position.Y)},
                                        {std::max(m_BoxSelectionStart.X, pointer.Position.X),
                                         std::max(m_BoxSelectionStart.Y, pointer.Position.Y)}};
            if (pointer.LeftDown && marquee)
            {
                ui.DrawFilledRectangle(selection, {0.20F, 0.55F, 1.0F, 0.12F});
                ui.DrawRectangle(selection, {0.30F, 0.68F, 1.0F, 0.95F}, 1.0F);
            }
            if (pointer.LeftReleased)
            {
                if (marquee)
                {
                    if (m_BoxSelectionAdditive)
                        document.SetSelections(m_BoxSelectionBase);
                    const auto entities = KeireEditor::SelectSceneEntitiesInRectangle(renderScene, imageRect, selection,
                                                                                      camera, resolveMeshBounds);
                    m_Controller.SetSceneViewportSelection(entities, m_BoxSelectionAdditive);
                }
                m_BoxSelecting = false;
                m_BoxSelectionBase.clear();
            }
        }
    }
    UpdateCamera(ui, imageState);
}

void KeireEditor::SceneViewportPanel::UpdateCamera(Keire::UiFrame& ui, const Keire::UiItemState& imageState)
{
    const auto scene = m_Controller.SceneViewportDocument().ActiveScene();
    const auto assets = m_Controller.SceneViewportAssetSystem();
    const auto resolveMeshBounds = [assets](const Keire::AssetId mesh) -> std::optional<Keire::MeshBounds>
    {
        if (!assets)
            return std::nullopt;
        const auto metadata = assets->TryGetMetadata(mesh);
        if (!metadata || !metadata->LocalBounds)
            return std::nullopt;
        const auto& bounds = *metadata->LocalBounds;
        return Keire::MeshBounds{{bounds.Minimum[0], bounds.Minimum[1], bounds.Minimum[2]},
                                 {bounds.Maximum[0], bounds.Maximum[1], bounds.Maximum[2]}};
    };
    const auto pointer = ui.PointerState();
    const bool navigationRegion = imageState.Hovered || m_Camera->Capturing();
    bool changed = false;

    std::vector<Keire::EntityId> selectedIds;
    std::vector<Keire::Entity> selectedEntities;
    if (scene)
    {
        for (const auto selection : m_Controller.SceneViewportDocument().Selections())
        {
            const auto entity = scene->FindEntity(Keire::EntityId(selection));
            if (entity && entity.ActiveInHierarchy())
            {
                selectedIds.push_back(entity.Id());
                selectedEntities.push_back(entity);
            }
        }
    }
    if (!m_Camera->LockedEntities().empty())
    {
        if (!m_Camera->LockedTo(selectedIds))
            m_Camera->SetLockedEntity({});
        else
        {
            const auto bounds = KeireEditor::CalculateSceneEntityBounds(selectedEntities, resolveMeshBounds);
            if (bounds.Valid)
            {
                const auto viewportSize = m_ViewportRect.Size();
                const float aspectRatio = std::max(viewportSize.Width, 1.0F) / std::max(viewportSize.Height, 1.0F);
                m_Camera->FollowFrame(
                    bounds.Center(), bounds.Radius(), aspectRatio,
                    static_cast<float>(m_Controller.SceneViewportTime().UnscaledDeltaTime().Seconds()));
            }
        }
    }

    const bool focusShortcutRegion = imageState.Hovered || ui.WindowFocused();
    if (focusShortcutRegion && !selectedIds.empty())
    {
        const bool routeOverFocusedWindow = imageState.Hovered && !ui.WindowFocused();
        if (ui.Shortcut({.Key = Keire::UiKey::F, .Shift = true, .Global = routeOverFocusedWindow}))
        {
            if (m_Camera->LockedTo(selectedIds))
                m_Camera->SetLockedEntity({});
            else
                m_Camera->SetLockedEntities(selectedIds);
            changed = true;
        }
        else if (ui.Shortcut({.Key = Keire::UiKey::F, .Global = routeOverFocusedWindow}))
        {
            const auto action =
                m_Camera->ApplyFocusShortcut(selectedIds, m_Controller.SceneViewportTime().RealtimeSinceStartup());
            if (action == KeireEditor::SceneFocusShortcutAction::Frame)
            {
                const auto bounds = KeireEditor::CalculateSceneEntityBounds(selectedEntities, resolveMeshBounds);
                if (bounds.Valid)
                {
                    const auto viewportSize = m_ViewportRect.Size();
                    const float aspectRatio = std::max(viewportSize.Width, 1.0F) / std::max(viewportSize.Height, 1.0F);
                    m_Camera->Frame(bounds.Center(), bounds.Radius(), aspectRatio);
                }
            }
            changed = action != KeireEditor::SceneFocusShortcutAction::None;
        }
    }

    if (imageState.Hovered && pointer.RightPressed && !ui.AltDown() && !m_Camera->Capturing())
    {
        m_Controller.SceneViewportWindows()->SetCursorMode(m_Controller.SceneViewportWindow(),
                                                           Keire::CursorMode::RelativeLocked);
        m_Camera->SetCapturing(true);
    }
    else if (m_Camera->Capturing() && (!pointer.RightDown || !ui.WindowFocused()))
    {
        m_Controller.SceneViewportWindows()->SetCursorMode(m_Controller.SceneViewportWindow(),
                                                           Keire::CursorMode::Normal);
        m_Camera->SetCapturing(false);
        if (!m_ProjectRoot.empty())
            (void)m_Camera->Save(m_ProjectRoot / "Library/Editor/SceneCamera.state");
    }

    Keire::Detail::EditorCameraInput input;
    input.PointerDelta = {pointer.Delta.X, pointer.Delta.Y};
    input.Wheel = navigationRegion ? pointer.Wheel : 0.0F;
    input.DeltaSeconds = static_cast<float>(m_Controller.SceneViewportTime().UnscaledDeltaTime().Seconds());
    input.Orbit = navigationRegion && ui.AltDown() && pointer.LeftDown;
    input.Pan = navigationRegion && pointer.MiddleDown;
    input.Zoom = navigationRegion && ui.AltDown() && pointer.RightDown;
    input.Fly = m_Camera->Capturing();
    input.Fast = ui.ShiftDown();
    if (m_Camera->Capturing() || imageState.Hovered)
    {
        input.MoveForward = (ui.KeyDown(Keire::UiKey::W) || ui.KeyDown(Keire::UiKey::Up) ? 1.0F : 0.0F) -
                            (ui.KeyDown(Keire::UiKey::S) || ui.KeyDown(Keire::UiKey::Down) ? 1.0F : 0.0F);
        input.MoveRight = (ui.KeyDown(Keire::UiKey::D) || ui.KeyDown(Keire::UiKey::Right) ? 1.0F : 0.0F) -
                          (ui.KeyDown(Keire::UiKey::A) || ui.KeyDown(Keire::UiKey::Left) ? 1.0F : 0.0F);
        if (m_Camera->Capturing())
            input.MoveUp = (ui.KeyDown(Keire::UiKey::E) ? 1.0F : 0.0F) - (ui.KeyDown(Keire::UiKey::Q) ? 1.0F : 0.0F);
    }
    changed = m_Camera->Update(input) || changed;

    if (changed)
    {
        if (input.Orbit || input.Pan || input.Zoom || input.Fly)
            m_Camera->SetLockedEntity({});
        m_Camera->MarkDirty();
    }
}
