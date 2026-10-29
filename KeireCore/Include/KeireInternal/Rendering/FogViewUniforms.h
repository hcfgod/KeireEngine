#pragma once

#include "KeireInternal/Environment/FogDensity.h"

namespace Keire::RenderBackend
{
    struct alignas(16) FogLocalUniforms
    {
        std::array<float, 4> CenterExtinction{};
        std::array<float, 4> HalfExtentEdge{};

        bool operator==(const FogLocalUniforms&) const noexcept = default;
    };

    struct alignas(16) FogViewUniforms
    {
        // Camera-relative base height, height falloff, extinction and maximum distance.
        std::array<float, 4> HeightDensity{};
        // Scattering albedo RGB and phase anisotropy.
        std::array<float, 4> Scattering{};
        // Enabled, view steps, visible local volume count, visible exclusion count.
        std::array<std::uint32_t, 4> Controls{};
        std::array<FogLocalUniforms, MaximumLocalFogVolumes> Local{};
        // Appended in volume ABI 9. The extinction lane stores exclusion strength.
        std::array<FogLocalUniforms, MaximumFogExclusionRegions> Exclusions{};

        bool operator==(const FogViewUniforms&) const noexcept = default;
    };

    static_assert(sizeof(FogLocalUniforms) == 32);
    static_assert(sizeof(FogViewUniforms) == 1072);
    static_assert(offsetof(FogViewUniforms, Exclusions) == 560);
    static_assert(offsetof(FogViewUniforms, Local) == 48);

    [[nodiscard]] inline FogViewUniforms MakeFogViewUniforms(const FogSettings& settings,
                                                             const std::array<double, 3>& observerFromAnchor,
                                                             const AtmosphereSettings* planet = nullptr)
    {
        Detail::ValidateFogDensity(settings);
        if (!settings.Enabled)
        {
            return {};
        }
        for (const double coordinate : observerFromAnchor)
        {
            if (!std::isfinite(coordinate) || std::abs(coordinate) > 1.0e15)
            {
                throw std::invalid_argument("Fog observer exceeds the supported local frame domain.");
            }
        }
        FogViewUniforms result;
        result.HeightDensity = {
            static_cast<float>(settings.BaseHeightMeters - Detail::FogAltitude(settings, observerFromAnchor, planet)),
            static_cast<float>(settings.HeightFalloffPerMeter), static_cast<float>(settings.ExtinctionPerMeter),
            static_cast<float>(settings.MaximumDistanceMeters)};
        result.Scattering = {static_cast<float>(settings.ScatteringAlbedo[0]),
                             static_cast<float>(settings.ScatteringAlbedo[1]),
                             static_cast<float>(settings.ScatteringAlbedo[2]), static_cast<float>(settings.Anisotropy)};
        result.Controls = {settings.PlanetaryHeight ? 2U : 1U, settings.ViewSteps, 0U, 0U};
        for (const auto& volume : settings.LocalVolumes)
        {
            if (!volume.Enabled || volume.ExtinctionPerMeter == 0.0)
            {
                continue;
            }
            std::array<double, 3> relative{};
            bool outside = false;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                // Subtract before narrowing so nearby volumes retain precision far from the origin.
                relative[axis] = volume.Center[axis] - observerFromAnchor[axis];
                outside =
                    outside || std::abs(relative[axis]) > settings.MaximumDistanceMeters + volume.HalfExtent[axis];
            }
            if (outside)
            {
                continue;
            }
            auto& target = result.Local[result.Controls[2]++];
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                target.CenterExtinction[axis] = static_cast<float>(relative[axis]);
                target.HalfExtentEdge[axis] = static_cast<float>(volume.HalfExtent[axis]);
            }
            target.CenterExtinction[3] = static_cast<float>(volume.ExtinctionPerMeter);
            target.HalfExtentEdge[3] = static_cast<float>(volume.EdgeFraction);
        }
        for (const auto& region : settings.ExclusionRegions)
        {
            if (!region.Enabled || region.Strength == 0.0)
            {
                continue;
            }
            std::array<double, 3> relative{};
            bool outside = false;
            for (std::size_t axis = 0; axis < relative.size(); ++axis)
            {
                relative[axis] = region.Center[axis] - observerFromAnchor[axis];
                outside =
                    outside || std::abs(relative[axis]) > settings.MaximumDistanceMeters + region.HalfExtent[axis];
            }
            if (outside)
            {
                continue;
            }
            auto& target = result.Exclusions[result.Controls[3]++];
            for (std::size_t axis = 0; axis < relative.size(); ++axis)
            {
                target.CenterExtinction[axis] = static_cast<float>(relative[axis]);
                target.HalfExtentEdge[axis] = static_cast<float>(region.HalfExtent[axis]);
            }
            target.CenterExtinction[3] = static_cast<float>(region.Strength);
            target.HalfExtentEdge[3] = static_cast<float>(region.EdgeFraction);
        }
        return result;
    }
} // namespace Keire::RenderBackend
