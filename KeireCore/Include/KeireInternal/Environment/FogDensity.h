#pragma once

#include "Keire/Rendering/Atmosphere.h"
#include "Keire/Rendering/Fog.h"
#include "KeireInternal/Environment/FogExclusion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace Keire::Detail
{
    inline void ValidateFogDensity(const FogSettings& settings)
    {
        const auto within = [](double value, double lower, double upper)
        { return std::isfinite(value) && value >= lower && value <= upper; };
        if (!within(settings.BaseHeightMeters, -100'000.0, 100'000.0) ||
            !within(settings.HeightFalloffPerMeter, 0.0, 10.0) || !within(settings.ExtinctionPerMeter, 0.0, 10.0) ||
            !within(settings.MaximumDistanceMeters, 1.0, 1'000'000.0) || !within(settings.Anisotropy, -0.95, 0.95) ||
            settings.ViewSteps < 1 || settings.ViewSteps > 128 ||
            settings.LocalVolumes.size() > MaximumLocalFogVolumes ||
            settings.ExclusionRegions.size() > MaximumFogExclusionRegions)
        {
            throw std::invalid_argument("Fog density, distance or quality exceeds the supported local metre domain.");
        }
        for (const auto& region : settings.ExclusionRegions)
        {
            ValidateFogExclusion(region);
        }
        for (const double albedo : settings.ScatteringAlbedo)
        {
            if (!within(albedo, 0.0, 1.0))
            {
                throw std::invalid_argument("Fog scattering albedo must be finite and within [0,1].");
            }
        }
        for (const auto& volume : settings.LocalVolumes)
        {
            if (!within(volume.ExtinctionPerMeter, 0.0, 10.0) || !within(volume.EdgeFraction, 0.0, 1.0))
            {
                throw std::invalid_argument("Local fog density or edge blend is invalid.");
            }
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                if (!std::isfinite(volume.Center[axis]) || !within(volume.HalfExtent[axis], 0.01, 1'000'000.0))
                {
                    throw std::invalid_argument("Local fog bounds must be finite with positive supported extents.");
                }
            }
        }
    }

    [[nodiscard]] inline double FogAltitude(const FogSettings& settings, const std::array<double, 3>& position,
                                            const AtmosphereSettings* planet)
    {
        for (const double coordinate : position)
        {
            if (!std::isfinite(coordinate))
            {
                throw std::invalid_argument("Fog sample position must be finite.");
            }
        }
        double altitude = position[1];
        if (settings.PlanetaryHeight)
        {
            if (planet == nullptr || !std::isfinite(planet->PlanetRadius) || planet->PlanetRadius <= 0.0)
            {
                throw std::invalid_argument("Planetary fog requires a finite positive planet radius.");
            }
            std::array<double, 3> relative{};
            for (std::size_t axis = 0; axis < relative.size(); ++axis)
            {
                relative[axis] = position[axis] - planet->PlanetCenter[axis];
                if (!std::isfinite(relative[axis]))
                {
                    throw std::invalid_argument("Planetary fog requires finite planet-relative coordinates.");
                }
            }
            altitude = std::hypot(relative[0], relative[1], relative[2]) - planet->PlanetRadius;
            if (!std::isfinite(altitude))
            {
                throw std::invalid_argument("Planetary fog altitude exceeds the supported numeric domain.");
            }
        }
        return altitude;
    }

    // Call after validating the settings once; avoid repeated validation in sampling loops.
    [[nodiscard]] inline double SampleFogExtinction(const FogSettings& settings, const std::array<double, 3>& position,
                                                    const AtmosphereSettings* planet = nullptr)
    {
        for (const double coordinate : position)
        {
            if (!std::isfinite(coordinate))
            {
                throw std::invalid_argument("Fog sample position must be finite.");
            }
        }
        if (!settings.Enabled)
        {
            return 0.0;
        }
        // Below the reference height density is capped, rather than growing exponentially underground.
        const double height = std::max(0.0, FogAltitude(settings, position, planet) - settings.BaseHeightMeters);
        double extinction = settings.ExtinctionPerMeter * std::exp(-height * settings.HeightFalloffPerMeter);
        for (const auto& volume : settings.LocalVolumes)
        {
            if (!volume.Enabled)
            {
                continue;
            }
            double normalizedDistance = 0.0;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                normalizedDistance = std::max(normalizedDistance,
                                              std::abs(position[axis] - volume.Center[axis]) / volume.HalfExtent[axis]);
            }
            if (normalizedDistance >= 1.0)
            {
                continue;
            }
            const double edge = volume.EdgeFraction == 0.0
                                    ? 1.0
                                    : std::clamp((1.0 - normalizedDistance) / volume.EdgeFraction, 0.0, 1.0);
            const double weight = edge * edge * (3.0 - 2.0 * edge);
            extinction += volume.ExtinctionPerMeter * weight;
        }
        return extinction * SampleFogExclusionRetention(settings.ExclusionRegions, position);
    }
} // namespace Keire::Detail
