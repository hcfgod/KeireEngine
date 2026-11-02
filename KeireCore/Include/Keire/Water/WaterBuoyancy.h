#pragma once

#include "Keire/Api.h"
#include "Keire/Water/WaterSurface.h"

#include <optional>

namespace Keire
{
    struct WaterBuoyancyProbe
    {
        Vector3d Position;
        double RadiusMetres = 0.5;
        /// Weight this probe's displaced volume when approximating a hull with multiple probes.
        double VolumeScale = 1.0;
    };

    struct WaterBuoyancySample
    {
        double SubmergedVolumeCubicMetres = 0.0;
        Vector3d ForceNewtons;
        /// Application-point offset from the probe center in simulation axes.
        Vector3d CenterOfBuoyancyOffset;
    };

    /// Hydrostatic force opposing the supplied effective gravity, with finite water depth clipping.
    /// Uses a locally planar spherical probe; small probes approximate curved surfaces and hulls.
    /// Finite shore boundaries test probe centers, not exact sphere/shore intersection volumes.
    /// Returns no sample outside the water footprint. Zero submersion produces a zero-force sample.
    /// Pure calculation: does not move bodies or submit forces. Inputs must share one resolved frame.
    [[nodiscard]] KEIRE_API std::optional<WaterBuoyancySample>
    EvaluateWaterBuoyancy(const WaterSurface& surface, const WaterBuoyancyProbe& probe, Vector3d gravity);
} // namespace Keire
