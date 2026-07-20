#include "KeireClient/Editor/SceneGizmoController.h"

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>

namespace KeireEditor
{
    namespace
    {
        constexpr Keire::UiColor AxisX{0.95F, 0.24F, 0.27F, 1.0F};
        constexpr Keire::UiColor AxisY{0.30F, 0.84F, 0.36F, 1.0F};
        constexpr Keire::UiColor AxisZ{0.25F, 0.52F, 1.0F, 1.0F};
        constexpr Keire::UiColor CameraColor{0.35F, 0.78F, 1.0F, 1.0F};
        constexpr Keire::UiColor LightColor{1.0F, 0.76F, 0.20F, 1.0F};
        constexpr float Pi = 3.14159265358979323846F;

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
    } // namespace

    void SceneGizmoController::DrawToolbar(Keire::UiFrame& ui)
    {
        const auto toolButton = [&](const char* label, const SceneTool tool)
        {
            if (ui.Button(m_Tool == tool ? std::string("[") + label + "]" : label))
                m_Tool = tool;
            ui.SameLine();
        };
        toolButton("View Q", SceneTool::View);
        toolButton("Move W", SceneTool::Translate);
        toolButton("Rotate E", SceneTool::Rotate);
        toolButton("Scale R", SceneTool::Scale);
        if (ui.Button(m_Settings.LocalSpace ? "Local" : "Global"))
            m_Settings.LocalSpace = !m_Settings.LocalSpace;
        ui.SameLine();
        if (ui.Button(m_Settings.Snapping ? "Snap On" : "Snap Off"))
            m_Settings.Snapping = !m_Settings.Snapping;
        ui.SameLine();
        if (ui.Button("Snap Settings"))
            ui.OpenPopup("SceneSnapSettings");
        ui.SameLine();
        if (ui.Button(m_Settings.ShowIcons ? "Gizmos On" : "Gizmos Off"))
            m_Settings.ShowIcons = !m_Settings.ShowIcons;

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
        }
    }

    Keire::EntityId SceneGizmoController::UpdateAndDraw(Keire::UiFrame& ui, const Keire::Ref<Keire::Scene>& scene,
                                                        Keire::EntityId selected, const Keire::RenderCamera& camera,
                                                        const Keire::UiItemRect viewport, const bool allowManipulation,
                                                        BeginUndo beginUndo)
    {
        if (!scene || viewport.Size().Width <= 1.0F || viewport.Size().Height <= 1.0F)
            return selected;

        const auto pointer = ui.PointerState();
        const bool hovered = viewport.Contains(pointer.Position);
        if (hovered && !ui.ControlDown() && !ui.AltDown() && m_Drag.ActiveAxis == Axis::None)
        {
            if (ui.Shortcut({Keire::UiKey::Q}))
                m_Tool = SceneTool::View;
            else if (ui.Shortcut({Keire::UiKey::W}))
                m_Tool = SceneTool::Translate;
            else if (ui.Shortcut({Keire::UiKey::E}))
                m_Tool = SceneTool::Rotate;
            else if (ui.Shortcut({Keire::UiKey::R}))
                m_Tool = SceneTool::Scale;
        }

        const auto viewProjection = Keire::Math::Multiply(camera.Projection, camera.View);
        if (m_Settings.ShowIcons)
        {
            const auto drawIcons = [&](const auto& entities, const bool cameraIcons)
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
                        selected = entity.Id();

                    if (!cameraIcons && m_Settings.ShowLightDirections)
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
            drawIcons(scene->Query<Keire::CameraComponent>(), true);
            drawIcons(scene->Query<Keire::DirectionalLightComponent>(), false);
        }

        const auto entity = scene->FindEntity(selected);
        const auto transform = entity.GetComponent<Keire::TransformComponent>();
        if (!allowManipulation || !entity || !transform || m_Tool == SceneTool::View)
            return selected;

        const auto center = Project(transform->WorldPosition(), viewProjection, viewport);
        if (!center)
            return selected;
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

        if (hovered && pointer.LeftPressed && hoveredAxis != Axis::None && m_Drag.ActiveAxis == Axis::None)
        {
            m_Drag.ActiveAxis = hoveredAxis;
            m_Drag.StartPointer = pointer.Position;
            m_Drag.InitialPosition = transform->LocalPosition();
            m_Drag.InitialEuler = transform->LocalEulerAngles();
            m_Drag.InitialScale = transform->LocalScale();
            m_Drag.WorldLength = worldLength;
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

        if (m_Drag.ActiveAxis != Axis::None)
        {
            if (ui.KeyDown(Keire::UiKey::Escape))
            {
                transform->SetLocalPosition(m_Drag.InitialPosition);
                transform->SetLocalEulerAngles(m_Drag.InitialEuler);
                transform->SetLocalScale(m_Drag.InitialScale);
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
                else if (m_Tool == SceneTool::Scale && m_Drag.ActiveAxis == Axis::Uniform)
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
                    if (m_Tool == SceneTool::Translate)
                    {
                        const auto parent = transform->Parent();
                        const auto parentTransform = parent ? parent.GetComponent<Keire::TransformComponent>()
                                                            : Keire::Ref<Keire::TransformComponent>{};
                        const auto initialWorld =
                            parentTransform
                                ? Keire::Math::TransformPoint(parentTransform->WorldMatrix(), m_Drag.InitialPosition)
                                : m_Drag.InitialPosition;
                        const Keire::Vector3 desiredWorld{initialWorld.X + m_Drag.WorldAxis.X * amount,
                                                          initialWorld.Y + m_Drag.WorldAxis.Y * amount,
                                                          initialWorld.Z + m_Drag.WorldAxis.Z * amount};
                        if (parentTransform)
                        {
                            transform->SetLocalPosition(Keire::Math::TransformPoint(
                                Keire::Math::Inverse(parentTransform->WorldMatrix()), desiredWorld));
                        }
                        else
                            transform->SetLocalPosition(desiredWorld);
                    }
                    else if (m_Tool == SceneTool::Rotate)
                    {
                        auto value = m_Drag.InitialEuler;
                        if (m_Drag.ActiveAxis == Axis::X)
                            value.X += amount;
                        else if (m_Drag.ActiveAxis == Axis::Y)
                            value.Y += amount;
                        else
                            value.Z += amount;
                        transform->SetLocalEulerAngles(value);
                    }
                    else
                    {
                        auto value = m_Drag.InitialScale;
                        if (m_Drag.ActiveAxis == Axis::X)
                            value.X = std::max(0.0001F, value.X + amount);
                        else if (m_Drag.ActiveAxis == Axis::Y)
                            value.Y = std::max(0.0001F, value.Y + amount);
                        else if (m_Drag.ActiveAxis == Axis::Z)
                            value.Z = std::max(0.0001F, value.Z + amount);
                        else
                        {
                            value.X = std::max(0.0001F, value.X + amount);
                            value.Y = std::max(0.0001F, value.Y + amount);
                            value.Z = std::max(0.0001F, value.Z + amount);
                        }
                        transform->SetLocalScale(value);
                    }
                    scene->MarkDirty();
                }
            }
            else
                m_Drag = {};
        }
        return selected;
    }

    void SceneGizmoController::Load(const std::filesystem::path& projectRoot)
    {
        std::ifstream input(projectRoot / "Library/Editor/SceneTools.state");
        std::uint32_t version = 0;
        std::uint32_t tool = 0;
        if (!(input >> version >> tool >> m_Settings.PositionSnap.X >> m_Settings.PositionSnap.Y >>
              m_Settings.PositionSnap.Z >> m_Settings.RotationSnapDegrees >> m_Settings.ScaleSnap >>
              m_Settings.Snapping >> m_Settings.LocalSpace >> m_Settings.ShowIcons >> m_Settings.ShowCameraFrustums >>
              m_Settings.ShowLightDirections) ||
            version != 1 || tool > static_cast<std::uint32_t>(SceneTool::Scale))
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
            output << "1\n"
                   << static_cast<std::uint32_t>(m_Tool) << '\n'
                   << m_Settings.PositionSnap.X << ' ' << m_Settings.PositionSnap.Y << ' ' << m_Settings.PositionSnap.Z
                   << '\n'
                   << m_Settings.RotationSnapDegrees << ' ' << m_Settings.ScaleSnap << '\n'
                   << m_Settings.Snapping << ' ' << m_Settings.LocalSpace << ' ' << m_Settings.ShowIcons << ' '
                   << m_Settings.ShowCameraFrustums << ' ' << m_Settings.ShowLightDirections << '\n';
            Keire::Detail::WriteTextFileAtomically(projectRoot / "Library/Editor/SceneTools.state", output.str());
        }
        catch (...)
        {
        }
    }
} // namespace KeireEditor
