#pragma once

#include <array>
#include <compare>
#include <cstdint>

namespace Keire
{
    /// Local-frame volumetric cloud authoring values. Lengths are metres; extinction is inverse metres.
    /// Separate from artistic cloud sheets. Disabled by default for existing projects.
    struct VolumetricCloudSettings
    {
        bool Enabled = false;
        double BaseHeightMeters = 1500.0;
        double ThicknessMeters = 1200.0;
        double PatternSizeMeters = 3000.0;
        double Coverage = 0.6;
        /// Separate seeded horizontal weather from the legacy shape-atlas coverage field.
        bool IndependentWeather = false;
        /// Zero produces shallow layers; one permits the full authored vertical profile.
        double CloudType = 1.0;
        /// Regional variation around CloudType, independent of coverage and extinction.
        double TypeVariation = 0.5;
        /// Maximum regional displacement above/below the base height; independent weather only.
        double BaseHeightVariationMeters = 0.0;
        /// Ordered normalized layer heights: base start/end and crown start/end.
        std::array<double, 4> HeightProfile{0.0, 0.15, 0.7, 1.0};
        double ExtinctionPerMeter = 0.004;
        double DetailAmount = 0.2;
        /// Removes fine regions independently of the secondary shape octave.
        double ErosionAmount = 0.2;
        /// Integer frequency multiplier relative to the body; preserves periodic wind continuity.
        std::uint32_t DetailFrequency = 4;
        /// Coordinate displacement in atlas periods; zero disables large-scale variation.
        double VariationStrength = 0.75;
        /// Power-of-two atlas periods, dividing the wrapped local coordinate domain.
        std::uint32_t VariationPeriod = 64;
        /// Exponent applied to the height-shaped, eroded body; one preserves linear density.
        double DensityContrast = 1.0;
        /// Regional coverage variation, independent of shape displacement. Zero preserves uniform coverage.
        double RegionalCoverageStrength = 0.0;
        /// Independent weather: largest cell size in pattern units. Legacy: repeat size in atlas periods
        /// (four pattern units each). Powers of two preserve the wrapped local coordinate domain.
        std::uint32_t RegionalCoveragePeriod = 8;
        double Anisotropy = 0.65;
        std::uint32_t Seed = 1;
        std::array<double, 3> WindMetersPerSecond{5.0, 0.0, 0.0};
        std::array<double, 3> ScatteringAlbedo{0.95, 0.95, 0.95};
        std::uint32_t ViewSteps = 48;
        std::uint32_t ShadowSteps = 6;
        double MaximumDistanceMeters = 100000.0;
        // Contribution and shadow-depth reduction for two additional scattering approximations.
        double MultipleScatteringContribution = 0.5;
        double MultipleScatteringOcclusion = 0.5;
        /// Blend toward upper-hemisphere skylight attenuation; zero preserves unoccluded ambient lighting.
        double AmbientOcclusionStrength = 0.0;

        auto operator<=>(const VolumetricCloudSettings&) const noexcept = default;
    };
} // namespace Keire
