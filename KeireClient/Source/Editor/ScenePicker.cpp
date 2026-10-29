#include "KeireClient/Editor/ScenePicker.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] Keire::Vector3 NormalizedDirection(const Keire::Vector3 from, const Keire::Vector3 to)
        {
            const Keire::Vector3 difference{to.X - from.X, to.Y - from.Y, to.Z - from.Z};
            const float length =
                std::sqrt(difference.X * difference.X + difference.Y * difference.Y + difference.Z * difference.Z);
            if (length <= 0.000001F)
                throw std::runtime_error("Scene picking produced a zero-length ray.");
            return {difference.X / length, difference.Y / length, difference.Z / length};
        }

        Keire::Vector3 Unproject(const Keire::Matrix4& inverseViewProjection, const float x, const float y,
                                 const float z)
        {
            const auto& value = inverseViewProjection.Elements;
            const float resultX = value[0] * x + value[4] * y + value[8] * z + value[12];
            const float resultY = value[1] * x + value[5] * y + value[9] * z + value[13];
            const float resultZ = value[2] * x + value[6] * y + value[10] * z + value[14];
            const float resultW = value[3] * x + value[7] * y + value[11] * z + value[15];
            if (std::abs(resultW) <= 0.000001F)
                throw std::runtime_error("Scene picking produced an invalid homogeneous position.");
            return {resultX / resultW, resultY / resultW, resultZ / resultW};
        }

        [[nodiscard]] std::optional<float> IntersectBounds(const Keire::Matrix4& world, const Keire::MeshBounds& bounds,
                                                           const Keire::Vector3 rayOrigin,
                                                           const Keire::Vector3 rayDirection)
        {
            const auto inverse = Keire::Math::Inverse(world);
            const auto origin = Keire::Math::TransformPoint(inverse, rayOrigin);
            const auto direction = Keire::Math::TransformDirection(inverse, rayDirection);
            float minimum = 0.0F;
            float maximum = std::numeric_limits<float>::max();
            const auto testAxis =
                [&](const float position, const float axisDirection, const float lower, const float upper)
            {
                if (std::abs(axisDirection) <= 0.000001F)
                    return position >= lower && position <= upper;
                float first = (lower - position) / axisDirection;
                float second = (upper - position) / axisDirection;
                if (first > second)
                    std::swap(first, second);
                minimum = std::max(minimum, first);
                maximum = std::min(maximum, second);
                return maximum >= minimum;
            };
            if (!testAxis(origin.X, direction.X, bounds.Minimum.X, bounds.Maximum.X) ||
                !testAxis(origin.Y, direction.Y, bounds.Minimum.Y, bounds.Maximum.Y) ||
                !testAxis(origin.Z, direction.Z, bounds.Minimum.Z, bounds.Maximum.Z))
                return std::nullopt;
            return minimum;
        }

        [[nodiscard]] std::optional<Keire::UiPosition> ProjectToViewport(const Keire::Vector3 point,
                                                                         const Keire::Matrix4& viewProjection,
                                                                         const Keire::UiItemRect viewport)
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

        [[nodiscard]] Keire::MeshBounds BoundsForEntity(const Keire::Entity& entity,
                                                        const MeshBoundsResolver& resolveMeshBounds,
                                                        const PoseBoundsResolver& resolvePoseBounds = {})
        {
            constexpr Keire::MeshBounds transformBounds{{-0.15F, -0.15F, -0.15F}, {0.15F, 0.15F, 0.15F}};
            constexpr Keire::MeshBounds defaultMeshBounds{{-0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, 0.5F}};
            const auto renderer = entity.GetComponent<Keire::MeshRendererComponent>();
            if (!renderer || !renderer->Enabled() || !renderer->Visible())
                return transformBounds;
            if (resolvePoseBounds)
                if (const auto posed = resolvePoseBounds(entity))
                    return *posed;
            if (resolveMeshBounds && renderer->Mesh())
                if (const auto resolved = resolveMeshBounds(renderer->Mesh()))
                    return *resolved;
            return defaultMeshBounds;
        }

        void IncludePoint(SceneEntityBounds& bounds, const Keire::Vector3d point) noexcept
        {
            if (!bounds.Valid)
            {
                bounds.Minimum = point;
                bounds.Maximum = point;
                bounds.Valid = true;
                return;
            }
            bounds.Minimum.X = std::min(bounds.Minimum.X, point.X);
            bounds.Minimum.Y = std::min(bounds.Minimum.Y, point.Y);
            bounds.Minimum.Z = std::min(bounds.Minimum.Z, point.Z);
            bounds.Maximum.X = std::max(bounds.Maximum.X, point.X);
            bounds.Maximum.Y = std::max(bounds.Maximum.Y, point.Y);
            bounds.Maximum.Z = std::max(bounds.Maximum.Z, point.Z);
        }

        void IncludeEntity(SceneEntityBounds& bounds, const Keire::Entity& entity,
                           const MeshBoundsResolver& resolveMeshBounds, const PoseBoundsResolver& resolvePoseBounds)
        {
            if (!entity || !entity.ActiveInHierarchy())
                return;
            if (const auto transform = entity.GetComponent<Keire::TransformComponent>())
            {
                const auto local = BoundsForEntity(entity, resolveMeshBounds, resolvePoseBounds);
                const auto entityOrigin = transform->PresentationWorldPositionPrecise();
                const auto world = transform->PresentationRelativeMatrix(entityOrigin, 0.0);
                const Keire::Vector3d displacement{entityOrigin.X - bounds.WorldOrigin.X,
                                                   entityOrigin.Y - bounds.WorldOrigin.Y,
                                                   entityOrigin.Z - bounds.WorldOrigin.Z};
                for (int corner = 0; corner < 8; ++corner)
                {
                    const Keire::Vector3 point{corner & 1 ? local.Maximum.X : local.Minimum.X,
                                               corner & 2 ? local.Maximum.Y : local.Minimum.Y,
                                               corner & 4 ? local.Maximum.Z : local.Minimum.Z};
                    // Keep distant translation out of the float rotation/scale matrix.
                    const auto localCorner = Keire::Math::TransformPoint(world, point);
                    IncludePoint(bounds, {displacement.X + localCorner.X, displacement.Y + localCorner.Y,
                                          displacement.Z + localCorner.Z});
                }
            }
            for (const auto& child : entity.Children())
                IncludeEntity(bounds, child, resolveMeshBounds, resolvePoseBounds);
        }
    } // namespace

    Keire::Vector3d ResolveSceneDropPositionPrecise(const Keire::UiItemRect viewport, const Keire::UiPosition pointer,
                                                    const Keire::RenderCamera& camera)
    {
        const auto size = viewport.Size();
        if (!std::isfinite(size.Width) || !std::isfinite(size.Height) || size.Width <= 1.0F || size.Height <= 1.0F ||
            !std::isfinite(pointer.X) || !std::isfinite(pointer.Y) || !viewport.Contains(pointer))
            throw std::invalid_argument("Asset placement requires a point inside a valid scene viewport.");
        const float x = (pointer.X - viewport.Minimum.X) / size.Width * 2.0F - 1.0F;
        const float y = 1.0F - (pointer.Y - viewport.Minimum.Y) / size.Height * 2.0F;
        const auto inverse = Keire::Math::Inverse(Keire::Math::Multiply(camera.Projection, camera.View));
        const auto origin = Unproject(inverse, x, y, 0.0F);
        const auto direction = NormalizedDirection(origin, Unproject(inverse, x, y, 1.0F));
        double distance = -1.0;
        if (std::abs(direction.Y) > 0.000001F)
        {
            distance = -(camera.WorldOrigin.Y + origin.Y) / direction.Y;
        }
        if (!std::isfinite(distance) || distance < 0.0F)
        {
            const auto viewOrigin = Keire::Math::TransformPoint(camera.View, origin);
            const auto viewDirection = Keire::Math::TransformDirection(camera.View, direction);
            distance = (10.0F - viewOrigin.Z) / viewDirection.Z;
        }
        const Keire::Vector3d result{camera.WorldOrigin.X + (origin.X + direction.X * distance),
                                     camera.WorldOrigin.Y + (origin.Y + direction.Y * distance),
                                     camera.WorldOrigin.Z + (origin.Z + direction.Z * distance)};
        if (distance < 0.0F || !std::isfinite(result.X) || !std::isfinite(result.Y) || !std::isfinite(result.Z))
            throw std::invalid_argument("Scene camera cannot resolve asset placement.");
        return result;
    }

    Keire::Vector3 ResolveSceneDropPosition(const Keire::UiItemRect viewport, const Keire::UiPosition pointer,
                                            const Keire::RenderCamera& camera)
    {
        return Keire::Math::RelativeFloat(ResolveSceneDropPositionPrecise(viewport, pointer, camera), {},
                                          std::numeric_limits<double>::max());
    }

    Keire::Vector3 SceneEntityBounds::Center() const noexcept
    {
        return {static_cast<float>(Minimum.X * 0.5 + Maximum.X * 0.5),
                static_cast<float>(Minimum.Y * 0.5 + Maximum.Y * 0.5),
                static_cast<float>(Minimum.Z * 0.5 + Maximum.Z * 0.5)};
    }

    Keire::Vector3d SceneEntityBounds::CenterPrecise() const noexcept
    {
        return {WorldOrigin.X + (Minimum.X * 0.5 + Maximum.X * 0.5),
                WorldOrigin.Y + (Minimum.Y * 0.5 + Maximum.Y * 0.5),
                WorldOrigin.Z + (Minimum.Z * 0.5 + Maximum.Z * 0.5)};
    }

    float SceneEntityBounds::Radius() const noexcept
    {
        if (!Valid)
            return 0.0F;
        const double x = Maximum.X * 0.5 - Minimum.X * 0.5;
        const double y = Maximum.Y * 0.5 - Minimum.Y * 0.5;
        const double z = Maximum.Z * 0.5 - Minimum.Z * 0.5;
        const double radius = std::hypot(x, y, z);
        return radius > 0.0
                   ? static_cast<float>(std::min(radius, static_cast<double>(std::numeric_limits<float>::max())))
                   : 0.25F;
    }

    SceneEntityBounds CalculateSceneEntityBounds(const Keire::Entity& entity,
                                                 const MeshBoundsResolver& resolveMeshBounds,
                                                 const PoseBoundsResolver& resolvePoseBounds,
                                                 const Keire::Vector3d worldOrigin)
    {
        SceneEntityBounds bounds;
        bounds.WorldOrigin = worldOrigin;
        IncludeEntity(bounds, entity, resolveMeshBounds, resolvePoseBounds);
        return bounds;
    }

    SceneEntityBounds CalculateSceneEntityBounds(const std::span<const Keire::Entity> entities,
                                                 const MeshBoundsResolver& resolveMeshBounds,
                                                 const PoseBoundsResolver& resolvePoseBounds,
                                                 const Keire::Vector3d worldOrigin)
    {
        SceneEntityBounds bounds;
        bounds.WorldOrigin = worldOrigin;
        for (const auto& entity : entities)
            IncludeEntity(bounds, entity, resolveMeshBounds, resolvePoseBounds);
        return bounds;
    }

    Keire::EntityId PickSceneEntity(const Keire::Ref<Keire::Scene>& scene, const Keire::UiItemRect viewport,
                                    const Keire::UiPosition pointer, const Keire::RenderCamera& camera,
                                    const MeshBoundsResolver& resolveMeshBounds,
                                    const PoseBoundsResolver& resolvePoseBounds)
    {
        if (!scene || !scene->IsOpen() || !viewport.Contains(pointer))
            return {};
        const auto size = viewport.Size();
        if (size.Width <= 1.0F || size.Height <= 1.0F)
            return {};

        const float x = ((pointer.X - viewport.Minimum.X) / size.Width) * 2.0F - 1.0F;
        const float y = 1.0F - ((pointer.Y - viewport.Minimum.Y) / size.Height) * 2.0F;
        const auto inverse = Keire::Math::Inverse(Keire::Math::Multiply(camera.Projection, camera.View));
        const auto nearPoint = Unproject(inverse, x, y, 0.0F);
        const auto farPoint = Unproject(inverse, x, y, 1.0F);
        const auto direction = NormalizedDirection(nearPoint, farPoint);

        float closest = std::numeric_limits<float>::max();
        bool closestHasMesh = false;
        Keire::EntityId selected;
        for (const auto& entity : scene->Entities())
        {
            const auto transform = entity.GetComponent<Keire::TransformComponent>();
            if (!transform || !entity.ActiveInHierarchy())
                continue;

            const auto renderer = entity.GetComponent<Keire::MeshRendererComponent>();
            const bool hasMesh = renderer && renderer->Enabled() && renderer->Visible();
            const auto bounds = BoundsForEntity(entity, resolveMeshBounds, resolvePoseBounds);
            const auto relativeWorld =
                transform->PresentationRelativeMatrix(camera.WorldOrigin, std::numeric_limits<double>::max());
            const auto distance = IntersectBounds(relativeWorld, bounds, nearPoint, direction);
            if (!distance)
                continue;
            constexpr float tieTolerance = 0.0001F;
            if (*distance < closest - tieTolerance ||
                (std::abs(*distance - closest) <= tieTolerance && hasMesh && !closestHasMesh))
            {
                closest = *distance;
                closestHasMesh = hasMesh;
                selected = entity.Id();
            }
        }
        return selected;
    }

    std::vector<Keire::EntityId> SelectSceneEntitiesInRectangle(const Keire::Ref<Keire::Scene>& scene,
                                                                const Keire::UiItemRect viewport,
                                                                Keire::UiItemRect selection,
                                                                const Keire::RenderCamera& camera,
                                                                const MeshBoundsResolver& resolveMeshBounds,
                                                                const PoseBoundsResolver& resolvePoseBounds)
    {
        std::vector<Keire::EntityId> selected;
        if (!scene || !scene->IsOpen() || viewport.Size().Width <= 1.0F || viewport.Size().Height <= 1.0F)
            return selected;
        if (selection.Minimum.X > selection.Maximum.X)
            std::swap(selection.Minimum.X, selection.Maximum.X);
        if (selection.Minimum.Y > selection.Maximum.Y)
            std::swap(selection.Minimum.Y, selection.Maximum.Y);
        const auto viewProjection = Keire::Math::Multiply(camera.Projection, camera.View);
        for (const auto& entity : scene->Entities())
        {
            const auto transform = entity.GetComponent<Keire::TransformComponent>();
            if (!transform || !entity.ActiveInHierarchy())
                continue;
            const auto bounds = BoundsForEntity(entity, resolveMeshBounds, resolvePoseBounds);
            Keire::UiItemRect projected{{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()},
                                        {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()}};
            bool visible = false;
            const auto relativeWorld =
                transform->PresentationRelativeMatrix(camera.WorldOrigin, std::numeric_limits<double>::max());
            for (int corner = 0; corner < 8; ++corner)
            {
                const Keire::Vector3 local{corner & 1 ? bounds.Maximum.X : bounds.Minimum.X,
                                           corner & 2 ? bounds.Maximum.Y : bounds.Minimum.Y,
                                           corner & 4 ? bounds.Maximum.Z : bounds.Minimum.Z};
                const auto screen =
                    ProjectToViewport(Keire::Math::TransformPoint(relativeWorld, local), viewProjection, viewport);
                if (!screen)
                    continue;
                visible = true;
                projected.Minimum.X = std::min(projected.Minimum.X, screen->X);
                projected.Minimum.Y = std::min(projected.Minimum.Y, screen->Y);
                projected.Maximum.X = std::max(projected.Maximum.X, screen->X);
                projected.Maximum.Y = std::max(projected.Maximum.Y, screen->Y);
            }
            if (visible && projected.Maximum.X >= selection.Minimum.X && projected.Minimum.X <= selection.Maximum.X &&
                projected.Maximum.Y >= selection.Minimum.Y && projected.Minimum.Y <= selection.Maximum.Y)
                selected.push_back(entity.Id());
        }
        return selected;
    }

    std::vector<Keire::EntityId> ResolveMaterialDropTargets(const Keire::Entity& target)
    {
        std::vector<Keire::EntityId> result;
        if (!target)
            return result;
        if (target.GetComponent<Keire::MeshRendererComponent>())
        {
            result.push_back(target.Id());
            return result;
        }

        auto pending = target.Children();
        for (std::size_t index = 0; index < pending.size(); ++index)
        {
            const auto& candidate = pending[index];
            if (candidate.GetComponent<Keire::MeshRendererComponent>())
                result.push_back(candidate.Id());
            const auto children = candidate.Children();
            pending.insert(pending.end(), children.begin(), children.end());
        }
        return result;
    }
} // namespace KeireEditor
