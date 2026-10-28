#pragma once

#include "KeireInternal/Environment/EnvironmentCloudPhase.h"
#include "KeireInternal/Environment/VolumetricCloudValidation.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace Keire::RenderBackend
{
    struct alignas(16) VolumetricCloudUniforms
    {
        // Camera-relative base height, thickness, pattern size and maximum ray distance, all metres.
        std::array<float, 4> Geometry{};
        // Coverage, extinction per metre, erosion and scattering anisotropy.
        std::array<float, 4> Density{};
        // Camera-relative periodic noise origin in pattern units. W is scattering occlusion.
        std::array<float, 4> PatternOrigin{};
        // RGB scattering albedo; W is multiple-scattering contribution.
        std::array<float, 4> Albedo{};
        // Enabled, deterministic seed, view samples and shadow samples.
        std::array<std::uint32_t, 4> Quality{};
    };

    static_assert(sizeof(VolumetricCloudUniforms) == 80);
    static_assert(offsetof(VolumetricCloudUniforms, Quality) == 64);

    [[nodiscard]] inline VolumetricCloudUniforms
    MakeVolumetricCloudUniforms(const VolumetricCloudSettings& settings,
                                const std::array<double, 3>& observerFromAnchor,
                                const Detail::EnvironmentClockState& clock)
    {
        Detail::ValidateVolumetricCloudSettings(settings);
        const Detail::EnvironmentClock validatedClock(clock);
        (void)validatedClock;
        VolumetricCloudUniforms result;
        constexpr double noisePeriod = 256.0;
        for (std::size_t axis = 0; axis < observerFromAnchor.size(); ++axis)
        {
            const double observer = observerFromAnchor[axis];
            if (!std::isfinite(observer) || std::abs(observer) > 1.0e9)
            {
                throw std::invalid_argument("Volumetric cloud observer exceeds the local frame domain.");
            }
            const double repeatMeters = settings.PatternSizeMeters * noisePeriod;
            const double wind = settings.WindMetersPerSecond[axis];
            const double daily = Detail::CloudPhaseDetail::DurationPhase(clock.SecondsPerDay, wind, repeatMeters, 1.0);
            const double fractional =
                Detail::CloudPhaseDetail::DurationPhase(clock.SecondsPerDay, wind, repeatMeters, clock.DayFraction);
            const double advection =
                Detail::CloudPhaseDetail::Wrap(Detail::CloudPhaseDetail::WholeDayPhase(daily, clock.Day) + fractional);
            const double observerPhase = std::fmod(observer, repeatMeters) / repeatMeters;
            result.PatternOrigin[axis] =
                static_cast<float>(Detail::CloudPhaseDetail::Wrap(observerPhase - advection) * noisePeriod);
            if (result.PatternOrigin[axis] >= 256.0F)
            {
                result.PatternOrigin[axis] = 0.0F;
            }
            result.Albedo[axis] = static_cast<float>(settings.ScatteringAlbedo[axis]);
        }
        result.Geometry = {static_cast<float>(settings.BaseHeightMeters - observerFromAnchor[1]),
                           static_cast<float>(settings.ThicknessMeters), static_cast<float>(settings.PatternSizeMeters),
                           static_cast<float>(settings.MaximumDistanceMeters)};
        result.Density = {static_cast<float>(settings.Coverage), static_cast<float>(settings.ExtinctionPerMeter),
                          static_cast<float>(settings.DetailAmount), static_cast<float>(settings.Anisotropy)};
        result.Quality = {settings.Enabled ? 1U : 0U, settings.Seed, settings.ViewSteps, settings.ShadowSteps};
        result.PatternOrigin[3] = static_cast<float>(settings.MultipleScatteringOcclusion);
        result.Albedo[3] = static_cast<float>(settings.MultipleScatteringContribution);
        return result;
    }
} // namespace Keire::RenderBackend
