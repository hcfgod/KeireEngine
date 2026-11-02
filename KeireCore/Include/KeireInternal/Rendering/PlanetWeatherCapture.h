#pragma once

#include "KeireInternal/Rendering/PlanetEnvironmentCapture.h"

namespace Keire::Detail
{
    struct CapturedPlanetWeather
    {
        ResolvedPlanetFrame Frame;
        CloudSettings Clouds;
        FogSettings Fog;
        bool InheritsFog = false;
    };

    [[nodiscard]] inline std::vector<CapturedPlanetWeather>
    CapturePlanetWeather(const std::span<const PlanetEnvironmentView> candidates,
                         const std::optional<PlanetEnvironmentView>& selected,
                         const AtmosphereSettings& inheritedAtmosphere, const CloudSettings& inheritedClouds,
                         const FogSettings& inheritedFog, const std::size_t maximumPlanets)
    {
        const bool active =
            inheritedFog.Enabled || inheritedClouds.Volume.Enabled ||
            std::ranges::any_of(inheritedClouds.Layers, &CloudLayer::Enabled) ||
            std::ranges::any_of(candidates, [](const auto& candidate)
                                { return candidate.CloudOverride.has_value() || candidate.FogOverride.has_value(); });
        if (!active)
        {
            return {};
        }
        if (candidates.size() > maximumPlanets)
        {
            throw std::length_error("Planet weather exceeds the supported candidate count.");
        }
        ValidateCloudSettings(inheritedClouds);
        ValidateFogDensity(inheritedFog);
        // Weather still needs coherent observations when air transport is disabled.
        auto validationAtmosphere = inheritedAtmosphere;
        validationAtmosphere.Enabled = true;
        const auto observations = CapturePlanetAtmospheres(candidates, selected, validationAtmosphere);
        std::vector<CapturedPlanetWeather> result;
        result.reserve(observations.Frames.size());
        for (const auto& frame : observations.Frames)
        {
            const auto candidate = std::ranges::find(candidates, frame.Planet, &PlanetEnvironmentView::Planet);
            if (selected && selected->Planet == frame.Planet &&
                (candidate->CloudOverride != selected->CloudOverride ||
                 candidate->FogOverride != selected->FogOverride))
            {
                throw std::invalid_argument("Selected planet weather differs from its composition candidate.");
            }
            result.push_back({frame, candidate->CloudOverride.value_or(inheritedClouds),
                              candidate->FogOverride.value_or(inheritedFog), !candidate->FogOverride.has_value()});
        }
        return result;
    }
} // namespace Keire::Detail
