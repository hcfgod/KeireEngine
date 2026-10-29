#pragma once

#include "KeireInternal/Environment/CloudEvaluation.h"
#include "KeireInternal/Rendering/PlanetaryCloudUniforms.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace Keire::RenderBackend
{
    struct alignas(16) CloudLayerUniforms
    {
        // Relative sheet height, reciprocal pattern size, coverage, opacity (zero when disabled).
        std::array<float, 4> Geometry{};
        // Wrapped observer-relative X/Z phase, edge softness, horizon fade.
        std::array<float, 4> Pattern{};
        std::array<float, 4> Color{};
        std::array<std::uint32_t, 4> Noise{};
    };

    struct alignas(16) CloudUniforms
    {
        std::array<CloudLayerUniforms, MaximumCloudLayers> Layers;
    };

    static_assert(sizeof(CloudLayerUniforms) == 64);
    static_assert(offsetof(CloudLayerUniforms, Pattern) == 16);
    static_assert(offsetof(CloudLayerUniforms, Color) == 32);
    static_assert(offsetof(CloudLayerUniforms, Noise) == 48);
    static_assert(sizeof(CloudUniforms) == 128);

    [[nodiscard]] inline std::array<Vector4, MaximumCloudLayers>
    MakePlanetaryCloudLayerPhases(const CloudSettings& settings, const Detail::ResolvedPlanetFrame& frame)
    {
        std::array<Vector4, MaximumCloudLayers> result{};
        for (std::size_t index = 0; index < settings.Layers.size(); ++index)
        {
            const auto& layer = settings.Layers[index];
            const auto phase = MakePlanetaryCloudPhase(
                frame, layer.PatternSizeMeters, {layer.WindMetersPerSecond[0], 0.0, layer.WindMetersPerSecond[1]});
            result[index] = {phase[0], phase[1], phase[2], 0.0F};
        }
        return result;
    }

    [[nodiscard]] inline bool CloudObserverWithinLocalDomain(const std::array<double, 3>& observerFromAnchor) noexcept
    {
        return Detail::ValidCloudHorizontalCoordinate(observerFromAnchor[0]) &&
               Detail::ValidCloudVerticalCoordinate(observerFromAnchor[1]) &&
               Detail::ValidCloudHorizontalCoordinate(observerFromAnchor[2]);
    }

    /// Directions supplied to the shader must use the same east/up/north frame as this observer and anchor.
    [[nodiscard]] inline CloudUniforms
    BuildCloudUniforms(const CloudSettings& settings, const std::array<double, 3>& observerFromAnchor,
                       const std::array<std::array<double, 2>, MaximumCloudLayers>& advectionPhase)
    {
        Detail::ValidateCloudSettings(settings);
        if (!CloudObserverWithinLocalDomain(observerFromAnchor))
        {
            throw std::invalid_argument("Cloud observer height exceeds the local evaluation domain.");
        }
        CloudUniforms result;
        for (std::size_t index = 0; index < settings.Layers.size(); ++index)
        {
            const auto& layer = settings.Layers[index];
            auto phase = Detail::EvaluateCloudPhase(layer, {observerFromAnchor[0], observerFromAnchor[2]}, 0.0);
            for (std::size_t axis = 0; axis < phase.size(); ++axis)
            {
                const double displacement = advectionPhase[index][axis];
                if (!std::isfinite(displacement) || displacement < 0.0 || displacement >= 1.0)
                {
                    throw std::invalid_argument("Cloud advection phase must be finite and within [0,1).");
                }
                phase[axis] = Detail::CloudUnitPhase(phase[axis] - displacement);
            }
            auto& output = result.Layers[index];
            output.Geometry = {static_cast<float>(layer.AltitudeMeters - observerFromAnchor[1]),
                               static_cast<float>(1.0 / layer.PatternSizeMeters), static_cast<float>(layer.Coverage),
                               layer.Enabled ? static_cast<float>(layer.Opacity) : 0.0F};
            output.Pattern = {static_cast<float>(phase[0]), static_cast<float>(phase[1]),
                              static_cast<float>(layer.EdgeSoftness), static_cast<float>(layer.HorizonFade)};
            output.Color = {static_cast<float>(layer.LinearColor[0]), static_cast<float>(layer.LinearColor[1]),
                            static_cast<float>(layer.LinearColor[2]), 0.0F};
            output.Noise = {static_cast<std::uint32_t>(layer.Appearance), layer.Seed, 0U, 0U};
        }
        return result;
    }

    [[nodiscard]] inline CloudUniforms BuildCloudUniforms(const CloudSettings& settings,
                                                          const std::array<double, 3>& observerFromAnchor,
                                                          const double elapsedSeconds)
    {
        std::array<std::array<double, 2>, MaximumCloudLayers> advection{};
        for (std::size_t index = 0; index < settings.Layers.size(); ++index)
        {
            const auto negativePhase = Detail::EvaluateCloudPhase(settings.Layers[index], {0.0, 0.0}, elapsedSeconds);
            for (std::size_t axis = 0; axis < negativePhase.size(); ++axis)
            {
                advection[index][axis] = Detail::CloudUnitPhase(-negativePhase[axis]);
            }
        }
        return BuildCloudUniforms(settings, observerFromAnchor, advection);
    }
} // namespace Keire::RenderBackend
