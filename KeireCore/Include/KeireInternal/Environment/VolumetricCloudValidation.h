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
            !within(settings.DetailAmount, 0.0, 1.0) || !within(settings.ErosionAmount, 0.0, 1.0) ||
            !within(settings.Anisotropy, -0.95, 0.95) || !within(settings.MultipleScatteringContribution, 0.0, 1.0) ||
            !within(settings.MultipleScatteringOcclusion, 0.0, 1.0) ||
            !within(settings.AmbientOcclusionStrength, 0.0, 1.0))
        {
            throw std::invalid_argument("Volumetric cloud density or scattering parameters are invalid.");
        }
        if (settings.ViewSteps < 1 || settings.ViewSteps > 128 || settings.ShadowSteps < 1 || settings.ShadowSteps > 16)
        {
            throw std::invalid_argument("Volumetric cloud sample counts exceed supported quality budgets.");
        }
        if (settings.DetailFrequency < 1 || settings.DetailFrequency > 16 ||
            !within(settings.VariationStrength, 0.0, 1.0) || !within(settings.DensityContrast, 0.25, 4.0) ||
            settings.VariationPeriod < 1 || settings.VariationPeriod > 64 ||
            (settings.VariationPeriod & (settings.VariationPeriod - 1U)) != 0 ||
            !within(settings.RegionalCoverageStrength, 0.0, 1.0) || settings.RegionalCoveragePeriod < 1 ||
            settings.RegionalCoveragePeriod > 64 ||
            (settings.RegionalCoveragePeriod & (settings.RegionalCoveragePeriod - 1U)) != 0)
        {
            throw std::invalid_argument("Volumetric cloud detail, variation or contrast settings are invalid.");
        }
        if (!within(settings.CloudType, 0.0, 1.0) || !within(settings.TypeVariation, 0.0, 1.0) ||
            !within(settings.BaseHeightVariationMeters, 0.0, 100000.0))
        {
            throw std::invalid_argument("Cloud type or regional height variation is outside its finite range.");
        }
        double previousHeight = 0.0;
        for (const double height : settings.HeightProfile)
        {
            if (!within(height, previousHeight, 1.0))
            {
                throw std::invalid_argument("Cloud height profile must be finite, ordered and within [0,1].");
            }
            previousHeight = height;
        }
        if (static_cast<float>(settings.HeightProfile.front()) >= static_cast<float>(settings.HeightProfile.back()))
        {
            throw std::invalid_argument("Cloud height profile must occupy a nonzero layer at GPU float precision.");
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
