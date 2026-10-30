#pragma once

#include "KeireInternal/Rendering/VolumeTransport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace Keire::Detail
{
    struct PlanetAtmosphereRayBody
    {
        // Subtract observer and center in the spatial frame before constructing this double-precision value.
        std::array<double, 3> ObserverFromCenter{};
        double AtmosphereRadius = 0.0;
        double OpaqueRadius = 0.0;
    };

    struct PlanetAtmosphereCoefficients
    {
        std::array<double, 3> Extinction{};
        std::array<double, 3> Source{};
    };

    struct PlanetAtmosphereRayResult
    {
        std::array<double, 3> Radiance{};
        std::array<double, 3> Transmittance{1.0, 1.0, 1.0};
        double EndDistance = 0.0;
        std::optional<std::size_t> OpaqueBody;
        std::size_t MediumSamples = 0;
    };

    namespace AtmosphereCompositionDetail
    {
        [[nodiscard]] inline std::optional<std::array<double, 2>>
        SphereInterval(const std::array<double, 3>& origin, const std::array<double, 3>& direction, const double radius)
        {
            double along = 0.0;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                along += origin[axis] * direction[axis];
            }
            const double distance = std::hypot(origin[0], origin[1], origin[2]);
            if (!std::isfinite(along) || !std::isfinite(distance))
            {
                throw std::out_of_range("Atmosphere ray geometry exceeds double precision.");
            }
            const std::array closest{std::fma(-along, direction[0], origin[0]),
                                     std::fma(-along, direction[1], origin[1]),
                                     std::fma(-along, direction[2], origin[2])};
            const double perpendicular = std::hypot(closest[0], closest[1], closest[2]);
            if (!std::isfinite(perpendicular))
            {
                throw std::out_of_range("Atmosphere closest approach exceeds double precision.");
            }
            if (perpendicular > radius)
            {
                return std::nullopt;
            }
            const double root = std::sqrt((radius - perpendicular) * (radius + perpendicular));
            const double stable = -along - std::copysign(root, along);
            const double other = distance > 2.0 * radius
                                     ? -along + std::copysign(root, along)
                                     : (stable == 0.0 ? 0.0 : (distance - radius) * (distance + radius) / stable);
            if (!std::isfinite(stable) || !std::isfinite(other))
            {
                throw std::out_of_range("Atmosphere intersection exceeds double precision.");
            }
            return std::array{(std::min)(stable, other), (std::max)(stable, other)};
        }
    } // namespace AtmosphereCompositionDetail

    // Numerical reference for the multi-planet shader contract. Partition at every medium boundary so vacuum
    // does not consume quadrature samples. Overlaps add coefficients before transport, never finished sky colors.
    // The sampler receives body index and ray distance in metres and returns source radiance per metre.
    // OpaqueBody terminates transport; Transmittance remains the attenuation to that surface, not to the sky.
    // Equal-distance opaque hits retain the first candidate. Renderer input must use stable planet identity order.
    template <typename Sampler>
    [[nodiscard]] PlanetAtmosphereRayResult
    IntegratePlanetAtmospheres(const std::span<const PlanetAtmosphereRayBody> bodies,
                               const std::array<double, 3>& direction, const double maximumDistance,
                               const std::size_t samplesPerInterval, Sampler&& sample)
    {
        constexpr std::size_t maximumBodies = 64;
        constexpr std::size_t maximumSamplesPerInterval = 1024;
        const double directionLength = std::hypot(direction[0], direction[1], direction[2]);
        if (bodies.size() > maximumBodies || !std::isfinite(maximumDistance) || maximumDistance < 0.0 ||
            !std::isfinite(directionLength) || std::abs(directionLength - 1.0) > 1.0e-12 || samplesPerInterval == 0 ||
            samplesPerInterval > maximumSamplesPerInterval)
        {
            throw std::invalid_argument("Atmosphere ray or reference integration budget is invalid.");
        }
        PlanetAtmosphereRayResult result;
        result.EndDistance = maximumDistance;
        std::vector<std::optional<std::array<double, 2>>> intervals;
        intervals.reserve(bodies.size());
        for (std::size_t index = 0; index < bodies.size(); ++index)
        {
            const auto& body = bodies[index];
            if (!std::isfinite(body.AtmosphereRadius) || body.AtmosphereRadius <= 0.0 ||
                !std::isfinite(body.OpaqueRadius) || body.OpaqueRadius < 0.0 ||
                body.OpaqueRadius > body.AtmosphereRadius ||
                !std::ranges::all_of(body.ObserverFromCenter, [](double value) { return std::isfinite(value); }))
            {
                throw std::invalid_argument("Atmosphere body geometry is invalid.");
            }
            intervals.push_back(
                AtmosphereCompositionDetail::SphereInterval(body.ObserverFromCenter, direction, body.AtmosphereRadius));
            if (body.OpaqueRadius == 0.0)
            {
                continue;
            }
            const auto solid =
                AtmosphereCompositionDetail::SphereInterval(body.ObserverFromCenter, direction, body.OpaqueRadius);
            const double radius =
                std::hypot(body.ObserverFromCenter[0], body.ObserverFromCenter[1], body.ObserverFromCenter[2]);
            // An outward surface ray has exit==0; it must not block its own sky.
            if (solid && (solid->at(1) > 0.0 || radius < body.OpaqueRadius))
            {
                const double entry = (std::max)(0.0, solid->at(0));
                if (entry <= result.EndDistance && (!result.OpaqueBody || entry < result.EndDistance))
                {
                    result.EndDistance = entry;
                    result.OpaqueBody = index;
                }
            }
        }
        std::vector<double> boundaries{0.0, result.EndDistance};
        for (const auto& interval : intervals)
        {
            if (interval)
            {
                boundaries.push_back(std::clamp(interval->at(0), 0.0, result.EndDistance));
                boundaries.push_back(std::clamp(interval->at(1), 0.0, result.EndDistance));
            }
        }
        std::ranges::sort(boundaries);
        boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
        std::vector<std::size_t> active;
        active.reserve(bodies.size());
        for (std::size_t boundary = 1; boundary < boundaries.size(); ++boundary)
        {
            const double start = boundaries[boundary - 1];
            const double end = boundaries[boundary];
            const double midpoint = start + (end - start) * 0.5;
            active.clear();
            for (std::size_t index = 0; index < intervals.size(); ++index)
            {
                if (intervals[index] && midpoint > intervals[index]->at(0) && midpoint < intervals[index]->at(1))
                {
                    active.push_back(index);
                }
            }
            if (active.empty())
            {
                continue;
            }
            const double step = (end - start) / static_cast<double>(samplesPerInterval);
            for (std::size_t sampleIndex = 0; sampleIndex < samplesPerInterval; ++sampleIndex)
            {
                const double distance = start + (static_cast<double>(sampleIndex) + 0.5) * step;
                PlanetAtmosphereCoefficients combined;
                for (const auto index : active)
                {
                    const auto medium = sample(index, distance);
                    ++result.MediumSamples;
                    for (std::size_t channel = 0; channel < 3; ++channel)
                    {
                        if (!std::isfinite(medium.Extinction[channel]) || medium.Extinction[channel] < 0.0 ||
                            !std::isfinite(medium.Source[channel]) || medium.Source[channel] < 0.0)
                        {
                            throw std::invalid_argument("Atmosphere coefficients must be finite and nonnegative.");
                        }
                        combined.Extinction[channel] += medium.Extinction[channel];
                        combined.Source[channel] += medium.Source[channel];
                        if (!std::isfinite(combined.Extinction[channel]) || !std::isfinite(combined.Source[channel]))
                        {
                            throw std::overflow_error("Combined atmosphere coefficients exceed double precision.");
                        }
                    }
                }
                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    const auto integral = IntegrateVolumeSegment(combined.Extinction[channel], step);
                    result.Radiance[channel] +=
                        result.Transmittance[channel] * combined.Source[channel] * integral.SourceWeight;
                    result.Transmittance[channel] *= integral.Transmittance;
                    if (!std::isfinite(result.Radiance[channel]))
                    {
                        throw std::overflow_error("Integrated atmosphere radiance exceeds double precision.");
                    }
                }
            }
        }
        return result;
    }
} // namespace Keire::Detail
