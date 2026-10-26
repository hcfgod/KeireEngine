#pragma once

#include "Keire/Rendering/VolumetricCloud.h"

#include <cmath>
#include <stdexcept>

namespace Keire::Detail
{
    inline void ValidateVolumetricCloudSettings(const VolumetricCloudSettings& settings)
    {
        const auto within = [](const double value, const double minimum, const double maximum)
        { return std::isfinite(value) && value >= minimum && value <= maximum; };

        if (!within(settings.BaseHeightMeters, -100000.0, 100000.0) ||
            !within(settings.ThicknessMeters, 1.0, 100000.0) || !within(settings.PatternSizeMeters, 1.0, 100000.0) ||
            !within(settings.MaximumDistanceMeters, 1.0, 1000000.0))
        {
            throw std::invalid_argument("Volumetric cloud geometry exceeds the supported local metre domain.");
        }
        if (!within(settings.Coverage, 0.0, 1.0) || !within(settings.ExtinctionPerMeter, 0.0, 10.0) ||
            !within(settings.DetailAmount, 0.0, 1.0) || !within(settings.Anisotropy, -0.95, 0.95))
        {
            throw std::invalid_argument("Volumetric cloud density or scattering parameters are invalid.");
        }
        if (settings.ViewSteps < 1 || settings.ViewSteps > 128 || settings.ShadowSteps < 1 || settings.ShadowSteps > 16)
        {
            throw std::invalid_argument("Volumetric cloud sample counts exceed supported quality budgets.");
        }
        for (const double speed : settings.WindMetersPerSecond)
        {
            if (!within(speed, -10000.0, 10000.0))
            {
                throw std::invalid_argument("Volumetric cloud wind is outside the supported metres-per-second range.");
            }
        }
        for (const double albedo : settings.ScatteringAlbedo)
        {
            if (!within(albedo, 0.0, 1.0))
            {
                throw std::invalid_argument("Volumetric cloud scattering albedo must be finite and within [0,1].");
            }
        }
    }
} // namespace Keire::Detail
