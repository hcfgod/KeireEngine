#include "KeireClient/Editor/SceneGizmoController.h"

#include "KeireClient/Editor/GpuOcclusionDiagnostics.h"
#include "KeireClient/Editor/ScenePicker.h"

#include "Keire/ECS/Components/CharacterControllerComponent.h"
#include "Keire/ECS/Components/RigidBodyComponent.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <type_traits>

namespace KeireEditor
{
    namespace
    {
        constexpr Keire::UiColor AxisX{0.95F, 0.24F, 0.27F, 1.0F};
        constexpr Keire::UiColor AxisY{0.30F, 0.84F, 0.36F, 1.0F};
        constexpr Keire::UiColor AxisZ{0.25F, 0.52F, 1.0F, 1.0F};
        constexpr Keire::UiColor CameraColor{0.35F, 0.78F, 1.0F, 1.0F};
        constexpr Keire::UiColor LightColor{1.0F, 0.76F, 0.20F, 1.0F};
        constexpr Keire::UiColor ReflectionProbeColor{0.30F, 0.86F, 1.0F, 1.0F};
        constexpr Keire::UiColor LightProbeVolumeColor{0.72F, 0.48F, 1.0F, 1.0F};
        constexpr Keire::UiColor ColliderColor{0.25F, 0.92F, 0.56F, 1.0F};
        constexpr Keire::UiColor TriggerColor{1.0F, 0.43F, 0.18F, 1.0F};
        constexpr Keire::UiColor ControllerColor{0.18F, 0.78F, 1.0F, 1.0F};
        constexpr Keire::UiColor GroundedControllerColor{0.30F, 0.94F, 0.52F, 1.0F};
        constexpr Keire::UiColor StaticBodyColor{0.56F, 0.62F, 0.70F, 1.0F};
        constexpr Keire::UiColor DynamicBodyColor{1.0F, 0.68F, 0.18F, 1.0F};
        constexpr Keire::UiColor KinematicBodyColor{0.30F, 0.72F, 1.0F, 1.0F};
        constexpr float Pi = 3.14159265358979323846F;
        constexpr float MinimumColliderGeometry = 0.001F;
        constexpr float MaximumColliderGeometry = 100'000.0F;

        [[nodiscard]] Keire::UiColor AxisColor(const SceneGizmoController::Axis axis)
        {
            switch (axis)
            {
            case SceneGizmoController::Axis::X:
                return AxisX;
            case SceneGizmoController::Axis::Y:
                return AxisY;
            case SceneGizmoController::Axis::Z:
                return AxisZ;
            default:
                return {0.92F, 0.92F, 0.92F, 1.0F};
            }
        }

        [[nodiscard]] float Length(const Keire::Vector3 value) noexcept
        {
            return std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
        }

        [[nodiscard]] Keire::Vector3 Normalize(const Keire::Vector3 value) noexcept
        {
            const float length = Length(value);
            return length > 0.00001F ? Keire::Vector3{value.X / length, value.Y / length, value.Z / length}
                                     : Keire::Vector3{};
        }

        [[nodiscard]] float Distance(const Keire::UiPosition left, const Keire::UiPosition right) noexcept
        {
            const float x = right.X - left.X;
            const float y = right.Y - left.Y;
            return std::sqrt(x * x + y * y);
        }

        [[nodiscard]] float DistanceToSegment(const Keire::UiPosition point, const Keire::UiPosition start,
                                              const Keire::UiPosition end) noexcept
        {
            const float x = end.X - start.X;
            const float y = end.Y - start.Y;
            const float lengthSquared = x * x + y * y;
            if (lengthSquared <= 0.0001F)
                return Distance(point, start);
            const float projection =
                std::clamp(((point.X - start.X) * x + (point.Y - start.Y) * y) / lengthSquared, 0.0F, 1.0F);
            return Distance(point, {start.X + x * projection, start.Y + y * projection});
        }

        [[nodiscard]] std::optional<Keire::UiPosition>
        Project(const Keire::Vector3 point, const Keire::Matrix4& viewProjection, const Keire::UiItemRect viewport)
        {
            const auto& value = viewProjection.Elements;
            const float x = value[0] * point.X + value[4] * point.Y + value[8] * point.Z + value[12];
            const float y = value[1] * point.X + value[5] * point.Y + value[9] * point.Z + value[13];
            const float w = value[3] * point.X + value[7] * point.Y + value[11] * point.Z + value[15];
            if (!std::isfinite(w) || w <= 0.0001F)
                return std::nullopt;
            const auto size = viewport.Size();
            return Keire::UiPosition{viewport.Minimum.X + (x / w * 0.5F + 0.5F) * size.Width,
                                     viewport.Minimum.Y + (0.5F - y / w * 0.5F) * size.Height};
        }

        [[nodiscard]] float Snap(const float value, const float increment) noexcept
        {
            return increment > 0.00001F ? std::round(value / increment) * increment : value;
        }

        [[nodiscard]] Keire::Vector3 AxisVector(const SceneGizmoController::Axis axis) noexcept
        {
            switch (axis)
            {
            case SceneGizmoController::Axis::X:
                return {1.0F, 0.0F, 0.0F};
            case SceneGizmoController::Axis::Y:
                return {0.0F, 1.0F, 0.0F};
            case SceneGizmoController::Axis::Z:
                return {0.0F, 0.0F, 1.0F};
            default:
                return {};
            }
        }

        void DrawCameraIcon(Keire::UiFrame& ui, const Keire::UiPosition center, const bool selected)
        {
            const float scale = selected ? 1.2F : 1.0F;
            const Keire::UiItemRect body{{center.X - 7.0F * scale, center.Y - 5.0F * scale},
                                         {center.X + 4.0F * scale, center.Y + 5.0F * scale}};
            ui.DrawFilledRectangle(body, {CameraColor.Red, CameraColor.Green, CameraColor.Blue, 0.18F}, 2.0F);
            ui.DrawRectangle(body, CameraColor, selected ? 2.0F : 1.3F, 2.0F);
            ui.DrawTriangle({center.X + 4.0F * scale, center.Y - 4.0F * scale},
                            {center.X + 10.0F * scale, center.Y - 7.0F * scale},
                            {center.X + 10.0F * scale, center.Y + 7.0F * scale}, CameraColor, 1.3F);
        }

        void DrawLightIcon(Keire::UiFrame& ui, const Keire::UiPosition center, const bool selected)
        {
            const float radius = selected ? 6.0F : 5.0F;
            ui.DrawFilledCircle(center, radius, {LightColor.Red, LightColor.Green, LightColor.Blue, 0.24F});
            ui.DrawCircle(center, radius, LightColor, selected ? 2.0F : 1.3F);
            for (int index = 0; index < 8; ++index)
            {
                const float angle = static_cast<float>(index) * Pi * 0.25F;
                const Keire::UiPosition start{center.X + std::cos(angle) * (radius + 2.0F),
                                              center.Y + std::sin(angle) * (radius + 2.0F)};
                const Keire::UiPosition end{center.X + std::cos(angle) * (radius + 6.0F),
                                            center.Y + std::sin(angle) * (radius + 6.0F)};
                ui.DrawLine(start, end, LightColor, 1.2F);
            }
        }

        void DrawProbeIcon(Keire::UiFrame& ui, const Keire::UiPosition center, const Keire::UiColor color,
                           const bool selected)
        {
            const float radius = selected ? 7.0F : 6.0F;
            const std::array points{
                Keire::UiPosition{center.X, center.Y - radius}, Keire::UiPosition{center.X + radius, center.Y},
                Keire::UiPosition{center.X, center.Y + radius}, Keire::UiPosition{center.X - radius, center.Y}};
            ui.DrawFilledCircle(center, radius * 0.55F, {color.Red, color.Green, color.Blue, 0.20F});
            for (std::size_t index = 0; index < points.size(); ++index)
                ui.DrawLine(points[index], points[(index + 1U) % points.size()], color, selected ? 2.0F : 1.3F);
        }

        void DrawCameraFrustum(Keire::UiFrame& ui, const Keire::TransformComponent& transform,
                               const Keire::CameraComponent& camera, const Keire::Matrix4& viewProjection,
                               const Keire::UiItemRect viewport, const bool selected)
        {
            const auto world = transform.WorldMatrix();
            const auto origin = transform.WorldPosition();
            const auto forward = Normalize(Keire::Math::TransformDirection(world, {0.0F, 0.0F, 1.0F}));
            const auto right = Normalize(Keire::Math::TransformDirection(world, {1.0F, 0.0F, 0.0F}));
            const auto up = Normalize(Keire::Math::TransformDirection(world, {0.0F, 1.0F, 0.0F}));
            const float length = 2.5F;
            const float aspect = viewport.Size().Width / std::max(viewport.Size().Height, 1.0F);
            const float halfHeight = camera.Projection() == Keire::CameraProjection::Perspective
                                         ? std::tan(camera.VerticalFieldOfViewDegrees() * Pi / 360.0F) * length
                                         : std::clamp(camera.OrthographicSize() * 0.25F, 0.5F, 5.0F);
            const float halfWidth = halfHeight * aspect;
            const Keire::Vector3 farCenter{origin.X + forward.X * length, origin.Y + forward.Y * length,
                                           origin.Z + forward.Z * length};
            std::array<Keire::Vector3, 4> corners{};
            const std::array signs{Keire::Vector2{-1.0F, -1.0F}, Keire::Vector2{1.0F, -1.0F},
                                   Keire::Vector2{1.0F, 1.0F}, Keire::Vector2{-1.0F, 1.0F}};
            for (std::size_t index = 0; index < corners.size(); ++index)
            {
                corners[index] = {
                    farCenter.X + right.X * halfWidth * signs[index].X + up.X * halfHeight * signs[index].Y,
                    farCenter.Y + right.Y * halfWidth * signs[index].X + up.Y * halfHeight * signs[index].Y,
                    farCenter.Z + right.Z * halfWidth * signs[index].X + up.Z * halfHeight * signs[index].Y};
            }
            const auto projectedOrigin = Project(origin, viewProjection, viewport);
            if (!projectedOrigin)
                return;
            std::array<std::optional<Keire::UiPosition>, 4> projected{};
            for (std::size_t index = 0; index < projected.size(); ++index)
                projected[index] = Project(corners[index], viewProjection, viewport);
            const Keire::UiColor color{CameraColor.Red, CameraColor.Green, CameraColor.Blue, selected ? 0.90F : 0.42F};
            for (std::size_t index = 0; index < projected.size(); ++index)
            {
                if (projected[index])
                {
                    ui.DrawLine(*projectedOrigin, *projected[index], color, selected ? 1.5F : 1.0F);
                    const auto next = (index + 1) % projected.size();
                    if (projected[next])
                        ui.DrawLine(*projected[index], *projected[next], color, selected ? 1.5F : 1.0F);
                }
            }
        }

