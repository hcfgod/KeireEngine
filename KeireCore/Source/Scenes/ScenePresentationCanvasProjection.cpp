#include "KeireInternal/Scenes/ScenePresentationCanvasProjectionInternal.h"

#include "Keire/ECS/Components/CameraComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Rendering/RenderSystem.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Ui.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <stdexcept>

namespace Keire::Detail
{
    namespace
    {
        [[nodiscard]] std::optional<Vector2> ProjectPoint(const Vector3 point, const Matrix4& viewProjection,
                                                          const Vector2 viewport) noexcept
        {
            const auto& value = viewProjection.Elements;
            const float x = value[0] * point.X + value[4] * point.Y + value[8] * point.Z + value[12];
            const float y = value[1] * point.X + value[5] * point.Y + value[9] * point.Z + value[13];
            const float w = value[3] * point.X + value[7] * point.Y + value[11] * point.Z + value[15];
            if (!std::isfinite(w) || w <= 0.0001F)
                return std::nullopt;
            return Vector2{(x / w * 0.5F + 0.5F) * viewport.X, (0.5F - y / w * 0.5F) * viewport.Y};
        }

        [[nodiscard]] Vector3 Unproject(const Matrix4& inverseViewProjection, const float x, const float y,
                                        const float z)
        {
            const auto& value = inverseViewProjection.Elements;
            const float resultX = value[0] * x + value[4] * y + value[8] * z + value[12];
            const float resultY = value[1] * x + value[5] * y + value[9] * z + value[13];
            const float resultZ = value[2] * x + value[6] * y + value[10] * z + value[14];
            const float resultW = value[3] * x + value[7] * y + value[11] * z + value[15];
            if (!std::isfinite(resultW) || std::abs(resultW) <= 0.000001F)
                throw std::runtime_error("Canvas projection produced an invalid homogeneous point.");
            return {resultX / resultW, resultY / resultW, resultZ / resultW};
        }

        [[nodiscard]] std::optional<Vector2> MapViewportToCanvasLayoutInternal(const ProjectedCanvasState& canvas,
                                                                               const Vector2 point,
                                                                               const bool requireCanvasBounds) noexcept
        {
            if (!canvas.Geometry.Visible || !std::isfinite(point.X) || !std::isfinite(point.Y) ||
                (requireCanvasBounds &&
                 (point.X < 0.0F || point.Y < 0.0F || point.X > canvas.Viewport.X || point.Y > canvas.Viewport.Y)))
                return std::nullopt;
            if (canvas.Geometry.RenderMode != CanvasRenderMode::WorldSpace)
                return point;
            try
            {
                const float x = point.X / canvas.Viewport.X * 2.0F - 1.0F;
                const float y = 1.0F - point.Y / canvas.Viewport.Y * 2.0F;
                const auto inverseWorld = Math::Inverse(canvas.World);
                const auto nearPoint =
                    Math::TransformPoint(inverseWorld, Unproject(canvas.InverseViewProjection, x, y, 0.0F));
                const auto farPoint =
                    Math::TransformPoint(inverseWorld, Unproject(canvas.InverseViewProjection, x, y, 1.0F));
                const Vector3 direction{farPoint.X - nearPoint.X, farPoint.Y - nearPoint.Y, farPoint.Z - nearPoint.Z};
                if (std::abs(direction.Z) <= 0.000001F)
                    return std::nullopt;
                const float distance = -nearPoint.Z / direction.Z;
                if (!std::isfinite(distance) || distance < 0.0F)
                    return std::nullopt;
                const Vector3 local{nearPoint.X + direction.X * distance, nearPoint.Y + direction.Y * distance, 0.0F};
                const float normalizedX =
                    local.X / (canvas.ReferenceResolution.X * canvas.WorldUnitsPerPixel) + canvas.Pivot.X;
                const float normalizedY =
                    canvas.Pivot.Y - local.Y / (canvas.ReferenceResolution.Y * canvas.WorldUnitsPerPixel);
                if (requireCanvasBounds &&
                    (normalizedX < 0.0F || normalizedX > 1.0F || normalizedY < 0.0F || normalizedY > 1.0F))
                    return std::nullopt;
                return Vector2{normalizedX * canvas.Viewport.X, normalizedY * canvas.Viewport.Y};
            }
            catch (...)
            {
                return std::nullopt;
            }
        }
    } // namespace

