#pragma once

#include <cmath>
#include <stdexcept>

namespace Keire::Detail
{
    struct VolumeSegmentIntegral
    {
        double Transmittance = 1.0;
        /// Metres: multiply by constant in-scattered/emitted radiance per metre.
        double SourceWeight = 0.0;
    };

    /// Exact homogeneous segment, serving as a reference for cloud/fog ray-march integration.
    /// Extinction is inverse metres; source radiance is evaluated separately from this attenuation kernel.
    [[nodiscard]] inline VolumeSegmentIntegral IntegrateVolumeSegment(double extinction, double distance)
    {
        if (!std::isfinite(extinction) || extinction < 0.0 || !std::isfinite(distance) || distance < 0.0)
        {
            throw std::invalid_argument("Volume extinction and distance must be finite and nonnegative.");
        }
        if (distance == 0.0)
        {
            return {};
        }
        if (extinction == 0.0)
        {
            return {1.0, distance};
        }
        const double opticalDepth = extinction * distance;
        // The thin limit avoids cancellation and a division by an underflowed optical depth.
        if (opticalDepth < 1.0e-5)
        {
            const double weight = 1.0 - opticalDepth * (0.5 - opticalDepth / 6.0);
            return {std::exp(-opticalDepth), distance * weight};
        }
        // Overflow of the product represents an opaque segment, not an invalid authored coefficient.
        return {std::exp(-opticalDepth), -std::expm1(-opticalDepth) / extinction};
    }

    struct VolumeRadiance
    {
        double Transmittance = 1.0;
        double Radiance = 0.0;
    };

    /// Per-channel front-to-back composition. Inputs are evaluated integrals, not raw densities.
    [[nodiscard]] inline VolumeRadiance ComposeVolumeRadiance(const VolumeRadiance& front,
                                                              const VolumeRadiance& back) noexcept
    {
        return {front.Transmittance * back.Transmittance, front.Radiance + front.Transmittance * back.Radiance};
    }
} // namespace Keire::Detail