        void DrawWorldCircle(Keire::UiFrame& ui, const Keire::Vector3 center, const Keire::Vector3 axisA,
                             const Keire::Vector3 axisB, const float radius, const Keire::Matrix4& viewProjection,
                             const Keire::UiItemRect viewport, const Keire::UiColor color)
        {
            constexpr std::size_t segments = 48;
            std::optional<Keire::UiPosition> previous;
            for (std::size_t segment = 0; segment <= segments; ++segment)
            {
                const float angle = static_cast<float>(segment) / static_cast<float>(segments) * Pi * 2.0F;
                const auto point =
                    Keire::Vector3{center.X + (axisA.X * std::cos(angle) + axisB.X * std::sin(angle)) * radius,
                                   center.Y + (axisA.Y * std::cos(angle) + axisB.Y * std::sin(angle)) * radius,
                                   center.Z + (axisA.Z * std::cos(angle) + axisB.Z * std::sin(angle)) * radius};
                const auto projected = Project(point, viewProjection, viewport);
                if (previous && projected)
                    ui.DrawLine(*previous, *projected, color, 1.2F);
                previous = projected;
            }
        }

        [[nodiscard]] Keire::Vector3 Add(const Keire::Vector3 left, const Keire::Vector3 right) noexcept
        {
            return {left.X + right.X, left.Y + right.Y, left.Z + right.Z};
        }

        [[nodiscard]] Keire::Vector3 Multiply(const Keire::Vector3 value, const float scalar) noexcept
        {
            return {value.X * scalar, value.Y * scalar, value.Z * scalar};
        }

        [[nodiscard]] Keire::Vector3 AxisVector(const std::size_t axis) noexcept
        {
            switch (axis)
            {
            case 0:
                return {1.0F, 0.0F, 0.0F};
            case 1:
                return {0.0F, 1.0F, 0.0F};
            default:
                return {0.0F, 0.0F, 1.0F};
            }
        }

        void DrawWorldBox(Keire::UiFrame& ui, const Keire::Matrix4& world, const Keire::Vector3 minimum,
                          const Keire::Vector3 maximum, const Keire::Matrix4& viewProjection,
                          const Keire::UiItemRect viewport, const Keire::UiColor color, const float thickness)
        {
            const std::array local{
                Keire::Vector3{minimum.X, minimum.Y, minimum.Z}, Keire::Vector3{maximum.X, minimum.Y, minimum.Z},
                Keire::Vector3{maximum.X, maximum.Y, minimum.Z}, Keire::Vector3{minimum.X, maximum.Y, minimum.Z},
                Keire::Vector3{minimum.X, minimum.Y, maximum.Z}, Keire::Vector3{maximum.X, minimum.Y, maximum.Z},
                Keire::Vector3{maximum.X, maximum.Y, maximum.Z}, Keire::Vector3{minimum.X, maximum.Y, maximum.Z},
            };
            std::array<std::optional<Keire::UiPosition>, local.size()> projected{};
            for (std::size_t index = 0; index < local.size(); ++index)
                projected[index] = Project(Keire::Math::TransformPoint(world, local[index]), viewProjection, viewport);
            constexpr std::array edges{
                std::array<std::size_t, 2>{0, 1}, std::array<std::size_t, 2>{1, 2}, std::array<std::size_t, 2>{2, 3},
                std::array<std::size_t, 2>{3, 0}, std::array<std::size_t, 2>{4, 5}, std::array<std::size_t, 2>{5, 6},
                std::array<std::size_t, 2>{6, 7}, std::array<std::size_t, 2>{7, 4}, std::array<std::size_t, 2>{0, 4},
                std::array<std::size_t, 2>{1, 5}, std::array<std::size_t, 2>{2, 6}, std::array<std::size_t, 2>{3, 7}};
            for (const auto edge : edges)
            {
                if (projected[edge[0]] && projected[edge[1]])
                    ui.DrawLine(*projected[edge[0]], *projected[edge[1]], color, thickness);
            }
        }

        void DrawLocalCircle(Keire::UiFrame& ui, const Keire::Matrix4& world, const Keire::Vector3 center,
                             const Keire::Vector3 axisA, const Keire::Vector3 axisB, const float radius,
                             const Keire::Matrix4& viewProjection, const Keire::UiItemRect viewport,
                             const Keire::UiColor color, const float thickness)
        {
            constexpr std::size_t segments = 48;
            std::optional<Keire::UiPosition> previous;
            for (std::size_t segment = 0; segment <= segments; ++segment)
            {
                const float angle = static_cast<float>(segment) / static_cast<float>(segments) * Pi * 2.0F;
                const auto local = Add(
                    center, Multiply(Add(Multiply(axisA, std::cos(angle)), Multiply(axisB, std::sin(angle))), radius));
                const auto projected = Project(Keire::Math::TransformPoint(world, local), viewProjection, viewport);
                if (previous && projected)
                    ui.DrawLine(*previous, *projected, color, thickness);
                previous = projected;
            }
        }

        void DrawLocalArc(Keire::UiFrame& ui, const Keire::Matrix4& world, const Keire::Vector3 center,
                          const Keire::Vector3 axisA, const Keire::Vector3 axisB, const float radius,
                          const float startAngle, const float endAngle, const Keire::Matrix4& viewProjection,
                          const Keire::UiItemRect viewport, const Keire::UiColor color, const float thickness)
        {
            constexpr std::size_t segments = 24;
            std::optional<Keire::UiPosition> previous;
            for (std::size_t segment = 0; segment <= segments; ++segment)
            {
                const float alpha = static_cast<float>(segment) / static_cast<float>(segments);
                const float angle = startAngle + (endAngle - startAngle) * alpha;
                const auto local = Add(
                    center, Multiply(Add(Multiply(axisA, std::cos(angle)), Multiply(axisB, std::sin(angle))), radius));
                const auto projected = Project(Keire::Math::TransformPoint(world, local), viewProjection, viewport);
                if (previous && projected)
                    ui.DrawLine(*previous, *projected, color, thickness);
                previous = projected;
            }
        }

        [[nodiscard]] Keire::UiColor ColliderWireColor(const Keire::ColliderComponent& collider,
                                                       const bool selected) noexcept
        {
            const auto base = collider.Trigger() ? TriggerColor : ColliderColor;
            return {base.Red, base.Green, base.Blue, selected ? 0.95F : 0.34F};
        }

