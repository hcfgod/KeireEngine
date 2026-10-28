#pragma once

#include "Keire/Rendering/VolumetricCloud.h"

#include <array>
#include <string_view>

namespace KeireEditor::Detail
{
    enum class CloudShapeStyle
    {
        SoftBodies,
        DefinedBillows,
        DenseBillows
    };

    inline constexpr std::array CloudShapeStyles{CloudShapeStyle::SoftBodies, CloudShapeStyle::DefinedBillows,
                                                 CloudShapeStyle::DenseBillows};

    [[nodiscard]] inline std::string_view CloudShapeStyleLabel(const CloudShapeStyle style)
    {
        switch (style)
        {
        case CloudShapeStyle::SoftBodies:
            return "Soft bodies";
        case CloudShapeStyle::DefinedBillows:
            return "Defined billows";
        case CloudShapeStyle::DenseBillows:
            return "Dense billows";
        }
        return "Custom";
    }

    // Shape-only starting points: preserve the user's placement, lighting, wind and rendering budget.
    [[nodiscard]] inline Keire::VolumetricCloudSettings CloudShapePreset(Keire::VolumetricCloudSettings settings,
                                                                         const CloudShapeStyle style)
    {
        const Keire::VolumetricCloudSettings defaults;
        const bool definedBillows = style != CloudShapeStyle::SoftBodies;
        settings.HeightProfile = defaults.HeightProfile;
        settings.Coverage = definedBillows ? 0.7 : defaults.Coverage;
        const double densityMultiplier = style == CloudShapeStyle::DenseBillows ? 10.0 : definedBillows ? 3.0 : 1.0;
        settings.ExtinctionPerMeter = defaults.ExtinctionPerMeter * densityMultiplier;
        settings.DetailAmount = definedBillows ? 0.7 : defaults.DetailAmount;
        settings.ErosionAmount = definedBillows ? 0.65 : defaults.ErosionAmount;
        settings.DensityContrast = definedBillows ? 0.65 : defaults.DensityContrast;
        settings.DetailFrequency = defaults.DetailFrequency;
        settings.VariationStrength = defaults.VariationStrength;
        settings.VariationPeriod = defaults.VariationPeriod;
        settings.RegionalCoverageStrength = definedBillows ? 1.0 : defaults.RegionalCoverageStrength;
        settings.RegionalCoveragePeriod = definedBillows ? 2U : defaults.RegionalCoveragePeriod;
        return settings;
    }

    [[nodiscard]] inline std::string_view CloudShapePresetLabel(const Keire::VolumetricCloudSettings& settings)
    {
        for (const auto style : CloudShapeStyles)
        {
            if (CloudShapePreset(settings, style) == settings)
            {
                return CloudShapeStyleLabel(style);
            }
        }
        return "Custom";
    }
} // namespace KeireEditor::Detail
