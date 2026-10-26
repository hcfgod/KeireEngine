#pragma once

#include "Keire/Rendering/Cloud.h"
#include "KeireInternal/Environment/VolumetricCloudValidation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Keire::Detail
{
    inline void ValidateCloudSettings(const CloudSettings& settings)
    {
        ValidateVolumetricCloudSettings(settings.Volume);
        const auto inRange = [](const double value, const double minimum, const double maximum)
        { return std::isfinite(value) && value >= minimum && value <= maximum; };
        for (const auto& layer : settings.Layers)
        {
            if (layer.Appearance != CloudAppearance::LegacyUnlit && layer.Appearance != CloudAppearance::NaturalLit)
            {
                throw std::invalid_argument("Unknown cloud appearance model.");
            }
            if (!inRange(layer.AltitudeMeters, -10000.0, 100000.0) ||
                !inRange(layer.PatternSizeMeters, 100.0, 1000000.0) || !inRange(layer.Coverage, 0.0, 1.0) ||
                !inRange(layer.Opacity, 0.0, 1.0) || !inRange(layer.EdgeSoftness, 0.001, 1.0) ||
                !inRange(layer.HorizonFade, 0.01, 1.0))
            {
                throw std::invalid_argument("Invalid layered cloud geometry or coverage.");
            }
            for (const auto wind : layer.WindMetersPerSecond)
            {
                if (!inRange(wind, -1000.0, 1000.0))
                {
                    throw std::invalid_argument("Cloud wind must be finite metres per second within +/-1000.");
                }
            }
            for (const auto color : layer.LinearColor)
            {
                if (!inRange(color, 0.0, 16.0))
                {
                    throw std::invalid_argument("Cloud linear color must be finite and within [0,16].");
                }
            }
        }
    }

    [[nodiscard]] inline double CloudUnitPhase(const double value) noexcept { return value - std::floor(value); }

    /// Explicit elapsed simulation seconds, bounded to retain subsecond evaluation precision.
    /// Observer X/Z must already be differences from the stable pattern anchor, never absolute universe coordinates.
    [[nodiscard]] inline std::array<double, 2> EvaluateCloudPhase(const CloudLayer& layer,
                                                                  const std::array<double, 2>& observerFromAnchor,
                                                                  const double elapsedSeconds)
    {
        CloudSettings validation;
        validation.Layers[0] = layer;
        ValidateCloudSettings(validation);
        if (!std::isfinite(elapsedSeconds) || std::abs(elapsedSeconds) > 1.0e9)
        {
            throw std::invalid_argument("Cloud elapsed seconds must be finite and within +/-1e9.");
        }
        std::array<double, 2> result;
        for (std::size_t axis = 0; axis < result.size(); ++axis)
        {
            if (!std::isfinite(observerFromAnchor[axis]) || std::abs(observerFromAnchor[axis]) > 1.0e9)
            {
                throw std::invalid_argument("Cloud anchor-relative displacement exceeds the local evaluation domain.");
            }
            result[axis] =
                CloudUnitPhase(std::fmod(observerFromAnchor[axis], layer.PatternSizeMeters) / layer.PatternSizeMeters -
                               std::fmod(elapsedSeconds * layer.WindMetersPerSecond[axis], layer.PatternSizeMeters) /
                                   layer.PatternSizeMeters);
        }
        return result;
    }

    /// CPU reference for the shader's periodic artistic density, not a volumetric density or weather simulation.
    [[nodiscard]] inline double SampleCloudPattern(const double x, const double z) noexcept
    {
        constexpr double tau = 6.28318530717958647692;
        return std::clamp(0.5 + 0.25 * std::sin(tau * x) * std::cos(tau * z) + 0.15 * std::sin(tau * (2.0 * x + z)) +
                              0.1 * std::cos(tau * (x - 3.0 * z)),
                          0.0, 1.0);
    }

    [[nodiscard]] inline std::uint32_t CloudNoiseHash(const std::uint32_t x, const std::uint32_t z,
                                                      const std::uint32_t seed) noexcept
    {
        std::uint32_t value = x * 0x8da6b343U ^ z * 0xd8163841U ^ seed * 0xcb1ab31fU;
        value ^= value >> 16U;
        value *= 0x7feb352dU;
        value ^= value >> 15U;
        value *= 0x846ca68bU;
        return value ^ (value >> 16U);
    }

    /// Float reference matches the shader's integer lattice and interpolation; backend results allow float tolerance.
    [[nodiscard]] inline float CloudPeriodicNoise(const float x, const float z, const std::uint32_t period,
                                                  const std::uint32_t seed) noexcept
    {
        if (period == 0 || period > 128U || !std::isfinite(x) || !std::isfinite(z))
        {
            return 0.0F;
        }
        const float px = (x - std::floor(x)) * static_cast<float>(period);
        const float pz = (z - std::floor(z)) * static_cast<float>(period);
        const auto ix = static_cast<std::uint32_t>(px);
        const auto iz = static_cast<std::uint32_t>(pz);
        const float fx = px - std::floor(px);
        const float fz = pz - std::floor(pz);
        const float sx = fx * fx * (3.0F - 2.0F * fx);
        const float sz = fz * fz * (3.0F - 2.0F * fz);
        const auto sample = [&](const std::uint32_t dx, const std::uint32_t dz)
        {
            return static_cast<float>(CloudNoiseHash((ix + dx) % period, (iz + dz) % period, seed) >> 8U) / 16777216.0F;
        };
        return std::lerp(std::lerp(sample(0, 0), sample(1, 0), sx), std::lerp(sample(0, 1), sample(1, 1), sx), sz);
    }

    [[nodiscard]] inline float SampleNaturalCloudPattern(const float x, const float z, const std::uint32_t seed,
                                                         const float footprint = 0.0F) noexcept
    {
        if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(footprint) || footprint < 0.0F)
        {
            return 0.0F;
        }
        const float localX = x - std::floor(x);
        const float localZ = z - std::floor(z);
        const float warpedX = localX + (CloudPeriodicNoise(localX, localZ, 4, seed + 11U) - 0.5F) * 0.12F;
        const float warpedZ = localZ + (CloudPeriodicNoise(localX, localZ, 4, seed + 29U) - 0.5F) * 0.12F;
        float density = 0.0F;
        float weight = 0.533333333F;
        for (std::uint32_t octave = 0; octave < 4; ++octave)
        {
            const auto period = 16U << octave;
            const float resolved = std::clamp(1.0F - footprint * static_cast<float>(period), 0.0F, 1.0F);
            density += std::lerp(0.5F, CloudPeriodicNoise(warpedX, warpedZ, period, seed + octave), resolved) * weight;
            weight *= 0.5F;
        }
        const float erosion = std::lerp(0.5F, CloudPeriodicNoise(warpedX, warpedZ, 128, seed + 71U),
                                        std::clamp(1.0F - footprint * 128.0F, 0.0F, 1.0F));
        return std::clamp((density - 0.22F) * 1.55F - erosion * 0.12F, 0.0F, 1.0F);
    }

    /// Layered-cloud reflected light approximation, not volumetric transport or multiple scattering.
    [[nodiscard]] inline std::array<float, 3> EvaluateNaturalCloudLight(const float density, const float sunElevation,
                                                                        const float viewSunCosine,
                                                                        const std::array<float, 3>& ambient,
                                                                        const std::array<float, 3>& sunlight) noexcept
    {
        const float core = std::lerp(1.0F, 0.45F, density);
        const float rim = 0.15F * std::pow(std::clamp(viewSunCosine, 0.0F, 1.0F), 8.0F) * (1.0F - density);
        const float direct = ((0.2F + 0.8F * std::clamp(sunElevation, 0.0F, 1.0F)) * core + rim) / 3.14159265359F;
        std::array<float, 3> result{};
        for (std::size_t channel = 0; channel < result.size(); ++channel)
        {
            result[channel] = ambient[channel] * (0.65F + 0.35F * (1.0F - density)) + sunlight[channel] * direct;
        }
        return result;
    }

    /// Bound reflected HDR light before tint multiplication and premultiplication; clear pixels remain exactly zero.
    [[nodiscard]] inline std::array<float, 3> PremultiplyNaturalCloudLight(const std::array<float, 3>& tint,
                                                                           const std::array<float, 3>& illumination,
                                                                           const float alpha) noexcept
    {
        constexpr float MaximumHdrValue = 65504.0F;
        std::array<float, 3> result{};
        for (std::size_t channel = 0; channel < result.size(); ++channel)
        {
            const float limit = MaximumHdrValue / std::max(tint[channel], 0.0001F);
            result[channel] = std::min(illumination[channel], limit) * tint[channel] * alpha;
        }
        return result;
    }
} // namespace Keire::Detail
