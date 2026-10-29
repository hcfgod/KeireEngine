#pragma once

#include "Keire/Assets/Asset.h"
#include "Keire/Rendering/EnvironmentLighting.h"

#include <array>
#include <cstdint>

namespace Keire
{
    /// Per-view planet sample resolved in double precision by the owning world before render submission.
    /// Coordinates are planet-fixed metres; rotation maps planet-fixed vectors into the camera's world axes.
    struct PlanetEnvironmentView
    {
        AssetId Planet;
        std::uint64_t World = 0;
        std::uint64_t PlanetFrame = 0;
        std::uint64_t ObserverFrame = 0;
        std::uint64_t TopologyRevision = 0;
        std::uint64_t DiscontinuityRevision = 0;
        double SnapshotTime = 0.0;
        EnvironmentTimeSample Clock;
        std::array<double, 3> ObserverFromCenterPlanetMetres{};
        /// Unit quaternion in X, Y, Z, W order.
        std::array<double, 4> PlanetToObserverRotation{0.0, 0.0, 0.0, 1.0};
        double RadiusMetres = 0.0;
        std::array<double, 4> RootToObserverRotation{0.0, 0.0, 0.0, 1.0};
    };
} // namespace Keire
