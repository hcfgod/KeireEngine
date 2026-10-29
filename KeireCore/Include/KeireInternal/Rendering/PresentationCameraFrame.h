#pragma once

#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Math/Math.h"

namespace Keire::Internal
{
    // Keep the origin and view together: a previous-frame view must consume matrices
    // expressed relative to that frame's origin, not the current camera origin.
    struct PresentationCameraFrame final
    {
        Vector3d Origin;
        Matrix4 View;

        [[nodiscard]] Matrix4 RelativeTransform(const TransformComponent& transform, double maximumError) const
        {
            return transform.PresentationRelativeMatrix(Origin, maximumError);
        }
    };

    [[nodiscard]] inline PresentationCameraFrame BuildPresentationCameraFrame(const TransformComponent& transform)
    {
        const auto origin = transform.PresentationWorldPositionPrecise();
        // Translation is exactly zero before float conversion, including at coordinates
        // outside the float range. Preserve the existing camera scale/rotation contract.
        const auto relative = transform.PresentationRelativeMatrix(origin, 0.0);
        return {origin, Math::Inverse(relative)};
    }
} // namespace Keire::Internal
