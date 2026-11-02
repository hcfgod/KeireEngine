#include "Keire/Water/WaterBuoyancy.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace Keire
{
    std::optional<WaterBuoyancySample> EvaluateWaterBuoyancy(const WaterSurface& surface,
                                                             const WaterBuoyancyProbe& probe, const Vector3d gravity)
    {
        if (!Math::IsFinite(probe.Position) || !Math::IsFinite(gravity) || !std::isfinite(probe.RadiusMetres) ||
            probe.RadiusMetres <= 0.0 || !std::isfinite(probe.VolumeScale) || probe.VolumeScale < 0.0)
        {
            throw std::invalid_argument(
                "Water buoyancy requires finite inputs, positive radius and nonnegative weight.");
        }
        const auto water = surface.Sample(probe.Position);
        if (!water)
        {
            return std::nullopt;
        }
        WaterBuoyancySample result;
        if (probe.VolumeScale == 0.0)
        {
            return result;
        }
        const double radius = probe.RadiusMetres;
        // Heights are normalized by probe radius to keep cap arithmetic bounded. Both clipping
        // planes are measured along the sampled normal, including the finite water bottom.
        const double upper = std::clamp(-water->SignedDistanceMetres / radius, -1.0, 1.0);
        const double lower =
            std::clamp((-water->SignedDistanceMetres - surface.Definition().DepthMetres) / radius, -1.0, 1.0);
        const double interval = upper - lower;
        if (interval <= 0.0)
        {
            return result;
        }
        // Integral of the sphere's circular slices over [lower, upper]. Factoring the difference
        // avoids subtracting two almost-full spherical cap volumes in shallow water.
        const double sliceFactor = 1.0 - (upper * upper + upper * lower + lower * lower) / 3.0;
        const double volume = std::numbers::pi_v<double> * radius * radius * radius * interval * sliceFactor;
        result.SubmergedVolumeCubicMetres = volume * probe.VolumeScale;
        const double displacedMass = result.SubmergedVolumeCubicMetres * water->DensityKilogramsPerCubicMetre;
        if (!std::isfinite(result.SubmergedVolumeCubicMetres) || !std::isfinite(displacedMass) ||
            result.SubmergedVolumeCubicMetres <= 0.0)
        {
            throw std::out_of_range("Water buoyancy displaced volume or mass exceeds its representable range.");
        }
        // First moment / volume, factored by the same interval. The centroid lies inside the probe.
        const double sum = upper + lower;
        const double firstMoment = sum * (0.5 - (upper * upper + lower * lower) * 0.25);
        const double centroid = radius * std::clamp(firstMoment / sliceFactor, lower, upper);
        result.CenterOfBuoyancyOffset = {water->Normal.X * centroid, water->Normal.Y * centroid,
                                         water->Normal.Z * centroid};
        result.ForceNewtons = {-gravity.X * displacedMass, -gravity.Y * displacedMass, -gravity.Z * displacedMass};
        if (!Math::IsFinite(result.CenterOfBuoyancyOffset) || !Math::IsFinite(result.ForceNewtons))
        {
            throw std::out_of_range("Water buoyancy force or application point exceeds its representable range.");
        }
        return result;
    }
} // namespace Keire
