#pragma once

#include "Keire/Scenes/ScenePresentationRuntime.h"

#include <array>
#include <map>
#include <optional>
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
        float WorldUnitsPerPixel = 0.01F;
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
                     float viewportWidth, float viewportHeight, const RenderCamera* viewportCamera);

        [[nodiscard]] std::optional<ProjectedCanvasHit> ResolveHit(const RuntimeUiTree& tree, float x,
                                                                   float y) const noexcept;
        [[nodiscard]] const ProjectedCanvasState* ForNode(RuntimeUiElementId node) const noexcept;
        [[nodiscard]] std::optional<ScenePresentationCanvasGeometry> CanvasGeometry(EntityId canvas) const noexcept;
        [[nodiscard]] EntityId HitTestCanvas(float x, float y) const noexcept;
        [[nodiscard]] std::optional<ScenePresentationUiGeometry>
        UiGeometry(EntityId entity, const RuntimeUiTree& tree,
                   const std::map<EntityId, RuntimeUiElementId>& uiNodes) const noexcept;
        void Draw(const RuntimeUiTree& tree, UiFrame& ui, float offsetX, float offsetY) const;

      private:
        std::map<std::uint64_t, RuntimeUiElementId> m_NodeCanvases;
        std::vector<ProjectedCanvasState> m_Canvases;
    };
} // namespace Keire::Detail