    std::optional<Vector2> MapCanvasLayoutToViewport(const ProjectedCanvasState& canvas, const Vector2 point) noexcept
    {
        if (!canvas.Geometry.Visible)
            return std::nullopt;
        if (canvas.Geometry.RenderMode != CanvasRenderMode::WorldSpace)
            return point;
        const float normalizedX = point.X / canvas.Viewport.X;
        const float normalizedY = point.Y / canvas.Viewport.Y;
        const Vector3 local{(normalizedX - canvas.Pivot.X) * canvas.ReferenceResolution.X * canvas.WorldUnitsPerPixel,
                            (canvas.Pivot.Y - normalizedY) * canvas.ReferenceResolution.Y * canvas.WorldUnitsPerPixel,
                            0.0F};
        return ProjectPoint(Math::TransformPoint(canvas.World, local), canvas.ViewProjection, canvas.Viewport);
    }

    std::optional<Vector2> MapViewportToCanvasLayout(const ProjectedCanvasState& canvas, const Vector2 point) noexcept
    {
        return MapViewportToCanvasLayoutInternal(canvas, point, true);
    }

    std::optional<Vector2> MapCapturedViewportToCanvasLayout(const ProjectedCanvasState& canvas,
                                                             const Vector2 point) noexcept
    {
        return MapViewportToCanvasLayoutInternal(canvas, point, false);
    }

