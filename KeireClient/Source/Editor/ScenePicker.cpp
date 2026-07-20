#include "KeireClient/Editor/ScenePicker.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] Keire::Vector3 Unproject(const Keire::Matrix4& inverseViewProjection, const float x,
                                               const float y, const float z)
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

        [[nodiscard]] Keire::Vector3 NormalizedDirection(const Keire::Vector3 from, const Keire::Vector3 to)
        {
            const Keire::Vector3 difference{to.X - from.X, to.Y - from.Y, to.Z - from.Z};
            const float length =
                std::sqrt(difference.X * difference.X + difference.Y * difference.Y + difference.Z * difference.Z);
            if (length <= 0.000001F)
                throw std::runtime_error("Scene picking produced a zero-length ray.");
            return {difference.X / length, difference.Y / length, difference.Z / length};
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
    } // namespace

    Keire::EntityId PickSceneEntity(const Keire::Ref<Keire::Scene>& scene, const Keire::UiItemRect viewport,
                                    const Keire::UiPosition pointer, const Keire::RenderCamera& camera,
                                    const MeshBoundsResolver& resolveMeshBounds)
    {
        if (!scene || !viewport.Contains(pointer))
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

        constexpr Keire::MeshBounds transformBounds{{-0.15F, -0.15F, -0.15F}, {0.15F, 0.15F, 0.15F}};
        constexpr Keire::MeshBounds defaultMeshBounds{{-0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, 0.5F}};
        float closest = std::numeric_limits<float>::max();
        bool closestHasMesh = false;
        Keire::EntityId selected;
        for (const auto& entity : scene->Entities())
        {
            const auto transform = entity.GetComponent<Keire::TransformComponent>();
            if (!transform || !entity.ActiveInHierarchy())
                continue;

            auto bounds = transformBounds;
            const auto renderer = entity.GetComponent<Keire::MeshRendererComponent>();
            const bool hasMesh = renderer && renderer->Enabled() && renderer->Visible();
            if (hasMesh)
            {
                bounds = defaultMeshBounds;
                if (resolveMeshBounds && renderer->Mesh())
                {
                    if (const auto resolved = resolveMeshBounds(renderer->Mesh()))
                        bounds = *resolved;
                }
            }
            const auto distance = IntersectBounds(transform->WorldMatrix(), bounds, nearPoint, direction);
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
} // namespace KeireEditor
