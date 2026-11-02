#pragma once

#include "KeireInternal/Rendering/PlanetEnvironmentHistory.h"
#include "KeireInternal/Rendering/PlanetWeatherCapture.h"

namespace Keire::RenderBackend
{
    struct PlanetWeatherHistoryEntry
    {
        PlanetEnvironmentHistoryKey Geometry;
        std::array<double, 3> ObserverFromCenter{};
        CloudSettings Clouds;
        FogSettings Fog;
        double SecondsPerDay = 0.0;
        bool InheritsFog = false;

        bool operator==(const PlanetWeatherHistoryEntry&) const noexcept = default;
    };

    [[nodiscard]] inline std::vector<PlanetWeatherHistoryEntry>
    MakePlanetWeatherHistory(const std::span<const Detail::CapturedPlanetWeather> weather)
    {
        std::vector<PlanetWeatherHistoryEntry> result;
        result.reserve(weather.size());
        for (const auto& planet : weather)
        {
            const auto view = Detail::ToPlanetEnvironmentView(planet.Frame);
            result.push_back({*MakePlanetEnvironmentHistoryKey(view), view.ObserverFromCenterPlanetMetres,
                              planet.Clouds, planet.Fog, view.Clock.SecondsPerDay, planet.InheritsFog});
        }
        return result;
    }
} // namespace Keire::RenderBackend