    std::optional<std::array<Vector2, 4>> ProjectCanvasRectangleCorners(const ProjectedCanvasState& canvas,
                                                                        const RuntimeUiRect rectangle) noexcept
    {
        const std::array points{Vector2{rectangle.X, rectangle.Y}, Vector2{rectangle.X + rectangle.Width, rectangle.Y},
                                Vector2{rectangle.X + rectangle.Width, rectangle.Y + rectangle.Height},
                                Vector2{rectangle.X, rectangle.Y + rectangle.Height}};
        std::array<Vector2, 4> result{};
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            const auto projected = MapCanvasLayoutToViewport(canvas, points[index]);
            if (!projected)
                return std::nullopt;
            result[index] = *projected;
        }
        return result;
    }

    RuntimeUiRect ProjectCanvasRectangle(const ProjectedCanvasState& canvas, const RuntimeUiRect rectangle) noexcept
    {
        const auto points = ProjectCanvasRectangleCorners(canvas, rectangle);
        if (!points)
            return {};
        RuntimeUiRect result;
        float maximumX = points->front().X;
        float maximumY = points->front().Y;
        result.X = maximumX;
        result.Y = maximumY;
        for (const auto point : *points)
        {
            result.X = std::min(result.X, point.X);
            result.Y = std::min(result.Y, point.Y);
            maximumX = std::max(maximumX, point.X);
            maximumY = std::max(maximumY, point.Y);
        }
        result.Width = maximumX - result.X;
        result.Height = maximumY - result.Y;
        return result;
    }

    void ScenePresentationCanvasProjection::ResetAssignments() noexcept { m_NodeCanvases.clear(); }

    void ScenePresentationCanvasProjection::Clear() noexcept
    {
        m_NodeCanvases.clear();
        m_Canvases.clear();
    }

    void ScenePresentationCanvasProjection::Assign(const RuntimeUiElementId node, const RuntimeUiElementId canvasRoot)
    {
        if (node && canvasRoot)
            m_NodeCanvases.insert_or_assign(node.Value(), canvasRoot);
    }

    void ScenePresentationCanvasProjection::Rebuild(const Ref<Scene>& scene,
                                                    const std::map<EntityId, RuntimeUiElementId>& uiNodes,
                                                    const float viewportWidth, const float viewportHeight,
                                                    const RenderCamera* viewportCamera)
    {
        const auto resolveCamera = [&](const CanvasComponent& canvas) -> std::optional<RenderCamera>
        {
            if (viewportCamera)
                return *viewportCamera;
            Entity selected;
            if (canvas.RenderCameraEntity())
                selected = scene->FindEntity(canvas.RenderCameraEntity());
            if (!selected || !selected.GetComponent<CameraComponent>() || !selected.GetComponent<TransformComponent>())
            {
                std::int32_t priority = std::numeric_limits<std::int32_t>::min();
                for (const auto& candidate : scene->Query<CameraComponent>())
                {
                    const auto camera = candidate.GetComponent<CameraComponent>();
                    const auto transform = candidate.GetComponent<TransformComponent>();
                    if (!candidate.ActiveInHierarchy() || !camera->Enabled() || !camera->Primary() || !transform ||
                        camera->Priority() < priority)
                        continue;
                    selected = candidate;
                    priority = camera->Priority();
                }
            }
            const auto camera = selected ? selected.GetComponent<CameraComponent>() : Ref<CameraComponent>{};
            const auto transform = selected ? selected.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
            if (!camera || !transform)
                return std::nullopt;
            RenderCamera result;
            result.View = Math::Inverse(transform->PresentationWorldMatrix());
            result.Projection = camera->ProjectionMatrix(viewportWidth / viewportHeight);
            result.ClearColor = camera->ClearColor();
            result.NearPlane = camera->NearPlane();
            result.FarPlane = camera->FarPlane();
            return result;
        };

        m_Canvases.clear();
        const Vector2 viewport{viewportWidth, viewportHeight};
        for (const auto& entity : scene->Query<CanvasComponent>())
        {
            const auto foundRoot = uiNodes.find(entity.Id());
            if (!entity || foundRoot == uiNodes.end())
                continue;
            const auto canvas = entity.GetComponent<CanvasComponent>();
            ProjectedCanvasState state;
            state.Geometry.Canvas = entity.Id();
            state.Geometry.RenderMode = canvas->RenderMode();
            state.Geometry.PlaneDistance = canvas->PlaneDistance();
            state.Root = foundRoot->second;
            state.ReferenceResolution = canvas->ReferenceResolution();
            state.WorldUnitsPerPixel = canvas->WorldUnitsPerPixel();
            state.Viewport = viewport;
            if (const auto rect = entity.GetComponent<RectTransformComponent>())
                state.Pivot = rect->Pivot();

            if (canvas->RenderMode() != CanvasRenderMode::WorldSpace)
            {
                state.Geometry.ViewportCorners = {Vector2{0.0F, 0.0F}, Vector2{viewportWidth, 0.0F},
                                                  Vector2{viewportWidth, viewportHeight},
                                                  Vector2{0.0F, viewportHeight}};
                if (canvas->RenderMode() == CanvasRenderMode::ScreenSpaceOverlay)
                {
                    state.Geometry.Visible = true;
                }
                else if (const auto camera = resolveCamera(*canvas))
                {
                    state.Geometry.Visible =
                        canvas->PlaneDistance() >= camera->NearPlane && canvas->PlaneDistance() <= camera->FarPlane;
                }
                m_Canvases.push_back(std::move(state));
                continue;
            }

            const auto camera = resolveCamera(*canvas);
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!camera || !transform)
            {
                m_Canvases.push_back(std::move(state));
                continue;
            }
            state.World = transform->PresentationWorldMatrix();
            state.ViewProjection = Math::Multiply(camera->Projection, camera->View);
            try
            {
                state.InverseViewProjection = Math::Inverse(state.ViewProjection);
            }
            catch (...)
            {
                m_Canvases.push_back(std::move(state));
                continue;
            }
            const std::array layoutCorners{Vector2{0.0F, 0.0F}, Vector2{viewportWidth, 0.0F},
                                           Vector2{viewportWidth, viewportHeight}, Vector2{0.0F, viewportHeight}};
            bool visible = true;
            state.Geometry.Visible = true;
            for (std::size_t index = 0; index < layoutCorners.size(); ++index)
            {
                const auto projected = MapCanvasLayoutToViewport(state, layoutCorners[index]);
                if (!projected)
                {
                    visible = false;
                    break;
                }
                state.Geometry.ViewportCorners[index] = *projected;
            }
            state.Geometry.Visible = visible;
            m_Canvases.push_back(std::move(state));
        }
        std::ranges::stable_sort(m_Canvases,
                                 [scene](const ProjectedCanvasState& left, const ProjectedCanvasState& right)
                                 {
                                     const auto leftEntity = scene->FindEntity(left.Geometry.Canvas);
                                     const auto rightEntity = scene->FindEntity(right.Geometry.Canvas);
                                     const auto leftCanvas = leftEntity.GetComponent<CanvasComponent>();
                                     const auto rightCanvas = rightEntity.GetComponent<CanvasComponent>();
                                     return leftCanvas && rightCanvas &&
                                            leftCanvas->SortingOrder() < rightCanvas->SortingOrder();
                                 });
    }

    std::optional<ProjectedCanvasHit> ScenePresentationCanvasProjection::ResolveHit(const RuntimeUiTree& tree,
                                                                                    const float x,
                                                                                    const float y) const noexcept
    {
        for (auto iterator = m_Canvases.rbegin(); iterator != m_Canvases.rend(); ++iterator)
        {
            const auto point = MapViewportToCanvasLayout(*iterator, {x, y});
            if (!point)
                continue;
            const auto hit = tree.HitTestWithin(iterator->Root, point->X, point->Y);
            if (hit)
                return ProjectedCanvasHit{*hit, *point};
        }
        return std::nullopt;
    }

    const ProjectedCanvasState* ScenePresentationCanvasProjection::ForNode(const RuntimeUiElementId node) const noexcept
    {
        const auto found = m_NodeCanvases.find(node.Value());
        if (found == m_NodeCanvases.end())
            return nullptr;
        const auto canvas = std::ranges::find_if(m_Canvases, [root = found->second](const ProjectedCanvasState& state)
                                                 { return state.Root == root; });
        return canvas == m_Canvases.end() ? nullptr : &*canvas;
    }

    std::optional<ScenePresentationCanvasGeometry>
    ScenePresentationCanvasProjection::CanvasGeometry(const EntityId canvas) const noexcept
    {
        const auto found = std::ranges::find_if(m_Canvases, [canvas](const ProjectedCanvasState& state)
                                                { return state.Geometry.Canvas == canvas; });
        return found == m_Canvases.end() ? std::nullopt : std::optional{found->Geometry};
    }

    EntityId ScenePresentationCanvasProjection::HitTestCanvas(const float x, const float y) const noexcept
    {
        for (auto iterator = m_Canvases.rbegin(); iterator != m_Canvases.rend(); ++iterator)
            if (iterator->Geometry.RenderMode == CanvasRenderMode::WorldSpace &&
                MapViewportToCanvasLayout(*iterator, {x, y}))
                return iterator->Geometry.Canvas;
        return {};
    }

    std::optional<ScenePresentationUiGeometry>
    ScenePresentationCanvasProjection::UiGeometry(const EntityId entity, const RuntimeUiTree& tree,
                                                  const std::map<EntityId, RuntimeUiElementId>& uiNodes) const noexcept
    {
        try
        {
            const auto found = uiNodes.find(entity);
            if (found == uiNodes.end())
                return std::nullopt;
            const auto state = tree.State(found->second);
            const auto projection = ForNode(found->second);
            if (!state || !projection || !projection->Geometry.Visible)
                return std::nullopt;
            const std::array points{Vector2{state->Rect.X, state->Rect.Y},
                                    Vector2{state->Rect.X + state->Rect.Width, state->Rect.Y},
                                    Vector2{state->Rect.X + state->Rect.Width, state->Rect.Y + state->Rect.Height},
                                    Vector2{state->Rect.X, state->Rect.Y + state->Rect.Height}};
            ScenePresentationUiGeometry result;
            result.Entity = entity;
            result.Canvas = projection->Geometry.Canvas;
            result.Visible = true;
            for (std::size_t index = 0; index < points.size(); ++index)
            {
                const auto projected = MapCanvasLayoutToViewport(*projection, points[index]);
                if (!projected)
                    return std::nullopt;
                result.ViewportCorners[index] = *projected;
            }
            return result;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    void ScenePresentationCanvasProjection::Draw(const RuntimeUiTree& tree, UiFrame& ui, const float offsetX,
                                                 const float offsetY) const
    {
        for (const auto& command : tree.DrawCommands())
        {
            if (command.Type == RuntimeUiDrawType::PushClip || command.Type == RuntimeUiDrawType::PopClip)
                continue;
            const auto projection = ForNode(command.Element);
            if (!projection || !projection->Geometry.Visible)
                continue;
            const auto clipped = command.Rect.Intersect(command.ClipRect);
            if (clipped.Empty())
                continue;
            const auto projected = ProjectCanvasRectangle(*projection, clipped);
            const auto projectedCorners = ProjectCanvasRectangleCorners(*projection, clipped);
            if (projected.Empty())
                continue;
            const UiItemRect rectangle{
                {offsetX + projected.X, offsetY + projected.Y},
                {offsetX + projected.X + projected.Width, offsetY + projected.Y + projected.Height}};
            const float presentationScale = std::sqrt(std::max(0.0F, projected.Width * projected.Height) /
                                                      std::max(0.0001F, clipped.Width * clipped.Height));
            const UiColor color{command.ColorValue.Red, command.ColorValue.Green, command.ColorValue.Blue,
                                command.ColorValue.Alpha};
            switch (command.Type)
            {
            case RuntimeUiDrawType::Quad:
            case RuntimeUiDrawType::Image:
                if (projection->Geometry.RenderMode == CanvasRenderMode::WorldSpace && projectedCorners)
                {
                    std::array<UiPosition, 4> corners{};
                    for (std::size_t index = 0; index < corners.size(); ++index)
                        corners[index] = {offsetX + (*projectedCorners)[index].X,
                                          offsetY + (*projectedCorners)[index].Y};
                    ui.DrawFilledTriangle(corners[0], corners[1], corners[2], color);
                    ui.DrawFilledTriangle(corners[0], corners[2], corners[3], color);
                    if (command.BorderWidth > 0.0F)
                    {
                        const UiColor border{command.BorderColor.Red, command.BorderColor.Green,
                                             command.BorderColor.Blue, command.BorderColor.Alpha};
                        for (std::size_t index = 0; index < corners.size(); ++index)
                            ui.DrawLine(corners[index], corners[(index + 1U) % corners.size()], border,
                                        command.BorderWidth * presentationScale);
                    }
                }
                else
                {
                    ui.DrawFilledRectangle(rectangle, color, command.CornerRadius * presentationScale);
                    if (command.BorderWidth > 0.0F)
                        ui.DrawRectangle(rectangle,
                                         {command.BorderColor.Red, command.BorderColor.Green, command.BorderColor.Blue,
                                          command.BorderColor.Alpha},
                                         command.BorderWidth * presentationScale,
                                         command.CornerRadius * presentationScale);
                }
                break;
            case RuntimeUiDrawType::Text:
            {
                const auto measured = ui.MeasureText(command.Text, command.FontSize);
                float textX = command.Rect.X;
                float textY = command.Rect.Y;
                if (command.HorizontalAlignment == RuntimeUiAlignment::Center)
                    textX += (command.Rect.Width - measured.Width) * 0.5F;
                else if (command.HorizontalAlignment == RuntimeUiAlignment::End)
                    textX += command.Rect.Width - measured.Width;
                if (command.VerticalAlignment == RuntimeUiAlignment::Center)
                    textY += (command.Rect.Height - measured.Height) * 0.5F;
                else if (command.VerticalAlignment == RuntimeUiAlignment::End)
                    textY += command.Rect.Height - measured.Height;
                const auto projectedText = MapCanvasLayoutToViewport(*projection, {textX, textY});
                const auto projectedClip = ProjectCanvasRectangle(*projection, command.ClipRect);
                if (!projectedText || projectedClip.Empty())
                    break;
                const UiItemRect textClip{{offsetX + projectedClip.X, offsetY + projectedClip.Y},
                                          {offsetX + projectedClip.X + projectedClip.Width,
                                           offsetY + projectedClip.Y + projectedClip.Height}};
                ui.DrawOverlayText({offsetX + projectedText->X, offsetY + projectedText->Y}, color, command.Text,
                                   command.FontSize * presentationScale, textClip);
                break;
            }
            case RuntimeUiDrawType::PushClip:
            case RuntimeUiDrawType::PopClip:
                break;
            }
        }
    }
} // namespace Keire::Detail