        void DrawColliderWireframe(Keire::UiFrame& ui, const Keire::TransformComponent& transform,
                                   const Keire::ColliderComponent& collider, const Keire::Matrix4& viewProjection,
                                   const Keire::UiItemRect viewport, const bool selected,
                                   const MeshBoundsResolver& resolveMeshBounds)
        {
            const auto world = transform.WorldMatrix();
            const auto center = collider.Center();
            const auto color = ColliderWireColor(collider, selected);
            const float thickness = selected ? 2.0F : 1.0F;
            switch (collider.Shape())
            {
            case Keire::ColliderShape::Box:
            {
                const auto halfExtent = collider.HalfExtent();
                DrawWorldBox(ui, world, {center.X - halfExtent.X, center.Y - halfExtent.Y, center.Z - halfExtent.Z},
                             {center.X + halfExtent.X, center.Y + halfExtent.Y, center.Z + halfExtent.Z},
                             viewProjection, viewport, color, thickness);
                break;
            }
            case Keire::ColliderShape::Sphere:
                DrawLocalCircle(ui, world, center, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, collider.Radius(),
                                viewProjection, viewport, color, thickness);
                DrawLocalCircle(ui, world, center, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, collider.Radius(),
                                viewProjection, viewport, color, thickness);
                DrawLocalCircle(ui, world, center, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, collider.Radius(),
                                viewProjection, viewport, color, thickness);
                break;
            case Keire::ColliderShape::Capsule:
            {
                const float halfSegment = std::max(collider.Height() * 0.5F - collider.Radius(), 0.0F);
                const auto top = Add(center, {0.0F, halfSegment, 0.0F});
                const auto bottom = Add(center, {0.0F, -halfSegment, 0.0F});
                DrawLocalCircle(ui, world, top, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, collider.Radius(),
                                viewProjection, viewport, color, thickness);
                DrawLocalCircle(ui, world, bottom, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, collider.Radius(),
                                viewProjection, viewport, color, thickness);
                constexpr std::array radialAxes{Keire::Vector3{1.0F, 0.0F, 0.0F}, Keire::Vector3{-1.0F, 0.0F, 0.0F},
                                                Keire::Vector3{0.0F, 0.0F, 1.0F}, Keire::Vector3{0.0F, 0.0F, -1.0F}};
                for (const auto radial : radialAxes)
                {
                    const auto topPoint =
                        Keire::Math::TransformPoint(world, Add(top, Multiply(radial, collider.Radius())));
                    const auto bottomPoint =
                        Keire::Math::TransformPoint(world, Add(bottom, Multiply(radial, collider.Radius())));
                    const auto projectedTop = Project(topPoint, viewProjection, viewport);
                    const auto projectedBottom = Project(bottomPoint, viewProjection, viewport);
                    if (projectedTop && projectedBottom)
                        ui.DrawLine(*projectedTop, *projectedBottom, color, thickness);
                }
                DrawLocalArc(ui, world, top, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, collider.Radius(), 0.0F, Pi,
                             viewProjection, viewport, color, thickness);
                DrawLocalArc(ui, world, bottom, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, collider.Radius(), Pi,
                             Pi * 2.0F, viewProjection, viewport, color, thickness);
                DrawLocalArc(ui, world, top, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}, collider.Radius(), 0.0F, Pi,
                             viewProjection, viewport, color, thickness);
                DrawLocalArc(ui, world, bottom, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}, collider.Radius(), Pi,
                             Pi * 2.0F, viewProjection, viewport, color, thickness);
                break;
            }
            case Keire::ColliderShape::ConvexMesh:
            case Keire::ColliderShape::TriangleMesh:
                if (resolveMeshBounds && collider.CollisionMesh())
                {
                    if (const auto bounds = resolveMeshBounds(collider.CollisionMesh()))
                    {
                        DrawWorldBox(ui, world, Add(bounds->Minimum, center), Add(bounds->Maximum, center),
                                     viewProjection, viewport, color, thickness);
                    }
                }
                break;
            }
        }

        void DrawCharacterControllerWireframe(Keire::UiFrame& ui, const Keire::TransformComponent& transform,
                                              const Keire::CharacterControllerComponent& controller,
                                              const Keire::Matrix4& viewProjection, const Keire::UiItemRect viewport,
                                              const bool selected)
        {
            const auto world = transform.WorldMatrix();
            const auto base = controller.Grounded() ? GroundedControllerColor : ControllerColor;
            const Keire::UiColor color{base.Red, base.Green, base.Blue, selected ? 0.95F : 0.38F};
            const float thickness = selected ? 2.0F : 1.0F;
            const float radius = controller.Radius();
            const float halfSegment = std::max(controller.Height() * 0.5F - radius, 0.0F);
            const Keire::Vector3 top{0.0F, halfSegment, 0.0F};
            const Keire::Vector3 bottom{0.0F, -halfSegment, 0.0F};

            DrawLocalCircle(ui, world, top, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, radius, viewProjection, viewport,
                            color, thickness);
            DrawLocalCircle(ui, world, bottom, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, radius, viewProjection, viewport,
                            color, thickness);
            constexpr std::array radialAxes{Keire::Vector3{1.0F, 0.0F, 0.0F}, Keire::Vector3{-1.0F, 0.0F, 0.0F},
                                            Keire::Vector3{0.0F, 0.0F, 1.0F}, Keire::Vector3{0.0F, 0.0F, -1.0F}};
            for (const auto radial : radialAxes)
            {
                const auto topPoint = Keire::Math::TransformPoint(world, Add(top, Multiply(radial, radius)));
                const auto bottomPoint = Keire::Math::TransformPoint(world, Add(bottom, Multiply(radial, radius)));
                const auto projectedTop = Project(topPoint, viewProjection, viewport);
                const auto projectedBottom = Project(bottomPoint, viewProjection, viewport);
                if (projectedTop && projectedBottom)
                    ui.DrawLine(*projectedTop, *projectedBottom, color, thickness);
            }
            DrawLocalArc(ui, world, top, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, radius, 0.0F, Pi, viewProjection,
                         viewport, color, thickness);
            DrawLocalArc(ui, world, bottom, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, radius, Pi, Pi * 2.0F,
                         viewProjection, viewport, color, thickness);
            DrawLocalArc(ui, world, top, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}, radius, 0.0F, Pi, viewProjection,
                         viewport, color, thickness);
            DrawLocalArc(ui, world, bottom, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}, radius, Pi, Pi * 2.0F,
                         viewProjection, viewport, color, thickness);
        }

        void DrawRigidBodyGizmo(Keire::UiFrame& ui, const Keire::TransformComponent& transform,
                                const Keire::RigidBodyComponent& body, const Keire::Matrix4& viewProjection,
                                const Keire::UiItemRect viewport, const bool selected)
        {
            Keire::UiColor base = DynamicBodyColor;
            if (body.Motion() == Keire::PhysicsMotionType::Static)
                base = StaticBodyColor;
            else if (body.Motion() == Keire::PhysicsMotionType::Kinematic)
                base = KinematicBodyColor;
            const Keire::UiColor color{base.Red, base.Green, base.Blue, selected ? 0.95F : 0.55F};
            const auto center = transform.WorldPosition();
            const auto projected = Project(center, viewProjection, viewport);
            if (!projected)
                return;

            const float markerRadius = selected ? 5.0F : 3.5F;
            ui.DrawFilledCircle(*projected, markerRadius,
                                {color.Red, color.Green, color.Blue, selected ? 0.28F : 0.16F});
            ui.DrawCircle(*projected, markerRadius, color, selected ? 2.0F : 1.0F);

            const auto velocity = body.LinearVelocity();
            const float speed = Length(velocity);
            if (speed <= 0.001F)
                return;
            const float previewLength = std::clamp(speed * 0.25F, 0.25F, 5.0F);
            const auto end =
                Project(Add(center, Multiply(Normalize(velocity), previewLength)), viewProjection, viewport);
            if (end)
            {
                ui.DrawLine(*projected, *end, color, selected ? 2.0F : 1.0F);
                ui.DrawFilledCircle(*end, 2.0F, color);
            }
        }

        void DrawPointLightRange(Keire::UiFrame& ui, const Keire::TransformComponent& transform,
                                 const Keire::PointLightComponent& light, const Keire::Matrix4& viewProjection,
                                 const Keire::UiItemRect viewport)
        {
            const auto center = transform.WorldPosition();
            const auto color = Keire::UiColor{LightColor.Red, LightColor.Green, LightColor.Blue, 0.55F};
            DrawWorldCircle(ui, center, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, light.Range(), viewProjection, viewport,
                            color);
            DrawWorldCircle(ui, center, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, light.Range(), viewProjection, viewport,
                            color);
            DrawWorldCircle(ui, center, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, light.Range(), viewProjection, viewport,
                            color);
        }

        void DrawSpotLightCone(Keire::UiFrame& ui, const Keire::TransformComponent& transform,
                               const Keire::SpotLightComponent& light, const Keire::Matrix4& viewProjection,
                               const Keire::UiItemRect viewport)
        {
            const auto world = transform.WorldMatrix();
            const auto origin = transform.WorldPosition();
            const auto forward = Normalize(Keire::Math::TransformDirection(world, {0.0F, 0.0F, 1.0F}));
            const auto right = Normalize(Keire::Math::TransformDirection(world, {1.0F, 0.0F, 0.0F}));
            const auto up = Normalize(Keire::Math::TransformDirection(world, {0.0F, 1.0F, 0.0F}));
            const float length = light.Range();
            const float radius = std::tan(light.OuterAngleDegrees() * Pi / 180.0F) * length;
            const auto end = Keire::Vector3{origin.X + forward.X * length, origin.Y + forward.Y * length,
                                            origin.Z + forward.Z * length};
            const auto color = Keire::UiColor{LightColor.Red, LightColor.Green, LightColor.Blue, 0.68F};
            DrawWorldCircle(ui, end, right, up, radius, viewProjection, viewport, color);
            if (const auto projectedOrigin = Project(origin, viewProjection, viewport))
            {
                constexpr std::array signs{Keire::Vector2{-1.0F, 0.0F}, Keire::Vector2{1.0F, 0.0F},
                                           Keire::Vector2{0.0F, -1.0F}, Keire::Vector2{0.0F, 1.0F}};
                for (const auto sign : signs)
                {
                    const auto rim = Keire::Vector3{end.X + right.X * radius * sign.X + up.X * radius * sign.Y,
                                                    end.Y + right.Y * radius * sign.X + up.Y * radius * sign.Y,
                                                    end.Z + right.Z * radius * sign.X + up.Z * radius * sign.Y};
                    if (const auto projectedRim = Project(rim, viewProjection, viewport))
                        ui.DrawLine(*projectedOrigin, *projectedRim, color, 1.3F);
                }
            }
        }
    } // namespace

    bool SceneGizmoController::ApplyToolShortcut(const Keire::UiKey key) noexcept
    {
        switch (key)
        {
        case Keire::UiKey::Q:
            m_Tool = SceneTool::View;
            return true;
        case Keire::UiKey::W:
            m_Tool = SceneTool::Translate;
            return true;
        case Keire::UiKey::E:
            m_Tool = SceneTool::Rotate;
            return true;
        case Keire::UiKey::R:
            m_Tool = SceneTool::Scale;
            return true;
        default:
            return false;
        }
    }

    Keire::UiItemRect SceneGizmoController::DrawOverlayToolbar(Keire::UiFrame& ui, const Keire::UiItemRect viewport,
                                                               const std::uint32_t occlusionPyramidMipCount)
    {
        ClampOcclusionDebugMip(occlusionPyramidMipCount);
        constexpr float size = 28.0F;
        constexpr float gap = 3.0F;
        constexpr float padding = 8.0F;
        const Keire::UiPosition origin{viewport.Minimum.X + padding, viewport.Minimum.Y + padding};
        auto position = origin;
        const auto button = [&](const std::string_view id, const Keire::UiIcon icon, const std::string_view tooltip,
                                const bool selected)
        {
            const bool activated = ui.OverlayIconButton(
                id, icon, {.Position = position, .Size = {size, size}, .Tooltip = tooltip, .Selected = selected});
            position.X += size + gap;
            return activated;
        };
        if (button("SceneViewTool", Keire::UiIcon::View, "View tool (Q)", m_Tool == SceneTool::View))
            m_Tool = SceneTool::View;
        if (button("SceneMoveTool", Keire::UiIcon::Translate, "Move tool (W)", m_Tool == SceneTool::Translate))
            m_Tool = SceneTool::Translate;
        if (button("SceneRotateTool", Keire::UiIcon::Rotate, "Rotate tool (E)", m_Tool == SceneTool::Rotate))
            m_Tool = SceneTool::Rotate;
        if (button("SceneScaleTool", Keire::UiIcon::Scale, "Scale tool (R)", m_Tool == SceneTool::Scale))
            m_Tool = SceneTool::Scale;
        if (button("SceneTransformSpace", m_Settings.LocalSpace ? Keire::UiIcon::Local : Keire::UiIcon::Global,
                   m_Settings.LocalSpace ? "Local handle orientation" : "Global handle orientation", false))
            m_Settings.LocalSpace = !m_Settings.LocalSpace;
        if (button("SceneSnap", Keire::UiIcon::Snap, "Toggle snapping", m_Settings.Snapping))
            m_Settings.Snapping = !m_Settings.Snapping;
        if (button("ScenePhysicsGizmos", Keire::UiIcon::Physics, "Show physics gizmos", m_Settings.ShowPhysicsGizmos))
            m_Settings.ShowPhysicsGizmos = !m_Settings.ShowPhysicsGizmos;
        if (button("SceneColliderEdit", Keire::UiIcon::ColliderEdit, "Edit collider shapes", m_Settings.EditColliders))
            m_Settings.EditColliders = !m_Settings.EditColliders;
        if (button("SceneGizmoSettings", Keire::UiIcon::Settings, "Gizmo and snap settings", false))
            ui.OpenPopup("SceneSnapSettings");

        if (auto popup = ui.BeginPopup("SceneSnapSettings"); popup)
        {
            ui.Text("SNAPPING");
            ui.Separator();
            (void)ui.DragVector3("Position", m_Settings.PositionSnap, 0.05F);
            m_Settings.PositionSnap.X = std::clamp(std::abs(m_Settings.PositionSnap.X), 0.001F, 1000.0F);
            m_Settings.PositionSnap.Y = std::clamp(std::abs(m_Settings.PositionSnap.Y), 0.001F, 1000.0F);
            m_Settings.PositionSnap.Z = std::clamp(std::abs(m_Settings.PositionSnap.Z), 0.001F, 1000.0F);
            (void)ui.SliderFloat("Rotation", m_Settings.RotationSnapDegrees, 1.0F, 180.0F);
            (void)ui.SliderFloat("Scale", m_Settings.ScaleSnap, 0.01F, 2.0F);
            ui.Separator();
            (void)ui.Checkbox("Scene icons", m_Settings.ShowIcons);
            (void)ui.Checkbox("Camera frustums", m_Settings.ShowCameraFrustums);
            (void)ui.Checkbox("Light directions", m_Settings.ShowLightDirections);
            (void)ui.Checkbox("Physics gizmos", m_Settings.ShowPhysicsGizmos);
            (void)ui.Checkbox("Edit collider shapes", m_Settings.EditColliders);
            ui.Separator();
            ui.Text("GPU OCCLUSION DEBUG");
            constexpr std::array debugViews{Keire::GpuOcclusionDebugView::None,
                                            Keire::GpuOcclusionDebugView::VisibilityBounds,
                                            Keire::GpuOcclusionDebugView::HierarchicalDepth};
            if (auto combo = ui.BeginCombo("Visualization", GpuOcclusionDebugViewName(m_Settings.OcclusionDebugView));
                combo)
            {
                for (const auto view : debugViews)
                {
                    if (ui.Selectable(GpuOcclusionDebugViewName(view), m_Settings.OcclusionDebugView == view))
                    {
                        m_Settings.OcclusionDebugView = view;
                        m_Settings.OcclusionDebugMip = 0;
                    }
                }
            }
            if (m_Settings.OcclusionDebugView == Keire::GpuOcclusionDebugView::HierarchicalDepth)
            {
                auto mip = static_cast<std::int32_t>(m_Settings.OcclusionDebugMip);
                if (auto disabled = ui.BeginDisabled(occlusionPyramidMipCount == 0U); disabled)
                {
                    if (ui.SliderInt("HZB Mip", mip, 0,
                                     static_cast<std::int32_t>(std::max(1U, occlusionPyramidMipCount) - 1U)))
                        m_Settings.OcclusionDebugMip = static_cast<std::uint32_t>(mip);
                }
                if (occlusionPyramidMipCount == 0U)
                    ui.Text("No HZB mips are available on this surface.");
                else
                    ui.Text(std::to_string(occlusionPyramidMipCount) + " live HZB mips; mip 0 is highest resolution.");
            }
            (void)ui.Checkbox("Diagnostics metadata", m_Settings.ShowOcclusionMetadata);
            ui.Text("Visualization is session-only. Metadata reports fallback and readback age.");
        }
        return {origin, {position.X - gap, origin.Y + size}};
    }

    SceneGizmoResult SceneGizmoController::UpdateAndDraw(
        Keire::UiFrame& ui, const Keire::Ref<Keire::Scene>& scene, Keire::EntityId selected,
        const Keire::RenderCamera& camera, const Keire::UiItemRect viewport, const bool allowManipulation,
        const bool pointerBlocked, const BeginUndo& beginUndo, const MeshBoundsResolver& resolveMeshBounds,
        const std::span<const Keire::AssetId> selections, const Keire::ScenePresentationRuntime* presentation,
        const UiPanelSettingsResolver& resolveUiPanelSettings, const OpenUiDocument& openUiDocument)
    {
        if (!scene || viewport.Size().Width <= 1.0F || viewport.Size().Height <= 1.0F)
            return {selected};

        const auto pointer = ui.PointerState();
        bool selectionActivated = false;
        bool pointerConsumed = false;
        const bool hovered = !pointerBlocked && viewport.Contains(pointer.Position);
        const bool selectionRequested =
            hovered && !pointerBlocked && pointer.LeftPressed && m_Drag.ActiveAxis == Axis::None &&
            m_ColliderDrag.Handle == ColliderHandle::None && m_UiWorldPanelDrag.Handle == SceneUiRectHandle::None;
        if (hovered && !ui.ControlDown() && !ui.AltDown() && m_Drag.ActiveAxis == Axis::None &&
            m_ColliderDrag.Handle == ColliderHandle::None && m_UiWorldPanelDrag.Handle == SceneUiRectHandle::None)
        {
            if (ui.Shortcut({Keire::UiKey::Q}))
                (void)ApplyToolShortcut(Keire::UiKey::Q);
            else if (ui.Shortcut({Keire::UiKey::W}))
                (void)ApplyToolShortcut(Keire::UiKey::W);
            else if (ui.Shortcut({Keire::UiKey::E}))
                (void)ApplyToolShortcut(Keire::UiKey::E);
            else if (ui.Shortcut({Keire::UiKey::R}))
                (void)ApplyToolShortcut(Keire::UiKey::R);
        }

        const auto viewProjection = Keire::Math::Multiply(camera.Projection, camera.View);
        auto selectedUiDocumentRoute = SceneUiDocumentAuthoringRoute::None;
        if (selected)
        {
            const auto selectedEntity = scene->FindEntity(selected);
            const auto uiDocument = selectedEntity.GetComponent<Keire::UiDocumentComponent>();
            if (uiDocument)
            {
                std::optional<Keire::UiPanelSettingsDefinition> panelSettings;
                if (uiDocument->PanelSettings())
                {
                    if (resolveUiPanelSettings)
                        panelSettings = resolveUiPanelSettings(uiDocument->PanelSettings());
                }
                else
                    panelSettings = Keire::UiPanelSettingsDefinition{};
                selectedUiDocumentRoute = ResolveSceneUiDocumentAuthoringRoute(*uiDocument, panelSettings);

                const bool routeChanged = m_RoutedUiDocument != selected ||
                                          m_RoutedUiVisualTree != uiDocument->VisualTree() ||
                                          m_RoutedUiPanelSettings != uiDocument->PanelSettings() ||
                                          m_RoutedUiDocumentRoute != selectedUiDocumentRoute;
                if (selectedUiDocumentRoute == SceneUiDocumentAuthoringRoute::None)
                {
                    m_RoutedUiDocument = {};
                    m_RoutedUiVisualTree = {};
                    m_RoutedUiPanelSettings = {};
                    m_RoutedUiDocumentRoute = SceneUiDocumentAuthoringRoute::None;
                }
                else if (routeChanged)
                {
                    m_RoutedUiDocument = selected;
                    m_RoutedUiVisualTree = uiDocument->VisualTree();
                    m_RoutedUiPanelSettings = uiDocument->PanelSettings();
                    m_RoutedUiDocumentRoute = selectedUiDocumentRoute;
                    if (selectedUiDocumentRoute == SceneUiDocumentAuthoringRoute::FocusUiBuilder && openUiDocument)
                        openUiDocument(uiDocument->VisualTree());
                }

                if (selectedUiDocumentRoute == SceneUiDocumentAuthoringRoute::WorldSurfaceGizmo && panelSettings &&
                    presentation)
                {
                    const auto geometry = presentation->UiGeometry(selected);
                    const auto transform = selectedEntity.GetComponent<Keire::TransformComponent>();
                    if (geometry && geometry->Visible && transform)
                    {
                        std::array<Keire::UiPosition, 4> corners{};
                        for (std::size_t index = 0; index < corners.size(); ++index)
                        {
                            corners[index] = {viewport.Minimum.X + geometry->ViewportCorners[index].X,
                                              viewport.Minimum.Y + geometry->ViewportCorners[index].Y};
                            ui.DrawLine(corners[index], corners[(index + 1U) % corners.size()],
                                        {0.20F, 0.82F, 1.0F, 0.95F}, 1.6F);
                        }

                        auto hoveredHandle = SceneUiRectHandle::None;
                        for (std::size_t index = 0; index < corners.size(); ++index)
                        {
                            const Keire::UiItemRect handle{{corners[index].X - 5.0F, corners[index].Y - 5.0F},
                                                           {corners[index].X + 5.0F, corners[index].Y + 5.0F}};
                            ui.DrawFilledRectangle(handle, {0.16F, 0.68F, 0.94F, 1.0F}, 1.5F);
                            ui.DrawRectangle(handle, {0.92F, 0.98F, 1.0F, 1.0F}, 1.0F, 1.5F);
                            if (handle.Contains(pointer.Position))
                                hoveredHandle = static_cast<SceneUiRectHandle>(index + 1U);
                        }

                        const Keire::Vector2 baseSize{panelSettings->WorldWidth, panelSettings->WorldHeight};
                        const auto effectiveSize = EffectiveSceneUiWorldPanelSize(baseSize, transform->LocalScale());
                        std::ostringstream sizeLabel;
                        sizeLabel.precision(3);
                        sizeLabel << "World UI  base " << baseSize.X << " x " << baseSize.Y << " m  | effective "
                                  << effectiveSize.X << " x " << effectiveSize.Y << " m";
                        const auto labelPosition = Keire::UiPosition{corners[0].X + 8.0F, corners[0].Y + 8.0F};
                        ui.DrawOverlayText(labelPosition, {0.92F, 0.98F, 1.0F, 1.0F}, sizeLabel.str(), 11.0F, viewport);

                        if (hovered && !pointerBlocked && allowManipulation && pointer.LeftPressed &&
                            hoveredHandle != SceneUiRectHandle::None &&
                            m_UiWorldPanelDrag.Handle == SceneUiRectHandle::None && m_Drag.ActiveAxis == Axis::None &&
                            m_ColliderDrag.Handle == ColliderHandle::None)
                        {
                            m_UiWorldPanelDrag.Transform = transform;
                            m_UiWorldPanelDrag.InitialLocalScale = transform->LocalScale();
                            m_UiWorldPanelDrag.BasePhysicalSize = baseSize;
                            m_UiWorldPanelDrag.StartPointer = pointer.Position;
                            m_UiWorldPanelDrag.Handle = hoveredHandle;
                            m_UiWorldPanelDrag.UndoRecorded = false;
                            for (std::size_t index = 0; index < corners.size(); ++index)
                                m_UiWorldPanelDrag.InitialCorners[index] = {corners[index].X, corners[index].Y};
                            pointerConsumed = true;
                        }
                    }
                }
            }
            else
            {
                m_RoutedUiDocument = {};
                m_RoutedUiVisualTree = {};
                m_RoutedUiPanelSettings = {};
                m_RoutedUiDocumentRoute = SceneUiDocumentAuthoringRoute::None;
            }
        }
        else
        {
            m_RoutedUiDocument = {};
            m_RoutedUiVisualTree = {};
            m_RoutedUiPanelSettings = {};
            m_RoutedUiDocumentRoute = SceneUiDocumentAuthoringRoute::None;
        }

        if (m_UiWorldPanelDrag.Handle != SceneUiRectHandle::None)
        {
            pointerConsumed = true;
            if (ui.KeyDown(Keire::UiKey::Escape))
            {
                if (m_UiWorldPanelDrag.UndoRecorded && m_UiWorldPanelDrag.Transform)
                {
                    m_UiWorldPanelDrag.Transform->SetLocalScale(m_UiWorldPanelDrag.InitialLocalScale);
                    scene->MarkDirty();
                }
                m_UiWorldPanelDrag = {};
            }
            else if (pointer.LeftDown && m_UiWorldPanelDrag.Transform)
            {
                const Keire::Vector2 delta{pointer.Position.X - m_UiWorldPanelDrag.StartPointer.X,
                                           pointer.Position.Y - m_UiWorldPanelDrag.StartPointer.Y};
                auto edit = CalculateSceneUiWorldPanelHandleEdit(
                    m_UiWorldPanelDrag.Handle, m_UiWorldPanelDrag.InitialCorners, delta,
                    m_UiWorldPanelDrag.InitialLocalScale, m_UiWorldPanelDrag.BasePhysicalSize, ui.ShiftDown());
                if (m_Settings.Snapping)
                {
                    edit.EffectivePhysicalSize.X = std::max(Keire::TransformComponent::MinimumScaleMagnitude,
                                                            Snap(edit.EffectivePhysicalSize.X, m_Settings.ScaleSnap));
                    edit.EffectivePhysicalSize.Y = std::max(Keire::TransformComponent::MinimumScaleMagnitude,
                                                            Snap(edit.EffectivePhysicalSize.Y, m_Settings.ScaleSnap));
                    edit.LocalScale.X = std::copysign(edit.EffectivePhysicalSize.X /
                                                          std::max(m_UiWorldPanelDrag.BasePhysicalSize.X, 0.000001F),
                                                      m_UiWorldPanelDrag.InitialLocalScale.X);
                    edit.LocalScale.Y = std::copysign(edit.EffectivePhysicalSize.Y /
                                                          std::max(m_UiWorldPanelDrag.BasePhysicalSize.Y, 0.000001F),
                                                      m_UiWorldPanelDrag.InitialLocalScale.Y);
                }
                if (Keire::TransformComponent::IsValidLocalScale(edit.LocalScale) &&
                    edit.LocalScale != m_UiWorldPanelDrag.Transform->LocalScale())
                {
                    if (!m_UiWorldPanelDrag.UndoRecorded)
                    {
                        beginUndo("Resize World UI Document");
                        m_UiWorldPanelDrag.UndoRecorded = true;
                    }
                    m_UiWorldPanelDrag.Transform->SetLocalScale(edit.LocalScale);
                    scene->MarkDirty();
                }
            }
            else
                m_UiWorldPanelDrag = {};
        }

        if (m_Settings.ShowIcons)
        {
            const auto drawIcons = [&](const auto& entities, const bool cameraIcons, const bool directionalIcons)
            {
                for (const auto& entity : entities)
                {
                    const auto transform = entity.template GetComponent<Keire::TransformComponent>();
                    if (!transform || !entity.ActiveInHierarchy())
                        continue;
                    const auto projected = Project(transform->WorldPosition(), viewProjection, viewport);
                    if (!projected)
                        continue;
                    if (cameraIcons)
                    {
                        DrawCameraIcon(ui, *projected, selected == entity.Id());
                        if (m_Settings.ShowCameraFrustums)
                        {
                            const auto cameraComponent = entity.template GetComponent<Keire::CameraComponent>();
                            if (cameraComponent)
                                DrawCameraFrustum(ui, *transform, *cameraComponent, viewProjection, viewport,
                                                  selected == entity.Id());
                        }
                    }
                    else
                        DrawLightIcon(ui, *projected, selected == entity.Id());
                    if (hovered && pointer.LeftPressed && Distance(pointer.Position, *projected) <= 14.0F)
                    {
                        selected = entity.Id();
                        selectionActivated = true;
                        pointerConsumed = true;
                    }

                    if (directionalIcons && m_Settings.ShowLightDirections)
                    {
                        const auto direction =
                            Normalize(Keire::Math::TransformDirection(transform->WorldMatrix(), {0.0F, 0.0F, 1.0F}));
                        const auto end = Project({transform->WorldPosition().X + direction.X * 2.0F,
                                                  transform->WorldPosition().Y + direction.Y * 2.0F,
                                                  transform->WorldPosition().Z + direction.Z * 2.0F},
                                                 viewProjection, viewport);
                        if (end)
                            ui.DrawLine(*projected, *end, LightColor, selected == entity.Id() ? 2.0F : 1.0F);
                    }
                }
            };
            drawIcons(scene->Query<Keire::CameraComponent>(), true, false);
            drawIcons(scene->Query<Keire::DirectionalLightComponent>(), false, true);
            drawIcons(scene->Query<Keire::PointLightComponent>(), false, false);
            drawIcons(scene->Query<Keire::SpotLightComponent>(), false, true);
            const auto drawProbeIcons = [&](const auto& entities, const Keire::UiColor color)
            {
                for (const auto& entity : entities)
                {
                    const auto transform = entity.template GetComponent<Keire::TransformComponent>();
                    if (!transform || !entity.ActiveInHierarchy())
                        continue;
                    const auto projected = Project(transform->WorldPosition(), viewProjection, viewport);
                    if (!projected)
                        continue;
                    DrawProbeIcon(ui, *projected, color, selected == entity.Id());
                    if (hovered && pointer.LeftPressed && Distance(pointer.Position, *projected) <= 14.0F)
                    {
                        selected = entity.Id();
                        selectionActivated = true;
                        pointerConsumed = true;
                    }
                }
            };
            drawProbeIcons(scene->Query<Keire::ReflectionProbeComponent>(), ReflectionProbeColor);
            drawProbeIcons(scene->Query<Keire::LightProbeVolumeComponent>(), LightProbeVolumeColor);
        }

        for (const auto& colliderEntity : scene->Query<Keire::ColliderComponent>())
        {
            const bool colliderSelected = selected == colliderEntity.Id();
            if (!colliderSelected && !m_Settings.ShowPhysicsGizmos && !m_Settings.EditColliders)
                continue;
            const auto collider = colliderEntity.GetComponent<Keire::ColliderComponent>();
            const auto colliderTransform = colliderEntity.GetComponent<Keire::TransformComponent>();
            if (!collider || !collider->Enabled() || !colliderTransform || !colliderEntity.ActiveInHierarchy())
                continue;
            DrawColliderWireframe(ui, *colliderTransform, *collider, viewProjection, viewport, colliderSelected,
                                  resolveMeshBounds);
        }

        for (const auto& controllerEntity : scene->Query<Keire::CharacterControllerComponent>())
        {
            const bool controllerSelected = selected == controllerEntity.Id();
            if (!controllerSelected && !m_Settings.ShowPhysicsGizmos)
                continue;
            const auto controller = controllerEntity.GetComponent<Keire::CharacterControllerComponent>();
            const auto controllerTransform = controllerEntity.GetComponent<Keire::TransformComponent>();
            if (!controller || !controller->Enabled() || !controllerTransform || !controllerEntity.ActiveInHierarchy())
                continue;
            DrawCharacterControllerWireframe(ui, *controllerTransform, *controller, viewProjection, viewport,
                                             controllerSelected);
        }

        for (const auto& rigidBodyEntity : scene->Query<Keire::RigidBodyComponent>())
        {
            const bool rigidBodySelected = selected == rigidBodyEntity.Id();
            if (!rigidBodySelected && !m_Settings.ShowPhysicsGizmos)
                continue;
            const auto rigidBody = rigidBodyEntity.GetComponent<Keire::RigidBodyComponent>();
            const auto rigidBodyTransform = rigidBodyEntity.GetComponent<Keire::TransformComponent>();
            if (!rigidBody || !rigidBody->Enabled() || !rigidBodyTransform || !rigidBodyEntity.ActiveInHierarchy())
                continue;
            DrawRigidBodyGizmo(ui, *rigidBodyTransform, *rigidBody, viewProjection, viewport, rigidBodySelected);
        }

        if (m_Settings.ShowLightDirections && selected)
        {
            const auto selectedLight = scene->FindEntity(selected);
            const auto selectedTransform = selectedLight.GetComponent<Keire::TransformComponent>();
            if (selectedTransform)
            {
                if (const auto point = selectedLight.GetComponent<Keire::PointLightComponent>())
                    DrawPointLightRange(ui, *selectedTransform, *point, viewProjection, viewport);
                if (const auto spot = selectedLight.GetComponent<Keire::SpotLightComponent>())
                    DrawSpotLightCone(ui, *selectedTransform, *spot, viewProjection, viewport);
            }
        }

        if (selected)
        {
            const auto selectedProbe = scene->FindEntity(selected);
            const auto selectedTransform = selectedProbe.GetComponent<Keire::TransformComponent>();
            if (selectedTransform)
            {
                if (const auto reflection = selectedProbe.GetComponent<Keire::ReflectionProbeComponent>())
                {
                    const auto extents = reflection->BoxExtents();
                    DrawWorldBox(
                        ui, selectedTransform->WorldMatrix(), {-extents.X, -extents.Y, -extents.Z}, extents,
                        viewProjection, viewport,
                        {ReflectionProbeColor.Red, ReflectionProbeColor.Green, ReflectionProbeColor.Blue, 0.92F}, 2.0F);
                    const auto blend = reflection->BlendDistance();
                    const Keire::Vector3 inner{std::max(0.001F, extents.X - blend), std::max(0.001F, extents.Y - blend),
                                               std::max(0.001F, extents.Z - blend)};
                    DrawWorldBox(
                        ui, selectedTransform->WorldMatrix(), {-inner.X, -inner.Y, -inner.Z}, inner, viewProjection,
                        viewport,
                        {ReflectionProbeColor.Red, ReflectionProbeColor.Green, ReflectionProbeColor.Blue, 0.38F}, 1.0F);
                }
                if (const auto volume = selectedProbe.GetComponent<Keire::LightProbeVolumeComponent>())
                {
                    const auto extents = volume->BoxExtents();
                    DrawWorldBox(
                        ui, selectedTransform->WorldMatrix(), {-extents.X, -extents.Y, -extents.Z}, extents,
                        viewProjection, viewport,
                        {LightProbeVolumeColor.Red, LightProbeVolumeColor.Green, LightProbeVolumeColor.Blue, 0.92F},
                        2.0F);
                }
            }
        }

        const auto entity = scene->FindEntity(selected);
        const auto transform = entity.GetComponent<Keire::TransformComponent>();
        const auto collider = entity.GetComponent<Keire::ColliderComponent>();
        if (selectedUiDocumentRoute == SceneUiDocumentAuthoringRoute::FocusUiBuilder)
        {
            if (selectionRequested && !selectionActivated && !pointerConsumed)
            {
                selected = PickSceneEntity(scene, viewport, pointer.Position, camera, resolveMeshBounds);
                selectionActivated = true;
            }
            return {selected, selectionActivated, pointerConsumed};
        }

        struct ProjectedColliderHandle
        {
            ColliderHandle Handle = ColliderHandle::None;
            Keire::UiPosition Position;
            Keire::UiPosition ScreenAxis;
            float ScreenPixelsPerUnit = 1.0F;
        };
        std::vector<ProjectedColliderHandle> colliderHandles;
        const bool colliderEditing =
            allowManipulation && m_Settings.EditColliders && entity && transform && collider && collider->Enabled();
        if (colliderEditing)
        {
            const auto world = transform->WorldMatrix();
            const auto appendHandle = [&](const ColliderHandle handle, const Keire::Vector3 localPosition,
                                          const Keire::Vector3 localDirection)
            {
                const auto projected =
                    Project(Keire::Math::TransformPoint(world, localPosition), viewProjection, viewport);
                const auto projectedOutward = Project(
                    Keire::Math::TransformPoint(world, Add(localPosition, localDirection)), viewProjection, viewport);
                if (!projected || !projectedOutward)
                    return;
                const Keire::UiPosition screenAxis{projectedOutward->X - projected->X,
                                                   projectedOutward->Y - projected->Y};
                const float pixelsPerUnit = Distance(*projected, *projectedOutward);
                if (!std::isfinite(pixelsPerUnit) || pixelsPerUnit < 0.5F)
                    return;
                colliderHandles.push_back({handle, *projected, screenAxis, pixelsPerUnit});
            };
            const auto center = collider->Center();
            switch (collider->Shape())
            {
            case Keire::ColliderShape::Box:
            {
                const auto halfExtent = collider->HalfExtent();
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    const auto direction = AxisVector(axis);
                    const float extent = axis == 0 ? halfExtent.X : axis == 1 ? halfExtent.Y : halfExtent.Z;
                    const auto handle = axis == 0   ? ColliderHandle::BoxX
                                        : axis == 1 ? ColliderHandle::BoxY
                                                    : ColliderHandle::BoxZ;
                    appendHandle(handle, Add(center, Multiply(direction, extent)), direction);
                    appendHandle(handle, Add(center, Multiply(direction, -extent)), Multiply(direction, -1.0F));
                }
                break;
            }
            case Keire::ColliderShape::Sphere:
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    const auto direction = AxisVector(axis);
                    appendHandle(ColliderHandle::SphereRadius, Add(center, Multiply(direction, collider->Radius())),
                                 direction);
                    appendHandle(ColliderHandle::SphereRadius, Add(center, Multiply(direction, -collider->Radius())),
                                 Multiply(direction, -1.0F));
                }
                break;
            case Keire::ColliderShape::Capsule:
            {
                for (const auto direction : {Keire::Vector3{1.0F, 0.0F, 0.0F}, Keire::Vector3{-1.0F, 0.0F, 0.0F},
                                             Keire::Vector3{0.0F, 0.0F, 1.0F}, Keire::Vector3{0.0F, 0.0F, -1.0F}})
                {
                    appendHandle(ColliderHandle::CapsuleRadius, Add(center, Multiply(direction, collider->Radius())),
                                 direction);
                }
                const float end = collider->Height() * 0.5F;
                appendHandle(ColliderHandle::CapsuleHeight, Add(center, {0.0F, end, 0.0F}), {0.0F, 1.0F, 0.0F});
                appendHandle(ColliderHandle::CapsuleHeight, Add(center, {0.0F, -end, 0.0F}), {0.0F, -1.0F, 0.0F});
                break;
            }
            case Keire::ColliderShape::ConvexMesh:
            case Keire::ColliderShape::TriangleMesh:
                break;
            }
        }

        const ProjectedColliderHandle* hoveredColliderHandle = nullptr;
        float hoveredColliderDistance = 9.0F;
        for (const auto& handle : colliderHandles)
        {
            const float distance = Distance(pointer.Position, handle.Position);
            if (distance < hoveredColliderDistance)
            {
                hoveredColliderDistance = distance;
                hoveredColliderHandle = &handle;
            }
            const Keire::UiItemRect rectangle{{handle.Position.X - 4.5F, handle.Position.Y - 4.5F},
                                              {handle.Position.X + 4.5F, handle.Position.Y + 4.5F}};
            ui.DrawFilledRectangle(rectangle, ColliderWireColor(*collider, true), 1.5F);
            ui.DrawRectangle(rectangle, {0.03F, 0.04F, 0.06F, 0.9F}, 1.0F, 1.5F);
        }

        if (hovered && !pointerBlocked && pointer.LeftPressed && hoveredColliderHandle &&
            m_ColliderDrag.Handle == ColliderHandle::None && m_Drag.ActiveAxis == Axis::None)
        {
            pointerConsumed = true;
            m_ColliderDrag.Collider = collider;
            m_ColliderDrag.Handle = hoveredColliderHandle->Handle;
            m_ColliderDrag.StartPointer = pointer.Position;
            m_ColliderDrag.ScreenAxis = hoveredColliderHandle->ScreenAxis;
            m_ColliderDrag.ScreenPixelsPerUnit = hoveredColliderHandle->ScreenPixelsPerUnit;
            m_ColliderDrag.InitialCenter = collider->Center();
            m_ColliderDrag.InitialHalfExtent = collider->HalfExtent();
            m_ColliderDrag.InitialRadius = collider->Radius();
            m_ColliderDrag.InitialHeight = collider->Height();
            m_ColliderDrag.UndoRecorded = false;
        }

        if (m_ColliderDrag.Handle != ColliderHandle::None)
        {
            pointerConsumed = true;
            if (ui.KeyDown(Keire::UiKey::Escape))
            {
                if (m_ColliderDrag.UndoRecorded)
                {
                    m_ColliderDrag.Collider->SetCenter(m_ColliderDrag.InitialCenter);
                    m_ColliderDrag.Collider->SetHalfExtent(m_ColliderDrag.InitialHalfExtent);
                    m_ColliderDrag.Collider->SetRadius(m_ColliderDrag.InitialRadius);
                    m_ColliderDrag.Collider->SetHeight(m_ColliderDrag.InitialHeight);
                    scene->MarkDirty();
                }
                m_ColliderDrag = {};
            }
            else if (pointer.LeftDown)
            {
                const float axisLength =
                    std::max(Length({m_ColliderDrag.ScreenAxis.X, m_ColliderDrag.ScreenAxis.Y, 0.0F}), 0.5F);
                const float normalizedX = m_ColliderDrag.ScreenAxis.X / axisLength;
                const float normalizedY = m_ColliderDrag.ScreenAxis.Y / axisLength;
                const float projectedPixels = (pointer.Position.X - m_ColliderDrag.StartPointer.X) * normalizedX +
                                              (pointer.Position.Y - m_ColliderDrag.StartPointer.Y) * normalizedY;
                float amount = projectedPixels / m_ColliderDrag.ScreenPixelsPerUnit;
                if (m_Settings.Snapping)
                    amount = Snap(amount, m_Settings.ScaleSnap);

                auto nextHalfExtent = m_ColliderDrag.InitialHalfExtent;
                float nextRadius = m_ColliderDrag.InitialRadius;
                float nextHeight = m_ColliderDrag.InitialHeight;
                switch (m_ColliderDrag.Handle)
                {
                case ColliderHandle::BoxX:
                    nextHalfExtent.X = std::clamp(m_ColliderDrag.InitialHalfExtent.X + amount, MinimumColliderGeometry,
                                                  MaximumColliderGeometry);
                    break;
                case ColliderHandle::BoxY:
                    nextHalfExtent.Y = std::clamp(m_ColliderDrag.InitialHalfExtent.Y + amount, MinimumColliderGeometry,
                                                  MaximumColliderGeometry);
                    break;
                case ColliderHandle::BoxZ:
                    nextHalfExtent.Z = std::clamp(m_ColliderDrag.InitialHalfExtent.Z + amount, MinimumColliderGeometry,
                                                  MaximumColliderGeometry);
                    break;
                case ColliderHandle::SphereRadius:
                case ColliderHandle::CapsuleRadius:
                    nextRadius = std::clamp(m_ColliderDrag.InitialRadius + amount, MinimumColliderGeometry,
                                            MaximumColliderGeometry);
                    break;
                case ColliderHandle::CapsuleHeight:
                    nextHeight = std::clamp(m_ColliderDrag.InitialHeight + amount * 2.0F, MinimumColliderGeometry,
                                            MaximumColliderGeometry);
                    break;
                case ColliderHandle::None:
                    break;
                }
                const bool differsFromBaseline = nextHalfExtent != m_ColliderDrag.InitialHalfExtent ||
                                                 nextRadius != m_ColliderDrag.InitialRadius ||
                                                 nextHeight != m_ColliderDrag.InitialHeight;
                const bool differsFromCurrent = nextHalfExtent != m_ColliderDrag.Collider->HalfExtent() ||
                                                nextRadius != m_ColliderDrag.Collider->Radius() ||
                                                nextHeight != m_ColliderDrag.Collider->Height();
                if (differsFromCurrent && (differsFromBaseline || m_ColliderDrag.UndoRecorded))
                {
                    if (!m_ColliderDrag.UndoRecorded)
                    {
                        beginUndo("Resize Collider");
                        m_ColliderDrag.UndoRecorded = true;
                    }
                    m_ColliderDrag.Collider->SetHalfExtent(nextHalfExtent);
                    m_ColliderDrag.Collider->SetRadius(nextRadius);
                    m_ColliderDrag.Collider->SetHeight(nextHeight);
                    scene->MarkDirty();
                }
            }
            else
                m_ColliderDrag = {};
        }

        if (colliderEditing)
        {
            if (selectionRequested && !selectionActivated && !pointerConsumed)
            {
                selected = PickSceneEntity(scene, viewport, pointer.Position, camera, resolveMeshBounds);
                selectionActivated = true;
            }
            return {selected, selectionActivated, pointerConsumed || m_ColliderDrag.Handle != ColliderHandle::None};
        }

        if (!allowManipulation || !entity || !transform || m_Tool == SceneTool::View)
        {
            if (selectionRequested && !selectionActivated)
            {
                selected = PickSceneEntity(scene, viewport, pointer.Position, camera, resolveMeshBounds);
                selectionActivated = true;
            }
            return {selected, selectionActivated, pointerConsumed};
        }

        const auto center = Project(transform->WorldPosition(), viewProjection, viewport);
        if (!center)
        {
            if (selectionRequested && !selectionActivated)
            {
                selected = PickSceneEntity(scene, viewport, pointer.Position, camera, resolveMeshBounds);
                selectionActivated = true;
            }
            return {selected, selectionActivated, pointerConsumed};
        }
        const auto eye = Keire::Math::TransformPoint(Keire::Math::Inverse(camera.View), {});
        const float worldLength =
            std::clamp(Length({transform->WorldPosition().X - eye.X, transform->WorldPosition().Y - eye.Y,
                               transform->WorldPosition().Z - eye.Z}) *
                           0.12F,
                       0.5F, 8.0F);

        struct Handle
        {
            Axis HandleAxis;
            Keire::Vector3 WorldAxis;
            Keire::UiPosition End;
        };
        std::array<Handle, 3> handles{};
        const std::array axes{Axis::X, Axis::Y, Axis::Z};
        for (std::size_t index = 0; index < axes.size(); ++index)
        {
            auto worldAxis = AxisVector(axes[index]);
            if (m_Settings.LocalSpace)
                worldAxis = Normalize(Keire::Math::TransformDirection(transform->WorldMatrix(), worldAxis));
            const auto worldPosition = transform->WorldPosition();
            const auto end =
                Project({worldPosition.X + worldAxis.X * worldLength, worldPosition.Y + worldAxis.Y * worldLength,
                         worldPosition.Z + worldAxis.Z * worldLength},
                        viewProjection, viewport);
            handles[index] = {axes[index], worldAxis, end.value_or(*center)};
        }

        Axis hoveredAxis = Axis::None;
        if (m_Tool == SceneTool::Rotate)
        {
            constexpr std::array radii{34.0F, 42.0F, 50.0F};
            for (std::size_t index = 0; index < axes.size(); ++index)
            {
                ui.DrawCircle(*center, radii[index], AxisColor(axes[index]), 2.0F);
                if (std::abs(Distance(pointer.Position, *center) - radii[index]) <= 5.0F)
                    hoveredAxis = axes[index];
            }
        }
        else
        {
            for (const auto& handle : handles)
            {
                ui.DrawLine(*center, handle.End, AxisColor(handle.HandleAxis), 3.0F);
                if (m_Tool == SceneTool::Translate)
                {
                    const auto direction = Keire::UiPosition{handle.End.X - center->X, handle.End.Y - center->Y};
                    const float length =
                        std::max(std::sqrt(direction.X * direction.X + direction.Y * direction.Y), 0.001F);
                    const Keire::UiPosition perpendicular{-direction.Y / length * 4.0F, direction.X / length * 4.0F};
                    ui.DrawFilledTriangle(handle.End,
                                          {handle.End.X - direction.X / length * 9.0F + perpendicular.X,
                                           handle.End.Y - direction.Y / length * 9.0F + perpendicular.Y},
                                          {handle.End.X - direction.X / length * 9.0F - perpendicular.X,
                                           handle.End.Y - direction.Y / length * 9.0F - perpendicular.Y},
                                          AxisColor(handle.HandleAxis));
                }
                else
                {
                    ui.DrawFilledRectangle(
                        {{handle.End.X - 4.0F, handle.End.Y - 4.0F}, {handle.End.X + 4.0F, handle.End.Y + 4.0F}},
                        AxisColor(handle.HandleAxis), 1.0F);
                }
                if (DistanceToSegment(pointer.Position, *center, handle.End) <= 6.0F)
                    hoveredAxis = handle.HandleAxis;
            }
            if (m_Tool == SceneTool::Scale)
            {
                const Keire::UiItemRect uniformHandle{{center->X - 5.0F, center->Y - 5.0F},
                                                      {center->X + 5.0F, center->Y + 5.0F}};
                ui.DrawFilledRectangle(uniformHandle, {0.92F, 0.92F, 0.92F, 1.0F}, 1.0F);
                if (uniformHandle.Contains(pointer.Position))
                    hoveredAxis = Axis::Uniform;
            }
        }

        if (hovered && pointer.LeftPressed && hoveredAxis != Axis::None && m_Drag.ActiveAxis == Axis::None &&
            !pointerConsumed)
        {
            pointerConsumed = true;
            m_Drag.ActiveAxis = hoveredAxis;
            m_Drag.StartPointer = pointer.Position;
            m_Drag.WorldLength = worldLength;
            m_Drag.Pivot = transform->WorldPosition();
            m_Drag.PivotRotation = {};
            if (m_Settings.LocalSpace)
            {
                Keire::Vector3 position;
                Keire::Vector3 scale;
                (void)Keire::Math::DecomposeTransform(transform->WorldMatrix(), position, m_Drag.PivotRotation, scale);
            }
            m_Drag.Targets = SceneTransformGroup::Capture(scene, selections, selected);
            if (hoveredAxis == Axis::Uniform)
            {
                m_Drag.WorldAxis = {1.0F, 1.0F, 1.0F};
                m_Drag.ScreenAxis = {1.0F, -1.0F};
                m_Drag.ScreenLength = std::sqrt(2.0F);
            }
            else
            {
                m_Drag.WorldAxis = handles[static_cast<std::size_t>(hoveredAxis) - 1].WorldAxis;
                const auto end = handles[static_cast<std::size_t>(hoveredAxis) - 1].End;
                m_Drag.ScreenAxis = {end.X - center->X, end.Y - center->Y};
                m_Drag.ScreenLength = std::max(Distance(end, *center), 1.0F);
            }
            m_Drag.UndoRecorded = false;
        }

        if (selectionRequested && !selectionActivated && !pointerConsumed)
        {
            selected = PickSceneEntity(scene, viewport, pointer.Position, camera, resolveMeshBounds);
            selectionActivated = true;
        }

        if (m_Drag.ActiveAxis != Axis::None)
        {
            if (ui.KeyDown(Keire::UiKey::Escape))
            {
                SceneTransformGroup::Restore(m_Drag.Targets);
                scene->MarkDirty();
                m_Drag = {};
            }
            else if (pointer.LeftDown)
            {
                const float normalizedX = m_Drag.ScreenAxis.X / m_Drag.ScreenLength;
                const float normalizedY = m_Drag.ScreenAxis.Y / m_Drag.ScreenLength;
                const float projectedPixels = (pointer.Position.X - m_Drag.StartPointer.X) * normalizedX +
                                              (pointer.Position.Y - m_Drag.StartPointer.Y) * normalizedY;
                float amount = projectedPixels / m_Drag.ScreenLength * m_Drag.WorldLength;
                if (m_Tool == SceneTool::Rotate)
                    amount = projectedPixels * 0.35F;
                else if (m_Tool == SceneTool::Scale)
                    amount = projectedPixels * 0.01F;
                if (m_Settings.Snapping)
                {
                    if (m_Tool == SceneTool::Translate)
                    {
                        const auto increments = m_Settings.PositionSnap;
                        const float increment = m_Drag.ActiveAxis == Axis::X   ? increments.X
                                                : m_Drag.ActiveAxis == Axis::Y ? increments.Y
                                                                               : increments.Z;
                        amount = Snap(amount, increment);
                    }
                    else if (m_Tool == SceneTool::Rotate)
                        amount = Snap(amount, m_Settings.RotationSnapDegrees);
                    else
                        amount = Snap(amount, m_Settings.ScaleSnap);
                }
                if (std::abs(amount) > 0.00001F)
                {
                    if (!m_Drag.UndoRecorded)
                    {
                        beginUndo(m_Tool == SceneTool::Translate ? "Move Entity"
                                  : m_Tool == SceneTool::Rotate  ? "Rotate Entity"
                                                                 : "Scale Entity");
                        m_Drag.UndoRecorded = true;
                    }
                    const auto axis = static_cast<SceneTransformAxis>(
                        static_cast<std::underlying_type_t<Axis>>(m_Drag.ActiveAxis) - 1);
                    SceneTransformGroup::Apply(m_Drag.Targets, m_Tool, axis, amount, m_Drag.WorldAxis, m_Drag.Pivot,
                                               m_Drag.PivotRotation);
                    scene->MarkDirty();
                }
            }
            else
                m_Drag = {};
        }
        return {selected, selectionActivated, pointerConsumed || m_Drag.ActiveAxis != Axis::None};
    }

    void SceneGizmoController::Load(const std::filesystem::path& projectRoot)
    {
        m_Settings.OcclusionDebugView = Keire::GpuOcclusionDebugView::None;
        m_Settings.OcclusionDebugMip = 0;
        std::ifstream input(projectRoot / "Library/Editor/SceneTools.state");
        std::uint32_t version = 0;
        std::uint32_t tool = 0;
        if (!(input >> version) || (version != 1 && version != 2 && version != 3 && version != 4) ||
            !(input >> tool >> m_Settings.PositionSnap.X >> m_Settings.PositionSnap.Y >> m_Settings.PositionSnap.Z >>
              m_Settings.RotationSnapDegrees >> m_Settings.ScaleSnap >> m_Settings.Snapping >> m_Settings.LocalSpace >>
              m_Settings.ShowIcons >> m_Settings.ShowCameraFrustums >> m_Settings.ShowLightDirections) ||
            tool > static_cast<std::uint32_t>(SceneTool::Scale))
        {
            m_Settings = {};
            m_Tool = SceneTool::Translate;
            return;
        }
        m_Settings.EditColliders = false;
        m_Settings.ShowPhysicsGizmos = true;
        m_Settings.ShowOcclusionMetadata = false;
        if (version >= 2 && !(input >> m_Settings.EditColliders))
        {
            m_Settings = {};
            m_Tool = SceneTool::Translate;
            return;
        }
        if (version >= 3 && !(input >> m_Settings.ShowPhysicsGizmos))
        {
            m_Settings = {};
            m_Tool = SceneTool::Translate;
            return;
        }
        if (version >= 4 && !(input >> m_Settings.ShowOcclusionMetadata))
        {
            m_Settings = {};
            m_Tool = SceneTool::Translate;
            return;
        }
        if (!Keire::Math::IsFinite(m_Settings.PositionSnap) || m_Settings.PositionSnap.X <= 0.0F ||
            m_Settings.PositionSnap.Y <= 0.0F || m_Settings.PositionSnap.Z <= 0.0F ||
            !std::isfinite(m_Settings.RotationSnapDegrees) || m_Settings.RotationSnapDegrees <= 0.0F ||
            !std::isfinite(m_Settings.ScaleSnap) || m_Settings.ScaleSnap <= 0.0F)
        {
            m_Settings = {};
            m_Tool = SceneTool::Translate;
            return;
        }
        m_Tool = static_cast<SceneTool>(tool);
    }

    void SceneGizmoController::Save(const std::filesystem::path& projectRoot) const noexcept
    {
        try
        {
            std::filesystem::create_directories(projectRoot / "Library/Editor");
            std::ostringstream output;
            output << "4\n"
                   << static_cast<std::uint32_t>(m_Tool) << '\n'
                   << m_Settings.PositionSnap.X << ' ' << m_Settings.PositionSnap.Y << ' ' << m_Settings.PositionSnap.Z
                   << '\n'
                   << m_Settings.RotationSnapDegrees << ' ' << m_Settings.ScaleSnap << '\n'
                   << m_Settings.Snapping << ' ' << m_Settings.LocalSpace << ' ' << m_Settings.ShowIcons << ' '
                   << m_Settings.ShowCameraFrustums << ' ' << m_Settings.ShowLightDirections << ' '
                   << m_Settings.EditColliders << ' ' << m_Settings.ShowPhysicsGizmos << ' '
                   << m_Settings.ShowOcclusionMetadata << '\n';
            Keire::Detail::WriteTextFileAtomically(projectRoot / "Library/Editor/SceneTools.state", output.str());
        }
        catch (...)
        {
        }
    }
} // namespace KeireEditor
