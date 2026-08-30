#pragma once

#include "Keire/Scenes/ScenePresentationRuntime.h"
#include "Keire/Ui/UiToolkit.h"

#include <array>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace Keire::Detail
{
    struct ProjectedCanvasState final
    {
        ScenePresentationCanvasGeometry Geometry;
        RuntimeUiElementId Root;
        Matrix4 World;
        Matrix4 ViewProjection;
        Matrix4 InverseViewProjection;
        Vector2 ReferenceResolution{1920.0F, 1080.0F};
        Vector2 Pivot{0.5F, 0.5F};
        Vector2 Viewport;
        Vector2 WorldUnitsPerPixel{0.01F, 0.01F};
        float LayoutScale = 1.0F;
        std::int32_t SortingOrder = 0;
        bool ToolkitDocument = false;
        UiPanelTarget ToolkitTarget = UiPanelTarget::ScreenOverlay;
        AssetId RenderTexture;
        bool DepthTest = false;
        bool ReceivesInput = true;
    };

    struct UiDocumentPanelProjection final
    {
        EntityId Entity;
        RuntimeUiElementId Root;
        UiPanelSettingsDefinition Settings;
        bool ReceivesInput = true;
    };

    [[nodiscard]] std::optional<Vector2> MapCanvasLayoutToViewport(const ProjectedCanvasState& canvas,
                                                                   Vector2 point) noexcept;
    [[nodiscard]] std::optional<Vector2> MapViewportToCanvasLayout(const ProjectedCanvasState& canvas,
                                                                   Vector2 point) noexcept;
    [[nodiscard]] std::optional<Vector2> MapCapturedViewportToCanvasLayout(const ProjectedCanvasState& canvas,
                                                                           Vector2 point) noexcept;
    [[nodiscard]] std::optional<std::array<Vector2, 4>>
    ProjectCanvasRectangleCorners(const ProjectedCanvasState& canvas, RuntimeUiRect rectangle) noexcept;
    [[nodiscard]] RuntimeUiRect ProjectCanvasRectangle(const ProjectedCanvasState& canvas,
                                                       RuntimeUiRect rectangle) noexcept;

    struct ProjectedCanvasHit final
    {
        RuntimeUiElementId Node;
        Vector2 LayoutPoint;
    };

    class ScenePresentationCanvasProjection final
    {
      public:
        void ResetAssignments() noexcept;
        void Clear() noexcept;
        void Assign(RuntimeUiElementId node, RuntimeUiElementId canvasRoot);
        void Rebuild(const Ref<Scene>& scene, const std::map<EntityId, RuntimeUiElementId>& uiNodes,
                     std::span<const UiDocumentPanelProjection> documents, float viewportWidth, float viewportHeight,
                     const RenderCamera* viewportCamera);

        [[nodiscard]] std::optional<ProjectedCanvasHit> ResolveHit(const RuntimeUiTree& tree, float x,
                                                                   float y) const noexcept;
        [[nodiscard]] const ProjectedCanvasState* ForNode(RuntimeUiElementId node) const noexcept;
        [[nodiscard]] std::optional<ScenePresentationCanvasGeometry> CanvasGeometry(EntityId canvas) const noexcept;
        [[nodiscard]] EntityId HitTestCanvas(float x, float y) const noexcept;
        [[nodiscard]] std::optional<ScenePresentationUiGeometry>
        UiGeometry(EntityId entity, const RuntimeUiTree& tree,
                   const std::map<EntityId, RuntimeUiElementId>& uiNodes) const noexcept;
        [[nodiscard]] std::vector<RuntimeUiRenderSubmission> RenderSubmissions(const Ref<RuntimeUiTree>& tree,
                                                                               const Ref<RenderView>& view) const;
        void Draw(const RuntimeUiTree& tree, UiFrame& ui, float offsetX, float offsetY, bool includeOverlay,
                  bool includeWorld) const;

      private:
        std::map<std::uint64_t, RuntimeUiElementId> m_NodeCanvases;
        std::vector<ProjectedCanvasState> m_Canvases;
    };
} // namespace Keire::Detail
