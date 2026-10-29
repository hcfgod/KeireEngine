#pragma once

#include "KeireInternal/Rendering/EnvironmentLightingValidation.h"
#include "KeireInternal/Spatial/FrameRotation.h"

namespace Keire::Detail
{
    // Translation and observer radial up never enter this conversion. All observers share the same sun;
    // only the coordinate basis changes. The rotations must come from the same published frame sample.
    [[nodiscard]] inline EnvironmentLightingSnapshot
    ResolvePlanetCelestialLighting(EnvironmentLightingSnapshot snapshot, const FrameRotation& planetToObserver,
                                   const FrameRotation& rootToObserver)
    {
        ValidateEnvironmentLighting(snapshot);
        if (snapshot.Directional && snapshot.DirectionFrame != EnvironmentDirectionalFrame::LegacyWorld)
        {
            const auto& direction = snapshot.Directional->Direction;
            const auto& rotation =
                snapshot.DirectionFrame == EnvironmentDirectionalFrame::PlanetFixed ? planetToObserver : rootToObserver;
            const auto resolved = rotation.Apply({direction[0], direction[1], direction[2]});
            for (std::size_t axis = 0; axis < direction.size(); ++axis)
            {
                snapshot.Directional->Direction[axis] = static_cast<float>(resolved[axis]);
            }
        }
        snapshot.DirectionFrame = EnvironmentDirectionalFrame::LegacyWorld;
        ValidateEnvironmentLighting(snapshot);
        return snapshot;
    }
} // namespace Keire::Detail
